#include "CBootLoader.h"
#include "CHostMgr.h"
#include "../network/CHttpServer.h"
#include "../network/CTcpServer.h"
#include "../ini/CINIHandler.h"
#include "../business/RequestCenter.h"
#include "../database/CDBEngine.h"
#include <cstdlib>
#include <utility>

namespace net
{
	void EnvInitialize();
	bool IsThreadEnable();
} // namespace net

CBootLoader::CBootLoader() = default;

CBootLoader::~CBootLoader()
{
	Finalize();
}

bool CBootLoader::Initialize()
{
	if (m_bInitialized)
	{
		return true;
	}

	m_exec = std::filesystem::current_path();

	m_nErrorCode = 0;
	m_strLastError.clear();
	net::EnvInitialize();
	if (!net::IsThreadEnable())
	{
		m_nErrorCode = 1;
		m_strLastError = "Failed to enable libevent thread support";
		return false;
	}

	m_pHostMgr = std::make_unique<CHostMgr>();
	if (!m_pHostMgr->GetActiveHost().has_value())
	{
		m_nErrorCode = 2;
		m_strLastError = "Failed to initialize CHostMgr";
		return false;
	}

	m_strToken = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "HQMarket", "token", std::string());
	if (m_strToken.empty())
	{
		m_nErrorCode = 3;
		m_strLastError = "HQMarket token is required in ini/system.ini";
		return false;
	}

	m_strPassword = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "HQMarket", "password", std::string());
	if (m_strPassword.empty())
	{
		m_nErrorCode = 4;
		m_strLastError = "HQMarket token password is required in ini/system.ini";
		return false;
	}

	std::string strTcpPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "tcp_port", std::string());
	std::string strHttpPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "http_port", std::string());
	if (strTcpPort.empty() || strHttpPort.empty())
	{
		m_nErrorCode = 5;
		m_strLastError = "HQMarket tcp_port && http_port is required in ini/system.ini";
		return false;
	}

	std::string strMySqlHost = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "host", std::string("127.0.0.1"));
	std::string strMySqlPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "port", std::string("3306"));
	std::string strMySqlAccount = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "account", std::string("root"));
	std::string strMySqlPassword = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "password", std::string());
	std::string strMySqlDatabase = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "database", std::string("wind"));
	int nMySqlPort = std::atoi(strMySqlPort.c_str());
	int nMySqlPoolSize = std::atoi(ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "MySQL", "pool_size", std::string("4")).c_str());
	db::CConnectParam dbParam(strMySqlHost, static_cast<unsigned int>(nMySqlPort), strMySqlAccount, strMySqlPassword, strMySqlDatabase, "utf8mb4");
	if ((0 != CDBEngine::InstanceRef().Initialize(db::em_database::mysql, dbParam, nMySqlPoolSize)) || !InitializeUserStorage())
	{
		m_nErrorCode = 5;
		m_strLastError = "MySQL initialization failed";
		return false;
	}

	int nTcpPort = std::atoi(strTcpPort.c_str());
	int nHttpPort = std::atoi(strHttpPort.c_str());

	m_pTcpServer = std::make_unique<net::CTcpServer>(nTcpPort);
	m_pHttpServer = std::make_unique<net::CHttpServer>(nHttpPort);

	m_bInitialized = true;
	return true;
}

bool CBootLoader::Run()
{
	if (!m_bInitialized || (nullptr == m_pTcpServer) || (nullptr == m_pHttpServer))
	{
		m_nErrorCode = 4;
		m_strLastError = "Boot loader is not initialized";
		return false;
	}
	if (0 != m_pTcpServer->Initialize())
	{
		m_nErrorCode = 4;
		m_strLastError = "TCP server initialization failed";
		return false;
	}
	if (0 != m_pHttpServer->Initialize())
	{
		m_nErrorCode = 5;
		m_strLastError = "HTTP server initialization failed";
		return false;
	}
	std::jthread tcpServerThread([this]() { m_pTcpServer->Start(true); });
	m_pHttpServer->Start(true);
	m_pTcpServer->ShutDown();
	return true;
}

void CBootLoader::Finalize()
{
	if (nullptr != m_pHttpServer)
	{
		m_pHttpServer->ShutDown();
	}
	if (nullptr != m_pTcpServer)
	{
		m_pTcpServer->ShutDown();
	}
	m_pHttpServer.reset();
	m_pTcpServer.reset();
	CDBEngine::InstanceRef().Close();
	m_bInitialized = false;
}

net::CTcpServer& CBootLoader::GetTcpServer()
{
	return *m_pTcpServer;
}

net::CHttpServer& CBootLoader::GetHttpServer()
{
	return *m_pHttpServer;
}

const std::string& CBootLoader::GetToken() const
{
	return m_strToken;
}

const std::string& CBootLoader::GetPassword() const
{
	return m_strPassword;
}

CHostMgr& CBootLoader::GetHostMgr()
{
	return *m_pHostMgr;
}

const std::string& CBootLoader::GetLastError() const
{
	return m_strLastError;
}

int CBootLoader::GetErrorCode() const
{
	return m_nErrorCode;
}
