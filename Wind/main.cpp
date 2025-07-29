#include <iostream>
#include <ranges>
#include <vector>
#include "./network/CNetServer.h"

net::CNetServer* pServer = nullptr;
int main()
{
	if (nullptr == pServer)
	{
		pServer = new net::CNetServer(9877);
		pServer->Initialize();
		pServer->Start(true);
	}

	return 0;
}