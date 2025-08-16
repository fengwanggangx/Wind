#ifndef __DATABASE_COMMON_H__
#define __DATABASE_COMMON_H__

#include <string>

namespace db
{
	struct CConnectParam
	{
		CConnectParam(const std::string& strHost, unsigned int nPort, const std::string& strAccount, const std::string& strPasswd, const std::string& strDB, const std::string& strCharset);
		CConnectParam(const std::string& strParam, char delimiter);
		std::string m_strHost;
		unsigned int m_nPort{ 0 };
		std::string m_strAccount;
		std::string m_strPasswd;
		std::string m_strDataBase;
		std::string m_strCharset;
	};

	enum class DataTypes
	{
		em_string = 0,
		em_int32,
		em_int64,
		em_double,
		em_bool,
		binary
	};

	enum class database
	{
		unknown = 0,
		mysql,
		oracle,
		sqlite
	};

	db::DataTypes GetDataType(db::database ty, int nType);
	std::string GetDBName(db::database ty);
	db::database GetDBType(const std::string strName);
}


#endif
