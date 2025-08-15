#include <iostream>
#include <ranges>
#include <vector>
#include "./network/CNetServer.h"
#include "./database/CDBEngine.h"
#include "./network/CNetDistributor.h"
#include "./request/request.h"
net::CNetServer* pServer = nullptr;
int main()
{
	if (nullptr == pServer)
	{
		pServer = new net::CNetServer(9877);
		pServer->Initialize();
		pServer->Start(true);
	}

	CDBEngine::InstancePtr()->Initialize();
	net::CNetDistributor<CRequest>::InstancePtr()->RegisterHandler(nullptr);
	return 0;
}