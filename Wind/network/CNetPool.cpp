#include "CNetPool.h"
#include <iostream>
#include "common.h"
#include "event2/event.h"
#include <event2/buffer.h>
#include <string.h>
namespace net
{
	struct CNetInfo
	{
		evutil_socket_t m_fd{ -1 };
		struct sockaddr_storage	m_addr { 0 };
		struct bufferevent* m_pEvent{ nullptr };
		std::string m_strAddress;
		int m_nPort{ -1 };

		void Empty()
		{
			m_fd = -1;
			m_pEvent = nullptr;
			memset(&m_addr, 0, sizeof(m_addr));
			m_strAddress.clear();
			m_nPort = -1;
			if (nullptr != m_pEvent)
			{
				bufferevent_free(m_pEvent);
				m_pEvent = nullptr;
			}
		}

		~CNetInfo()
		{
			Empty();
		}
	};

	CNetPool::CNetPool()
	{
	}

	CNetPool::~CNetPool()
	{

	}

	bool CNetPool::RegisterAConnection(evutil_socket_t fd, struct bufferevent* pEvent, struct sockaddr* pAddr)
	{
		if ((nullptr == pAddr) || (nullptr == pEvent))
		{
			return false;
		}

		auto [mIter, bInserted] = m_pool.try_emplace(fd, nullptr);
		CNetInfo* pInfo = mIter->second;
		if (bInserted)
		{
			pInfo = new CNetInfo;
			mIter->second = pInfo;
		}
		pInfo->Empty();
		pInfo->m_fd = fd;
		pInfo->m_pEvent = pEvent;
		SockAddrSafeCopy(pInfo->m_addr, *pAddr);
		ParseSockAddr(pInfo->m_strAddress, pInfo->m_nPort, *pAddr);
		return true;
	}

	struct bufferevent* CNetPool::RegisterAConnection(evutil_socket_t fd, struct bufferevent* pEvent, struct sockaddr_storage* pAddr)
	{
		if ((nullptr == pAddr) || (nullptr == pEvent))
		{
			return nullptr;
		}

		auto [mIter, bInserted] = m_pool.try_emplace(fd, nullptr);
		CNetInfo* pInfo = mIter->second;
		if (bInserted)
		{
			pInfo = new CNetInfo;
			mIter->second = pInfo;
		}
		pInfo->Empty();
		pInfo->m_fd = fd;
		pInfo->m_pEvent = pEvent;
		memset(&pInfo->m_addr, 0, sizeof(pInfo->m_addr));  // 初始化目标结构体（可选但安全）
		memcpy(&pInfo->m_addr, pAddr, sizeof(*pAddr));
		ParseSockAddr(pInfo->m_strAddress, pInfo->m_nPort, *pAddr);
		return pEvent;
	}

	struct bufferevent* CNetPool::RegisterConnect(evutil_socket_t fd, struct event_base* pNet, struct sockaddr* pAddr, int nLength, bufferevent_data_cb readcb, bufferevent_data_cb writecb, bufferevent_event_cb eventcb, void* cbarg)
	{
		if (!CheckSockAddress(pAddr, nLength))
		{
			evutil_closesocket(fd);
			return nullptr;
		}

		int nOptions = (BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS | BEV_OPT_UNLOCK_CALLBACKS);
		if (net::IsThreadEnable())
		{
			nOptions |= BEV_OPT_THREADSAFE;
		}

		struct bufferevent* pBuffer = bufferevent_socket_new(pNet, fd, nOptions);
		if (nullptr == pBuffer)
		{
			std::cerr << "Failed to create bufferevent" << std::endl;
			evutil_closesocket(fd);
			return nullptr;
		}

		bufferevent_setcb(pBuffer, readcb, writecb, eventcb, cbarg);
		bufferevent_enable(pBuffer, EV_READ | EV_WRITE);
		RegisterAConnection(fd, pBuffer, pAddr);
		return pBuffer;
	}

	bool CNetPool::CloseAConnection(CNetInfo& info)
	{
		struct bufferevent* pEvent = info.m_pEvent;
		if (nullptr == pEvent)
		{
			return false;
		}

		bufferevent_disable(pEvent, EV_READ | EV_WRITE);

		// 清空缓冲区
		evbuffer_drain(bufferevent_get_input(pEvent), -1);
		evbuffer_drain(bufferevent_get_output(pEvent), -1);

		bufferevent_free(pEvent);
		return true;
	}

	bool CNetPool::CloseAConnection(evutil_socket_t fd)
	{
		auto mIter = m_pool.find(fd);
		if (mIter == m_pool.end())
		{
			return false;
		}
		CNetInfo* pInfo = mIter->second;
		m_pool.erase(mIter);

		CloseAConnection(*pInfo);

		delete pInfo;
		pInfo = nullptr;
		return true;
	}


	bool CNetPool::SendData2Client(evutil_socket_t fd, const char* data, size_t nLength)
	{
		if ((nullptr == data) || (0 == nLength))
		{
			return false;
		}

		auto mIter = m_pool.find(fd);
		if (mIter == m_pool.end())
		{
			return false;
		}
		struct bufferevent* pEvent = mIter->second->m_pEvent;
		if (nullptr == pEvent)
		{
			return false;
		}
		struct evbuffer* pBuffer = bufferevent_get_output(pEvent);
		if (nullptr == pBuffer)
		{
			return false;
		}

		return evbuffer_add(pBuffer, data, nLength);
	}
}