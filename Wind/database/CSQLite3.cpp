#include "CSQLite3.h"
#include <unordered_map>

namespace db
{
	CSQLite3::CSQLite3()
	{

	}

	CSQLite3::~CSQLite3()
	{
		Close();
	}

	int CSQLite3::Connect(const CConnectParam& param)
	{
		return 0;
	}

	int CSQLite3::Close()
	{
		return 0;
	}

	int CSQLite3::ExecUpdate(const std::string& strSQL)
	{
		return 0;
	}

	const _TyTableInfo& CSQLite3::ExecQuery(const std::string& strSQL)
	{
		static _TyTableInfo t;
		return t;
	}

	bool CSQLite3::BeginTransaction()
	{
		return false;
	}

	bool CSQLite3::EndTransaction()
	{
		return false;
	}

	bool CSQLite3::RollBackTransaction()
	{
		return false;
	}

}