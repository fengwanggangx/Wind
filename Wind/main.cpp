#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include "./network/CTcpServer.h"
#include "./database/CDBEngine.h"
#include "./business/RequestCenter.h"
#include "./network/CHttpServer.h"
#include "./network/netcommon.h"

int BootLoader()
{
	// 必须在 CHttpServer 构造、event_base 创建之前调用。
	net::EnvInitialize();
	if (!net::IsThreadEnable())
	{
		std::cerr << "Failed to enable libevent thread support" << std::endl;
		return -1;
	}
	return 0;
}

net::CTcpServer* pTcpServer = nullptr;
void TcpTest()
{
	if (nullptr != pTcpServer)
	{
		return;
	}
	pTcpServer = new net::CTcpServer(9877);
	pTcpServer->RegisterHandler(Query);
	pTcpServer->RegisterHandler(Update);
	pTcpServer->RegisterHandler(Auth);

	pTcpServer->Initialize();
	pTcpServer->Start(true);
}


std::unique_ptr<net::CHttpResponseData> MakeResponse(int nStatus, std::string strBody, const std::string& strContentType = "text/plain; charset=utf-8")
{
	auto response = std::make_unique<net::CHttpResponseData>();
	response->m_nStatus = nStatus;
	response->m_headers["Content-Type"] = strContentType;
	response->m_strBody = std::move(strBody);
	return response;
}

net::CHttpServer* pHttpServer = nullptr;
void HttpTest()
{
	if (nullptr != pHttpServer)
	{
		return;
	}
	pHttpServer = new net::CHttpServer(8080);
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

	const int nRet = pHttpServer->Initialize();
	if (0 != nRet)
	{
		std::cerr << "CHttpServer initialize failed, error=" << nRet << std::endl;
		return;
	}
	pHttpServer->Start(true);
}

int main()
{
	BootLoader();
	HttpTest();
	return 0;
}
