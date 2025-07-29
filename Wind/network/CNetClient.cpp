#include "CNetClient.h"
#include "CNetTools.h"
#include <unordered_map>
#include <event2/buffer.h>
#include "common.h"
#include <iostream>
#include "../request/request.h"
//#include "../log/Defines.h"
#include "CNetPool.h"
#include "CNetDistributor.h"
#include <string.h>

#define RequestDisptcher net::CNetDistributor<CRequest>::InstancePtr()

namespace net
{
	CNetClient::CNetClient(const std::string& strAddr, int nPort) : m_strAddr(strAddr), m_nPort(nPort)
	{
		m_buffer_recv.reserve(4096);
		m_buffer_send.reserve(4096);
	}

	std::size_t CNetClient::OnRead(struct bufferevent* pEvent)
	{
		std::vector<CRequest*> reqs;
		std::size_t nCount = net::utility::RequestFromBuffer(reqs, pEvent, m_buffer_recv);
		for (const auto& req : reqs)
		{
			RequestDisptcher->AddRequest(req);
		}
		return nCount;
	}

	void CNetClient::OnConnected(bufferevent* pEvent)
	{
		printf("成功连接到服务器\n");

		// 记录连接信息
		struct sockaddr_storage addr;
		socklen_t addrlen = sizeof(addr);
		memset(&addr, 0, addrlen);
		if (getsockname(bufferevent_getfd(pEvent), (struct sockaddr*)&addr, &addrlen) == 0)
		{
			char host[NI_MAXHOST], port[NI_MAXSERV];
			if (getnameinfo((struct sockaddr*)&addr, addrlen, host, NI_MAXHOST, port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
			{
				printf("本地地址: %s:%s\n", host, port);
			}
		}

		evutil_socket_t fd = bufferevent_getfd(pEvent);
		memset(&addr, 0, addrlen);
		// 获取对端地址
		if (getpeername(fd, (struct sockaddr*)&addr, &addrlen) == 0)
		{
			char host[NI_MAXHOST], port[NI_MAXSERV];
			if (getnameinfo((struct sockaddr*)&addr, addrlen, host, NI_MAXHOST, port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
			{
				printf("服务器地址: %s:%s\n", host, port);
			}
		}

		
		bufferevent* pBuffer = CNetPool::InstancePtr()->RegisterAConnection(fd, pEvent, &addr);
	}

	void CNetClient::OnEvent(struct bufferevent* pEvent, short events)
	{

		// 处理连接关闭事件
		if (events & BEV_EVENT_EOF)
		{
			printf("服务器关闭了连接\n");
		}

		// 处理错误事件
		if (events & BEV_EVENT_ERROR)
		{
			int err = EVUTIL_SOCKET_ERROR();
			printf("连接错误: %s\n", evutil_socket_error_to_string(err));
		}

		// 处理超时事件
		if (events & BEV_EVENT_TIMEOUT)
		{
			printf("连接超时\n");
		}

		// 连接关闭或发生错误时的处理
		if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT))
		{
		}

		// 释放bufferevent资源
		if (pEvent)
		{
			evutil_socket_t fd = bufferevent_getfd(pEvent);
			CNetPool::InstancePtr()->CloseAConnection(fd);
			bufferevent_free(pEvent);
			pEvent = nullptr;
		}
	}

	int CNetClient::Initialize()
	{
		if (nullptr == GetNet())
		{
			return -1;
		}

		int nOptions = net::IsThreadEnable() ? (BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE) : BEV_OPT_CLOSE_ON_FREE;
		struct bufferevent* pEvent  = bufferevent_socket_new(GetNet(), -1, nOptions);

		// 连接服务器
		struct sockaddr_in svr;
		bool bRet = net::FmtAddress(svr, m_nPort, m_strAddr);
		if (!bRet)
		{
			bufferevent_free(pEvent);
			return -1;
		}
		int nRet = bufferevent_socket_connect(pEvent, (struct sockaddr*)&svr, sizeof(svr));
		if (nRet != 0)
		{
			bufferevent_free(pEvent);
			return nRet;
		}

		bufferevent_setcb(pEvent, CNetClient::Read_Callback, nullptr, CNetClient::Event_Callback, this);
		nRet = bufferevent_enable(pEvent, EV_READ | EV_WRITE);
		if (nRet != 0)
		{
			bufferevent_free(pEvent);
			return nRet;
		}
		return 0;
	}

	void CNetClient::Send(const char* pData)
	{

	}
	void CNetClient::Recv(const char* pData)
	{

	}
}