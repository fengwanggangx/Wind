#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include "./hqmarket/CHQMarket.h"
#include "./hqmarket/v1/market.pb.h"
#include "./network/CTcpServer.h"
#include "./network/CHttpServer.h"
#include "./network/common_net.h"
#include "./system/CBootLoader.h"


void TcpTest(net::CTcpServer* pTcpServer)
{
	if (nullptr == pTcpServer)
	{
		return;
	}
// 	pTcpServer->RegisterHandler(Query);
// 	pTcpServer->RegisterHandler(Update);
// 	pTcpServer->RegisterHandler(Auth);

}

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
	pHttpServer->RegisterHandler(net::HttpMethod::GET, "/health", [](const net::CHttpRequest&) {
			return MakeResponse(200, R"({"status":"ok"})", "application/json; charset=utf-8");
		});

	// GET http://server-ip:8080/hello?name=Wind
	pHttpServer->RegisterHandler(net::HttpMethod::GET, "/hello", [](const net::CHttpRequest& request) {
			std::string strName = request.GetQuery("name");
			if (strName.empty())
			{
				strName = "world";
			}
			return MakeResponse(200, "hello, " + strName + "\n");
		});

	// POST http://server-ip:8080/echo
	pHttpServer->RegisterHandler(net::HttpMethod::POST, "/echo", [](const net::CHttpRequest& request) {
			const std::string strContentType = request.GetHeader("content-type");
			return MakeResponse(200, request.GetBody(),
				strContentType.empty() ? "application/octet-stream" : strContentType);
		});
}

void HQMarketTest(CHQMarket* pHQMarket)
{
	if (nullptr == pHQMarket)
	{
		return;
	}

	pHQMarket->RegisterHandler([](const CRequest& request)
	{
		if (nullptr == request)
		{
			return;
		}

		const std::string strCommand = request->GetCmd();
		if ("subscription_ack" == strCommand)
		{
			std::cout << "600010.SSE minute-bar subscription accepted="
				<< request->GetReturnData("accepted") << '\n';
			return;
		}

		if (("bar_1m" != strCommand) && ("bar" != strCommand))
		{
			return;
		}
		const CData* pData = request->GetData();
		if (nullptr == pData)
		{
			return;
		}
		const hqmarket::market::v1::BarData* pBar = pData->GetDataAs<hqmarket::market::v1::BarData>();
		if ((nullptr == pBar) || ("600010" != pBar->instrument().symbol()))
		{
			return;
		}
		std::cout << "600010.SSE minute bar: beginTimeMs=" << pBar->begin_time_ms()
			<< ", open=" << pBar->open_price()
			<< ", high=" << pBar->high_price()
			<< ", low=" << pBar->low_price()
			<< ", close=" << pBar->close_price()
			<< ", volume=" << pBar->volume()
			<< ", priceScale=" << pBar->price_scale() << '\n';
	});
}

int main()
{
	CBootLoader boot;
	if (!boot.Initialize())
	{
		std::cerr << boot.GetLastError() << '\n';
		return boot.GetErrorCode();
	}
	TcpTest(&boot.GetTcpServer());
	HttpTest(&boot.GetHttpServer());
	CHQMarket hqMarket(&boot.GetTcpClient());
	if (!hqMarket.Initialize(boot.GetHQMarketToken()))
	{
		std::cerr << "HQMarket token is required\n";
		return 7;
	}
	HQMarketTest(&hqMarket);

	if (!boot.Run())
	{
		std::cerr << boot.GetLastError() << '\n';
		return boot.GetErrorCode();
	}
	hqMarket.Stop();
	boot.Finalize();
	return 0;
}
