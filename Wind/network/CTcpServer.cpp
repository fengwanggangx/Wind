#include "CTcpServer.h"
#include <event2/buffer.h>  // 主要头文件
#include "CNetTools.h"
#include <iostream>
#include <event2/thread.h>
#include <event2/listener.h>
#include <chrono>
#include <thread>
#include "netcommon.h"
#include "CNetPool.h"
#include "../request/request.h"
#include "../basic/CDistributor.h"


namespace net
{

	CTcpServer::CTcpServer(int nPort) : m_nPort(nPort), m_dispatcher(std::make_unique<_TyDistributor>())
	{
		m_buffer_recv.reserve(4096);
		m_buffer_send.reserve(4096);
	}

	void CTcpServer::OnListenerError(struct evconnlistener* pListener)
	{

	}

	void CTcpServer::OnConnAccept(struct evconnlistener* pListener, evutil_socket_t fd, struct sockaddr* pAddr, int nLength)
	{
		if (nullptr == GetNet())
		{
			return;
		}

		struct bufferevent* pBuffer = CNetPool::InstancePtr()->RegisterConnect(fd, GetNet(), pAddr, nLength, CTcpServer::Read_Callback, nullptr, CTcpServer::Event_Callback, this);
		if (nullptr != pBuffer)
		{
			CRequest* pReq = new CRequest;
			pReq->SetType(CRequest::Type::QUERY_AUTH);
			pReq->SetCmd("connet_build");
			pReq->SetExtraData("retmsg", "connect_ok_hahhahahahhahaha");
			net::utility::SendRequest(pReq, pBuffer, m_buffer_send);
		}
	}

	std::size_t CTcpServer::OnRead(struct bufferevent* pEvent)
	{
		std::vector<std::unique_ptr<CRequest>> reqs;
		std::size_t sz = net::utility::RequestFromBuffer(reqs, pEvent, m_buffer_recv);
		if (m_dispatcher)
		{
			m_dispatcher->Dispatch(std::move(reqs));
		}
		return sz;
	}

	void CTcpServer::OnEvent(struct bufferevent* pEvent, short events)
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

	int CTcpServer::Initialize()
	{
		if (nullptr == GetNet())
		{
			return -1;
		}
		struct sockaddr_in svr;
		if (!FmtAddress(svr, m_nPort))
		{
			return -1;
		}
		struct evconnlistener* pListener = evconnlistener_new_bind(
			GetNet(), 
			CTcpServer::ConnAccept_Callback,
			this,
			LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
			-1,
			(struct sockaddr*)&svr, 
			sizeof(svr));

		if (nullptr == pListener)
		{
			return -1;
		}
		//evconnlistener_set_error_cb(pListener, ListenerErrorCallback);
		return 0;
	}

	void CTcpServer::RegisterHandler(_TyHandler&& func)
	{
		m_dispatcher->RegisterHandler(std::move(func));
	}
}