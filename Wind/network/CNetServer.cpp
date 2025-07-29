#include "CNetServer.h"
#include <event2/buffer.h>  // 主要头文件
#include "CNetTools.h"
#include <iostream>
#include <event2/thread.h>
#include <event2/listener.h>
#include <chrono>
#include <thread>
#include "common.h"
#include "CNetPool.h"
#include "../request/request.h"

namespace net
{

	CNetServer::CNetServer(int nPort) : m_nPort(nPort)
	{
		m_buffer_recv.reserve(4096);
		m_buffer_send.reserve(4096);
	}

	void CNetServer::OnListenerError(struct evconnlistener* pListener)
	{

	}

	void CNetServer::OnConnAccept(struct evconnlistener* pListener, evutil_socket_t fd, struct sockaddr* pAddr, int nLength)
	{
		if (nullptr == GetNet())
		{
			return;
		}

		struct bufferevent* pBuffer = CNetPool::InstancePtr()->RegisterConnect(fd, GetNet(), pAddr, nLength, CNetServer::Read_Callback, nullptr, CNetServer::Event_Callback, this);
		if (nullptr != pBuffer)
		{
			CRequest* pReq = new CRequest;
			pReq->SetType(CRequest::Type::QUERY_AUTH);
			pReq->SetCmd("connet_build");
			pReq->SetExtraData("retmsg", "connect_ok_hahhahahahhahaha");
			net::utility::SendRequest(pReq, pBuffer, m_buffer_send);
		}
	}

	std::size_t CNetServer::OnRead(struct bufferevent* pEvent)
	{
		std::vector<CRequest*> reqs;
		return net::utility::RequestFromBuffer(reqs, pEvent, m_buffer_recv);
	}

	void CNetServer::OnEvent(struct bufferevent* pEvent, short events)
	{
		if (nullptr == pEvent)
		{
			return;
		}
		evutil_socket_t fd = bufferevent_getfd(pEvent);
		if (fd < 0)
		{
			bufferevent_free(pEvent);
			pEvent = nullptr;
		}
		//BEV_EVENT_EOF:onnection closed   BEV_EVENT_ERROR:some other error  BEV_EVENT_TIMEOUT:
		CNetPool::InstancePtr()->CloseAConnection(fd);
	}

	int CNetServer::Initialize()
	{
		if (nullptr == GetNet())
		{
			return -1;
		}
		struct sockaddr_in svr;
		bool bRet = FmtAddress(svr, m_nPort);
		struct evconnlistener* pListener = evconnlistener_new_bind(
			GetNet(), 
			CNetServer::ConnAccept_Callback,
			this,
			LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
			-1,
			(struct sockaddr*)&svr, 
			sizeof(svr));

		//evconnlistener_set_error_cb(pListener, ListenerErrorCallback);
		return 0;
	}
}