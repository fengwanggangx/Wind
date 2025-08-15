#ifndef __CDBENGINE_H__
#define __CDBENGINE_H__

#include "../common/ISingleton.h"
#include <memory>
#include <functional>


namespace db
{
	class IDataBase;
	class CODBC;
	using _TyDBReleasor = std::function<void(IDataBase*)>;
	using _TyDBPtr = std::unique_ptr<IDataBase, _TyDBReleasor>;
}

class CDBEngine final : public ISingleton<CDBEngine>
{
	DECLARE_SINGLE_DFAULT(CDBEngine)
public:
	void Initialize();
	db::_TyDBPtr GetDBPtr(const std::string& strType);
private:
	db::CODBC* m_pODBC{nullptr};
};
#endif
