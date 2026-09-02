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

	std::string strTcpPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "tcp_port", std::string());
	std::string strHttpPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "http_port", std::string());
	if (strTcpPort.empty() || strHttpPort.empty())
	{
		m_nErrorCode = 3;
		m_strLastError = "HQMarket tcp_port && http_port is required in ini/system.ini";
		return false;
	}

	std::string strHQMarketServer = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "hqmarket_server", std::string());
	std::string strHQMarketPort = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "System", "hqmarket_port", std::string());
	if (strHQMarketServer.empty() || strHQMarketPort.empty())
	{
		m_nErrorCode = 4;
		m_strLastError = "HQMarket hqmarket_server && hqmarket_port is required in ini/system.ini";
		return false;
	}

	int nTcpPort = std::atoi(strTcpPort.c_str());
	int nHttpPort = std::atoi(strHttpPort.c_str());

	m_pTcpServer = std::make_unique<net::CTcpServer>(nTcpPort);
	m_pHttpServer = std::make_unique<net::CHttpServer>(nHttpPort);

	int nHQMarketPort = std::atoi(strHQMarketPort.c_str());
	m_pTcpClient = std::make_unique<net::CTcpClient>(strHQMarketServer, nHQMarketPort);

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

const std::string& CBootLoader::GetToken() const
{
	return m_strToken;
}

const std::string& CBootLoader::GetLastError() const
{
	return m_strLastError;
}

int CBootLoader::GetErrorCode() const
{
	return m_nErrorCode;
}
