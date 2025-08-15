#include "CODBC.h"
#include <unordered_map>
#include "CSQLite3.h"
#include "CMySQL.h"
#include "COracle.h"



namespace db
{
	IDataBase* CreateDB(db::database ty)
	{
		switch (ty)
		{
		case db::database::sqlite:
			return new CSQLite3();
		case db::database::mysql:
			return new CMySQL();
		case db::database::oracle:
			return new COracle();
		default:
			break;
		}
		return nullptr;
	}

	CODBC::CODBC()
	{
		m_Releasor = [this](IDataBase* pDB)
			{
				if (nullptr != pDB)
				{
					std::lock_guard<std::mutex> lck(m_mtx);
					pDB->m_status = status::free;
				}

			};
	}

	CODBC::~CODBC()
	{

	}

	int CODBC::Connect(db::database ty, const CConnectParam& param, int nCount)
	{
		std::lock_guard<std::mutex> lck(m_mtx);
		auto& pool = m_database[ty];
		for (int i = 0; i < nCount; ++i)
		{
			IDataBase* pDB = CreateDB(ty);
			pDB->Connect(param);
			pool.emplace_back(pDB);
		}
		return 0;
	}

	int CODBC::Close()
	{
		std::lock_guard<std::mutex> lck(m_mtx);
		for (const auto& data : m_database)
		{
			for (const auto& item : data.second)
			{
				if (nullptr != item)
				{
					item->Close();
					delete item;
				}
			}
		}
		m_database.clear();
		return 0;
	}

	int CODBC::Close(db::database ty)
	{
		std::lock_guard<std::mutex> lck(m_mtx);
		const auto& mIter = m_database.find(ty);
		if (m_database.end() == mIter)
		{
			return 0;
		}
		for (const auto& item : mIter->second)
		{
			if (nullptr != item)
			{
				item->Close();
				delete item;
			}
		}
		m_database.erase(ty);
		return 0;
	}

	_TyDBPtr CODBC::GetADataBase(db::database ty)
	{
		std::lock_guard<std::mutex> lck(m_mtx);
		const auto& mIter = m_database.find(ty);
		if (m_database.end() == mIter)
		{
			return nullptr;
		}

		_TyPool& pool = mIter->second;
		for (const auto& item : pool)
		{
			if (nullptr == item)
			{
				continue;
			}
			if (item->m_status != db::status::free)
			{
				continue;
			}

			item->m_status = db::status::busy;
			return _TyDBPtr(item, m_Releasor);
		}
		return nullptr;
	}

}