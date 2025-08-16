#ifndef __NET_COMMON_H__
#define __NET_COMMON_H__
#include <netinet/in.h>
#include <string>
namespace net
{
	bool IsThreadEnable();
	void EnvInitialize();
	bool FmtAddress(struct ::sockaddr_in& addr, int nPort, const std::string& strAddr = "");

	std::string ParseSockAddr(std::string& strAddr, int& nPort, const struct sockaddr& addr);
	std::string ParseSockAddr(std::string& strAddr, int& nPort, const struct sockaddr_storage& addr);

	bool CheckSockAddress(struct sockaddr* pAddr, int nLength);
	bool SockAddrSafeCopy(struct sockaddr& dst, const struct sockaddr& src);
	bool SockAddrSafeCopy(struct sockaddr_storage& dst, const struct sockaddr& src);
}


#endif
