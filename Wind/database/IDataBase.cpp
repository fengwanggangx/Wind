#include "IDataBase.h"

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
} // namespace db
