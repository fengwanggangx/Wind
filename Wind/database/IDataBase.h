#ifndef __IDATABASE_H__
#define __IDATABASE_H__

#include <string>
#include <vector>
#include "common_db.h"

namespace db
{
	struct CColumnInfo
	{
		unsigned int m_uId{ 0 };
		std::string m_strName;
		db::em_data_types m_type{ db::em_data_types::em_string };
		int m_nDecimal{ -1 };
	};

	enum class status
	{
		free,
		busy
	};

	using _TyColumnInfo = db::CColumnInfo;

	using _TyColumns = std::vector<db::CColumnInfo>;

	using _TyRows = std::vector<std::vector<std::string>>;

	using _TyTableInfo = std::pair<_TyColumns, _TyRows>;

	class IDataBase
	{
		public:
			virtual ~IDataBase() = default;

		public:
			virtual int Connect(const CConnectParam& param) = 0;
			virtual int Close() = 0;

			// virtual int ReConnect(const std::string& strFile) = 0;

			virtual int ExecUpdate(const std::string& strSQL) = 0;
			virtual const _TyTableInfo& ExecQuery(const std::string& strSQL) = 0;

			virtual bool BeginTransaction() = 0;
			virtual bool EndTransaction() = 0;
			virtual bool RollBackTransaction() = 0;

			virtual bool Transaction(const std::string& strSQL);

		public:
			db::status m_status{ db::status::free };
	};
} // namespace db
#endif
