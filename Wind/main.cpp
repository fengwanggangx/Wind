#include <iostream>
#include <ranges>
#include <vector>
#include "./network/CNetServer.h"
#include "./database/CDBEngine.h"
#include "./business/RequestCenter.h"

int main()
{
	net::CNetServer* pServer = nullptr;
	if (nullptr == pServer)
	{
		pServer = new net::CNetServer(9877);
		pServer->Initialize();
		pServer->Start(true);
	}

	CDBEngine::InstancePtr()->Initialize();
	pServer->RegisterHandler(Query);
	pServer->RegisterHandler(Update);
	pServer->RegisterHandler(Auth);
	return 0;
}