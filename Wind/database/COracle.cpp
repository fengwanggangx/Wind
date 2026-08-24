#include "COracle.h"
#include <unordered_map>

namespace db
{
	COracle::COracle()
	{
	}

	COracle::~COracle()
	{
		Close();
	}

	int COracle::Connect(const CConnectParam& param)
	{
		return -1;
	}

	int COracle::Close()
	{
		return 0;
	}

	int COracle::ExecUpdate(const std::string& strSQL)
	{
		return -1;
	}

	const _TyTableInfo& COracle::ExecQuery(const std::string& strSQL)
	{
		static _TyTableInfo t;
		return t;
	}

	bool COracle::BeginTransaction()
	{
		return false;
	}

	bool COracle::EndTransaction()
	{
		return false;
	}

	bool COracle::RollBackTransaction()
	{
		return false;
	}
} // namespace db
