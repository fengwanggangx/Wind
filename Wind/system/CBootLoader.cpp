#include "CBootLoader.h"
#include "../network/CHttpServer.h"
#include "../network/CTcpServer.h"
#include "../network/CTcpClient.h"
#include "../ini/CINIHandler.h"
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

	m_strToken = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "HQMarket", "token", std::string());
	if (m_strToken.empty())
	{
		m_nErrorCode = 2;
		m_strLastError = "HQMarket token is required in ini/system.ini";
		return false;
	}
	net::EnvInitialize();
	if (!net::IsThreadEnable())
	{
		m_nErrorCode = 1;
		m_strLastError = "Failed to enable libevent thread support";
		return false;
	}

	const char* pszWindTcpPort = std::getenv("WIND_TCP_PORT");
	const char* pszWindHttpPort = std::getenv("WIND_HTTP_PORT");
	int nWindTcpPort = ((nullptr != pszWindTcpPort) && ('\0' != *pszWindTcpPort)) ? std::atoi(pszWindTcpPort) : 9801;
	int nWindHttpPort = ((nullptr != pszWindHttpPort) && ('\0' != *pszWindHttpPort)) ? std::atoi(pszWindHttpPort) : 9802;
	m_pTcpServer = std::make_unique<net::CTcpServer>(nWindTcpPort);
	m_pHttpServer = std::make_unique<net::CHttpServer>(nWindHttpPort);
	const char* pszHQMarketHost = std::getenv("HQMARKET_HOST");
	const char* pszHQMarketPort = std::getenv("HQMARKET_PORT");
	std::string strHQMarketHost = ((nullptr != pszHQMarketHost) && ('\0' != *pszHQMarketHost)) ? pszHQMarketHost : "127.0.0.1";
	int nHQMarketPort = ((nullptr != pszHQMarketPort) && ('\0' != *pszHQMarketPort)) ? std::atoi(pszHQMarketPort) : 9901;
	m_pTcpClient = std::make_unique<net::CTcpClient>(strHQMarketHost, nHQMarketPort);

	m_bInitialized = true;
	return true;
}

bool CBootLoader::Run()
{
	if (!m_bInitialized || (nullptr == m_pTcpServer) || (nullptr == m_pTcpClient) || (nullptr == m_pHttpServer))
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
	if (0 != m_pTcpClient->Initialize())
	{
		m_nErrorCode = 6;
		m_strLastError = "HQMarket TCP client initialization failed";
		return false;
	}

	std::jthread tcpServerThread([this]() { m_pTcpServer->Start(true); });
	std::jthread tcpClientThread([this]() { m_pTcpClient->Start(true); });
	m_pHttpServer->Start(true);
	m_pTcpClient->ShutDown();
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
	if (nullptr != m_pTcpClient)
	{
		m_pTcpClient->ShutDown();
	}

	m_pHttpServer.reset();
	m_pTcpClient.reset();
	m_pTcpServer.reset();
	m_bInitialized = false;
}

net::CTcpServer& CBootLoader::GetTcpServer()
{
	return *m_pTcpServer;
}

net::CTcpClient& CBootLoader::GetTcpClient()
{
	return *m_pTcpClient;
}

net::CHttpServer& CBootLoader::GetHttpServer()
{
	return *m_pHttpServer;
}

const std::string& CBootLoader::GetHQMarketToken() const
{
	return m_strHQMarketToken;
}

const std::string& CBootLoader::GetLastError() const
{
	return m_strLastError;
}

int CBootLoader::GetErrorCode() const
{
	return m_nErrorCode;
}
