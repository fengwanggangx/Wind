#include <iostream>
#include <memory>
#include <string>
#include <utility>
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

	if (!boot.Run())
	{
		std::cerr << boot.GetLastError() << '\n';
		return boot.GetErrorCode();
	}
	boot.Finalize();
	return 0;
}
