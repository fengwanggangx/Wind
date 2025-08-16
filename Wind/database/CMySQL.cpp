#include "CMySQL.h"
#include <unordered_map>
#include <mysql/mysql.h>
#include "dbcommon.h"
#include <memory>

namespace db
{
	CMySQL::CMySQL()
	{
	}

	CMySQL::~CMySQL()
	{
		Close();
	}

	int CMySQL::Connect(const db::CConnectParam& param)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (IsValid())
		{
			return 0;
		}
		if (nullptr == m_pDB)
		{
			m_pDB = mysql_init(nullptr);
		}

		mysql_options(m_pDB, MYSQL_SET_CHARSET_NAME, param.m_strCharset.c_str());
		mysql_options(m_pDB, MYSQL_OPT_RECONNECT, (const char*)1);  // 自动重连
		if (!mysql_real_connect(m_pDB, param.m_strHost.c_str(), param.m_strAccount.c_str(), param.m_strPasswd.c_str(), param.m_strDataBase.c_str(), param.m_nPort, nullptr, 0))
		{
			const char* pError = mysql_error(m_pDB);
			mysql_close(m_pDB);
			m_pDB = nullptr;
			return 1;
		}
		return 0;
	}

	void CMySQL::DisConnect()
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (IsValid())
		{
			mysql_close(m_pDB);
		}
		m_pDB = nullptr;
	}

	bool CMySQL::IsValid()
	{
		return (nullptr != m_pDB) && (0 == mysql_ping(m_pDB));
	}

	int CMySQL::Close()
	{
		m_pDB = nullptr;
		return 0;
	}

	const _TyTableInfo& CMySQL::ExecQuery(const std::string& strSQL)
	{
		thread_local _TyTableInfo s_table;

		_TyColumns& columns = s_table.first;
		_TyRows& rows = s_table.second;

		columns.clear();
		columns.clear();

		if (!IsValid())
		{
			return s_table;
		}
		int nRet = mysql_query(m_pDB, strSQL.c_str());
		if (0 != nRet)
		{
			const char* pError = mysql_error(m_pDB);
			return s_table;
		}

		std::unique_ptr<MYSQL_RES, void(*)(MYSQL_RES*)> result
		(
			mysql_store_result(m_pDB), 
			[](MYSQL_RES* p) 
			{
				if (nullptr != p)
				{
					mysql_free_result(p);
				}
			}
		);
		if (nullptr == result.get())
		{
			//(0 == mysql_field_count(m_pDB))
			const char* pError = mysql_error(m_pDB);
			return s_table;
		}

		int nFields = mysql_num_fields(result.get());
		MYSQL_FIELD* pFields = mysql_fetch_fields(result.get());

		columns.reserve(nFields);
		for (int i = 0; i < nFields; ++i)
		{
			_TyColumnInfo& col = columns[i];
			col.m_strName = pFields[i].name;
			col.m_type = db::GetDataType(database::mysql, pFields[i].type);
		}
		uint64_t nRows = mysql_num_rows(result.get());

		rows.reserve(nRows);
		MYSQL_ROW row;
		while ((row = mysql_fetch_row(result.get())))
		{
			std::vector<std::string> rowData(nFields);
			unsigned long* lengths = mysql_fetch_lengths(result.get());

			for (int i = 0; i < nFields; ++i) 
			{
				if (row[i])
				{
					rowData.emplace_back(row[i], lengths[i]);
				}
				else
				{
					rowData.emplace_back("");
				}
			}

			rows.emplace_back(std::move(rowData));
		}
		return s_table;
	}

	int CMySQL::ExecUpdate(const std::string& strSQL)
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (!IsValid())
		{
			return 0;
		}
		int nRet = mysql_query(m_pDB, strSQL.c_str());
		if (0 != nRet)
		{
			const char* pError = mysql_error(m_pDB);
		}
		return nRet;
	}

	bool CMySQL::BeginTransaction()
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (!IsValid())
		{
			return false;
		}
		return 0 == mysql_autocommit(m_pDB, 0);
	}

	bool CMySQL::EndTransaction()
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (!IsValid())
		{
			return false;
		}
		bool bRet = 0 == mysql_commit(m_pDB);
		mysql_autocommit(m_pDB, 1);
		return bRet;
	}

	bool CMySQL::RollBackTransaction()
	{
		std::lock_guard<std::mutex> lock(m_mtx);
		if (!IsValid())
		{
			return false;
		}
		bool bRet = 0 == mysql_rollback(m_pDB);
		mysql_autocommit(m_pDB, 1);
		return bRet;
	}

}