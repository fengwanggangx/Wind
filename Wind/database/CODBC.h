#ifndef __CODBC_H__
#define __CODBC_H__

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include "dbcommon.h"
#include "../common/defines.h"


namespace db
{
	class IDataBase;
	struct CConnectParam;

	using _TyDBReleasor = std::function<void(IDataBase*)>;
	using _TyDBPtr = std::unique_ptr<IDataBase, _TyDBReleasor>;

	using _TyPool = std::vector<IDataBase*>;

	class CODBC final
	{
		DECLARE_ONLY_CUSTOM_CONSTRUCT(CODBC)
	public:
		int Connect(db::database ty, const CConnectParam& param, int nCount);
		int Close();
		int Close(db::database ty);

		_TyDBPtr GetADataBase(db::database ty);
	private:
		std::mutex m_mtx;
		std::unordered_map<db::database, _TyPool> m_database;
		_TyDBReleasor m_Releasor{ nullptr };
	};

}
#endif
