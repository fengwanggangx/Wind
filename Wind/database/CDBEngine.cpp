#include "CDBEngine.h"

CDBEngine::CDBEngine() : m_pODBC(std::make_unique<db::CODBC>())
{
}

CDBEngine::~CDBEngine()
{
	Close();
}

int CDBEngine::Initialize(db::em_database ty, const db::CConnectParam& param, int nCount)
{
	if (nullptr == m_pODBC)
	{
		return -1;
	}
	return m_pODBC->Connect(ty, param, nCount);
}

int CDBEngine::Close()
{
	if (nullptr == m_pODBC)
	{
		return 0;
	}
	return m_pODBC->Close();
}

int CDBEngine::Close(db::em_database t)
{
	if (nullptr == m_pODBC)
	{
		return 0;
	}
	return m_pODBC->Close(t);
}

db::_TyDBPtr CDBEngine::GetDBPtr(db::em_database t)
{
	if (nullptr == m_pODBC)
	{
		return nullptr;
	}
	return m_pODBC->GetADataBase(t);
}

db::_TyDBPtr CDBEngine::GetDBPtr(const std::string& strType)
{
	return GetDBPtr(db::GetDBType(strType));
}

std::size_t CDBEngine::Count(db::em_database t) const
{
	if (nullptr == m_pODBC)
	{
		return 0;
	}
	return m_pODBC->Count(t);
}
