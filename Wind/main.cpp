#include "./network/CHttpServer.h"
#include "./network/CTcpServer.h"
#include "./network/common_net.h"
#include "./strategy/CStrategyEngine.h"
#include "./system/CBootLoader.h"
#include "./system/CBrokerService.h"
#include "./system/CSession.h"
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

std::unique_ptr<net::CHttpResponseData> MakeResponse(int nStatus, std::string strBody, const std::string& strContentType = "text/plain; charset=utf-8")
{
	auto response = std::make_unique<net::CHttpResponseData>();
	response->m_nStatus = nStatus;
	response->m_headers["Content-Type"] = strContentType;
	response->m_strBody = std::move(strBody);
	return response;
}

void HttpTest(net::CHttpServer* pHttpServer)
{
	if (nullptr == pHttpServer)
	{
		return;
	}

	// GET http://server-ip:8080/health
	pHttpServer->RegisterHandler(net::HttpMethod::GET, "/health", [](const net::CHttpRequest&)
								 { return MakeResponse(200, R"({"status":"ok"})", "application/json; charset=utf-8"); });

	// GET http://server-ip:8080/hello?name=Wind
	pHttpServer->RegisterHandler(net::HttpMethod::GET, "/hello", [](const net::CHttpRequest& request)
								 {
			std::string strName = request.GetQuery("name");
			if (strName.empty())
			{
				strName = "world";
			}
			return MakeResponse(200, "hello, " + strName + "\n"); });

	// POST http://server-ip:8080/echo
	pHttpServer->RegisterHandler(net::HttpMethod::POST, "/echo", [](const net::CHttpRequest& request)
								 {
			const std::string strContentType = request.GetHeader("content-type");
			return MakeResponse(200, request.GetBody(),
				strContentType.empty() ? "application/octet-stream" : strContentType); });
}

int main()
{
	CBootLoader boot;
	if (!boot.Initialize())
	{
		std::cerr << boot.GetLastError() << '\n';
		return boot.GetErrorCode();
	}
	HttpTest(&boot.GetHttpServer());
	std::optional<CHostInfo> host = boot.GetHostMgr().GetActiveHost();
	if (!host.has_value())
	{
		std::cerr << "HQMarket active host is unavailable\n";
		return 7;
	}
	//连接HQMarket
	CSession session({ boot.GetAccount(), boot.GetPassword(), { }, host.value() });

	//策略引擎
	CStrategyEngine strategyEngine(&session);
	if (!strategyEngine.Initialize())
	{
		std::cerr << "Strategy engine initialization failed: " << strategyEngine.GetLastError() << '\n';
		return 7;
	}

	//客户端连接响应
	CBrokerService brokerService(&boot.GetTcpServer(), &session, &strategyEngine);
	if (!brokerService.Initialize())
	{
		std::cerr << "Trade service initialization failed\n";
		return 7;
	}

	if (!session.Start())
	{
		std::cerr << "HQMarket session configuration is invalid\n";
		return 7;
	}
	if (!boot.Run())
	{
		std::cerr << boot.GetLastError() << '\n';
		strategyEngine.StopAll();
		session.Stop();
		return boot.GetErrorCode();
	}
	strategyEngine.StopAll();
	session.Stop();
	boot.Finalize();
	return 0;
}
