#include "CDBEngine.h"
#include "CODBC.h"

std::unordered_multimap<db::database, std::pair<db::CConnectParam, int>> s_params
{
	{ db::database::mysql, { {"", ','}, 10 } },
	{ db::database::mysql, { {"", ','}, 10 } }
};




CDBEngine::CDBEngine() : m_pODBC(new db::CODBC())
{

}

CDBEngine::~CDBEngine()
{

}

void CDBEngine::Initialize()
{
	for (const auto& item : s_params)
	{
		m_pODBC->Connect(item.first, item.second.first, item.second.second);
	}
}

db::_TyDBPtr CDBEngine::GetDBPtr(const std::string& strType)
{
	return m_pODBC->GetADataBase(db::GetDBType(strType));
}
