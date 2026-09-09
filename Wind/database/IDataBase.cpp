#include "IDataBase.h"

#include <fstream>
#include <iterator>

namespace db
{
	bool IDataBase::Transaction(const std::string& strSQL)
	{
		if (!BeginTransaction())
		{
			return false;
		}
		if (0 != ExecUpdate(strSQL))
		{
			RollBackTransaction();
			return false;
		}
		if (!EndTransaction())
		{
			RollBackTransaction();
			return false;
		}
		return true;
	}

	int IDataBase::ExecScript(const std::string& strSQL)
	{
		return ExecUpdate(strSQL);
	}

	int IDataBase::ExecSqlFile(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath);
		if (!input.is_open())
		{
			return -1;
		}

		std::string strSQL{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
		if (strSQL.empty())
		{
			return -1;
		}

		return ExecScript(strSQL);
	}
} // namespace db
