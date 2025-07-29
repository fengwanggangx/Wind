#include "CNet.h"
#include "common.h"
#include <iostream>
#include <event2/thread.h>
#include <arpa/inet.h>
#include <string.h>
#include <event2/util.h>
// #include <event2/event.h>
// #include <event2/thread.h>

namespace net
{

	std::string GetErrorString(int errorCode) {
#ifdef _MSC_VER
		char buffer[256];
		if (strerror_s(buffer, sizeof(buffer), errorCode) != 0) {
			return "Unknown error";
		}
		return buffer;
#else
		return std::to_string(errorCode);	
#endif
	}

	bool EnableLibeventThread()
	{
#if defined(_WIN32)
		if (evthread_use_windows_threads() != 0)
		{
			std::cout << "Failed to enable Windows threads support" << std::endl;
			return false;
		}
#else
		if (evthread_use_pthreads() != 0) {
			std::cerr << "Failed to enable pthreads support" << std::endl;
			return -1;
		}
#endif
		return true;
	}

	std::atomic_bool bThreadEnable{ false };

	void EnvInitialize()
	{
#if defined(_WIN32)
		WSADATA wver;
		WSAStartup(MAKEWORD(2, 2), &wver);
#endif
		bThreadEnable.store(EnableLibeventThread());
	}

	bool IsThreadEnable()
	{
		return bThreadEnable.load();
	}

	bool FmtAddress(struct ::sockaddr_in& addr, int nPort, const std::string& strAddr/* = ""*/)
	{
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(static_cast<uint16_t>(nPort));
		if (!strAddr.empty())
		{
			int nRet = evutil_inet_pton(AF_INET, strAddr.c_str(), &addr.sin_addr.s_addr);
			return nRet == 1;
		}
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		return true;
	}

	std::string ParseSockAddr(std::string& strAddr, int& nPort, const struct ::sockaddr& addr)
	{
		// 根据地址族判断类型
		if (AF_INET == addr.sa_family)
		{
			char buffer[INET_ADDRSTRLEN];
			const struct ::sockaddr_in* pAddr = reinterpret_cast<const struct ::sockaddr_in*>(&addr);
			if (!inet_ntop(AF_INET, &pAddr->sin_addr, buffer, INET_ADDRSTRLEN))
			{
				return GetErrorString(errno);
			}

			nPort = ntohs(pAddr->sin_port);
			strAddr = buffer;
			return "";
		}

		if (AF_INET6 == addr.sa_family)
		{
			char buffer[INET6_ADDRSTRLEN];
			const auto* pAddr = reinterpret_cast<const struct sockaddr_in6*>(&addr);
			if (!inet_ntop(AF_INET6, &pAddr->sin6_addr, buffer, INET6_ADDRSTRLEN))
			{
				return GetErrorString(errno);
			}

			nPort = ntohs(pAddr->sin6_port);
			strAddr = buffer;
			return "";
		}
		return "unkown sa_family";
	}

	std::string ParseSockAddr(std::string& strAddr, int& nPort, const struct sockaddr_storage& addr)
	{
		strAddr.clear();
		nPort = -1;
		// 根据地址族判断类型
		if (AF_INET == addr.ss_family)
		{
			char buffer[INET_ADDRSTRLEN] = {0};
			const auto* pAddr = reinterpret_cast<const struct sockaddr_in*>(&addr);
			if (!inet_ntop(AF_INET, &pAddr->sin_addr, buffer, INET_ADDRSTRLEN))
			{
				return GetErrorString(errno);
			}

			nPort = ntohs(pAddr->sin_port);
			strAddr = buffer;
			return "";
		}

		if (AF_INET6 == addr.ss_family)
		{
			char buffer[INET6_ADDRSTRLEN] = { 0 };
			const auto* pAddr = reinterpret_cast<const struct sockaddr_in6*>(&addr);
			if (!inet_ntop(AF_INET6, &pAddr->sin6_addr, buffer, INET6_ADDRSTRLEN))
			{
				return GetErrorString(errno);
			}

			nPort = ntohs(pAddr->sin6_port);
			strAddr = buffer;
			return "";
		}
		return "unkown sa_family";
	}


	bool CheckSockAddress(struct sockaddr* pAddr, int nLength)
	{
		if (nullptr == pAddr)
		{
			return false;
		}
		//IPv4地址
		if ((pAddr->sa_family == AF_INET) && (nLength == sizeof(struct sockaddr_in))) 
		{
			return true;
		}
		
		//IPv6地址
		if (pAddr->sa_family == AF_INET6 && nLength == sizeof(struct sockaddr_in6)) 
		{
			return true;
		}
		return false;
	}


	bool SockAddrSafeCopy(struct sockaddr& dst, const struct sockaddr& src)
	{
		memset(&dst, 0, sizeof(dst));
		if (src.sa_family == AF_INET) 
		{
			memcpy(&dst, &src, sizeof(struct sockaddr_in));
			return true;
		}
		
		if (src.sa_family == AF_INET6) 
		{
			memcpy(&dst, &src, sizeof(struct sockaddr_in6));
			return true;
		}

		return false;
	}

	bool SockAddrSafeCopy(struct sockaddr_storage& dst, const struct sockaddr& src)
	{
		memset(&dst, 0, sizeof(dst));
		if (src.sa_family == AF_INET)
		{
			memcpy(&dst, &src, sizeof(struct sockaddr_in));
			return true;
		}

		if (src.sa_family == AF_INET6)
		{
			memcpy(&dst, &src, sizeof(struct sockaddr_in6));
			return true;
		}

		return false;
	}

}
