#include "dbcommon.h"
#include <unordered_map>
#include <charconv>

#include <mysql/field_types.h>
#include "../common/utility.h"


namespace db
{

	CConnectParam::CConnectParam(const std::string& strHost, unsigned int nPort, const std::string& strAccount, const std::string& strPasswd, const std::string& strDB, const std::string& strCharset) :
		m_strHost(strHost), m_nPort(nPort), m_strAccount(strAccount), m_strPasswd(strPasswd), m_strDataBase(strDB), m_strCharset(strCharset)
	{

	}

	CConnectParam::CConnectParam(const std::string& strParam, char delimiter)
	{
		std::vector<std::string> data;
		std::size_t sz = utility::stringsplit(strParam, data, delimiter, false);
		if (sz == 6)
		{
			m_strHost = data.at(0);
			utility::s2n(data.at(1), m_nPort);
			m_strAccount = data.at(2);
			m_strPasswd = data.at(3);
			m_strDataBase = data.at(4);
			m_strCharset = data.at(5);
		}
	}

	std::unordered_map<db::database, std::string> s_db
	{
		{ db::database::mysql, "mysql" },
		{ db::database::oracle, "oracle" },
		{ db::database::sqlite, "sqlite" },
	};

	std::string GetDBName(db::database ty)
	{
		const auto& mIter = s_db.find(ty);
		return (s_db.end() == mIter) ? "" : mIter->second;
	}

	db::database GetDBType(const std::string strName)
	{
		for (const auto& item : s_db)
		{
			if (strName == item.second)
			{
				return item.first;
			}
		}
		return db::database::unknown;
	}

	std::unordered_map<database, std::unordered_map<int, db::DataTypes>> s_types
	{
		{ database::mysql,
		{
			{ enum_field_types::MYSQL_TYPE_DECIMAL, DataTypes::em_double},
			{ enum_field_types::MYSQL_TYPE_TINY, DataTypes::em_int32 },
			{ enum_field_types::MYSQL_TYPE_SHORT, DataTypes::em_int32 },
			{ enum_field_types::MYSQL_TYPE_LONG, DataTypes::em_int64 },
			{ enum_field_types::MYSQL_TYPE_FLOAT, DataTypes::em_double},
			{ enum_field_types::MYSQL_TYPE_DOUBLE, DataTypes::em_double},
			{ enum_field_types::MYSQL_TYPE_NULL, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_TIMESTAMP, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_LONGLONG, DataTypes::em_int64 },
			{ enum_field_types::MYSQL_TYPE_INT24, DataTypes::em_int32 },
			{ enum_field_types::MYSQL_TYPE_DATE, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_TIME, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_DATETIME, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_YEAR, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_NEWDATE, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_VARCHAR, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_BIT, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_TIMESTAMP2, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_DATETIME2, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_TIME2,  DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_TYPED_ARRAY, DataTypes::em_string },
			{ enum_field_types::MYSQL_TYPE_INVALID, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_BOOL, DataTypes::em_bool },
			{ enum_field_types::MYSQL_TYPE_JSON, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_NEWDECIMAL, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_ENUM, DataTypes::em_int32 },
			{ enum_field_types::MYSQL_TYPE_SET, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_TINY_BLOB, DataTypes::binary},
			{ enum_field_types::MYSQL_TYPE_MEDIUM_BLOB, DataTypes::binary},
			{ enum_field_types::MYSQL_TYPE_LONG_BLOB, DataTypes::binary},
			{ enum_field_types::MYSQL_TYPE_BLOB, DataTypes::binary},
			{ enum_field_types::MYSQL_TYPE_VAR_STRING, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_STRING, DataTypes::em_string},
			{ enum_field_types::MYSQL_TYPE_GEOMETRY, DataTypes::em_string}

		}

		}
	};

	db::DataTypes GetDataType(db::database ty, int nType)
	{
		const auto& mIter = s_types.find(ty);
		if (s_types.end() == mIter)
		{
			return db::DataTypes::em_string;
		}
		const auto& em = mIter->second;
		const auto& mmIter = em.find(nType);
		if (em.end() == mmIter)
		{
			return db::DataTypes::em_string;
		}
		return mmIter->second;
	}

}
