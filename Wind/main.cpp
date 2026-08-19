#include <iostream>
#include <ranges>
#include <vector>
#include "./network/CTcpServer.h"
#include "./database/CDBEngine.h"
#include "./business/RequestCenter.h"

int main()
{
	net::CTcpServer* pServer = nullptr;
	if (nullptr == pServer)
	{
		pServer = new net::CTcpServer(9877);
		pServer->RegisterHandler(Query);
		pServer->RegisterHandler(Update);
		pServer->RegisterHandler(Auth);

		pServer->Initialize();
		pServer->Start(true);
	}

	//CDBEngine::InstancePtr()->Initialize();
	return 0;
}