#include "./ini/CINIHandler.h"
#include "./network/CHttpServer.h"
#include "./network/CTcpServer.h"
#include "./network/common_net.h"
#include "./strategy/CSimulatedTradingService.h"
#include "./strategy/CStrategyEngine.h"
#include "./strategy/strategies/CMovingAverageStrategy.h"
#include "./system/CBootLoader.h"
#include "./system/CBrokerService.h"
#include "./system/CSession.h"
#include <functional>
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

bool InitializeStrategies(CStrategyEngine& strategyEngine)
{
	if (!strategyEngine.RegisterFactory("moving_average", []()
	{
		return std::make_unique<CMovingAverageStrategy>();
	}))
	{
		return false;
	}
	ini::CINIHandler& iniHandler = ini::CINIHandler::InstanceRef();
	std::string strEnabled = iniHandler.GetValue(ini::Config::System, "Strategy", "enabled", std::string("0"));
	if (("1" != strEnabled) && ("true" != strEnabled))
	{
		return true;
	}
	std::string strSecurity = iniHandler.GetValue(ini::Config::System, "Strategy", "security", std::string());
	std::string strExchange = iniHandler.GetValue(ini::Config::System, "Strategy", "exchange", std::string());
	std::string strChannel = iniHandler.GetValue(ini::Config::System, "Strategy", "channel", std::string("quote"));
	CStrategyConfig config;
	config.m_id = 1;
	config.m_strType = "moving_average";
	config.m_strName = "default_moving_average";
	config.m_parameters.emplace("short_window", iniHandler.GetValue(ini::Config::System, "Strategy", "short_window", std::string("5")));
	config.m_parameters.emplace("long_window", iniHandler.GetValue(ini::Config::System, "Strategy", "long_window", std::string("20")));
	config.m_subscriptions.emplace_back(strSecurity, market::ParseMarket(strExchange), market::ParseChannel(strChannel));
	config.m_bAutoStart = true;
	return strategyEngine.CreateStrategy(config);
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
	CSession session({ boot.GetAccount(), boot.GetPassword(), { }, host.value() });
	CSimulatedTradingService tradingService;
	CStrategyEngine strategyEngine(&session, &tradingService, &tradingService);
	tradingService.SetOrderEventHandler(std::bind_front(&CStrategyEngine::OnOrderEvent, &strategyEngine));
	CBrokerService brokerService(&boot.GetTcpServer(), &session);
	if (!brokerService.Initialize())
	{
		std::cerr << "Trade service initialization failed\n";
		return 7;
	}
	session.RegisterHandler(std::bind_front(&CStrategyEngine::OnMarketResponse, &strategyEngine));
	session.RegisterStateHandler(std::bind_front(&CStrategyEngine::OnMarketState, &strategyEngine));
	if (!InitializeStrategies(strategyEngine))
	{
		std::cerr << "Strategy engine initialization failed\n";
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
		tradingService.Stop();
		session.Stop();
		return boot.GetErrorCode();
	}
	strategyEngine.StopAll();
	tradingService.Stop();
	session.Stop();
	boot.Finalize();
	return 0;
}
