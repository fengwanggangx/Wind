#include "CNetPool.h"
#include <iostream>
#include "common_net.h"
#include "event2/event.h"
#include <event2/buffer.h>
#include <string.h>
#include <utility>
#include <vector>
namespace net
{
	struct CNetInfo
	{
		CNetInfo()
		{
			m_buffer_recv.reserve(2048);
			m_buffer_send.reserve(2048);
		}

		_TyConnectionId m_fd{ -1 };

		int m_nPort{ -1 };
		std::string m_strAddress;
		struct sockaddr_storage m_addr { 0 };
		struct bufferevent* m_pEvent{ nullptr };
		
		std::vector<char> m_buffer_recv;
		std::vector<char> m_buffer_send;

		void Empty()
		{
			m_fd = -1;
			if (m_pEvent != nullptr)
			{
				bufferevent_free(m_pEvent);
				m_pEvent = nullptr;
			}

			m_nPort = -1;
			m_strAddress.clear();
			memset(&m_addr, 0, sizeof(m_addr));
			
			m_buffer_recv.clear();
			m_buffer_send.clear();
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

	bool CNetPool::RegisterAConnection(_TyConnectionId id, struct bufferevent* pEvent, struct sockaddr* pAddr)
	{
		if ((nullptr == pAddr) || (nullptr == pEvent))
		{
			return false;
		}

		std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		auto [mIter, bInserted] = m_pool.try_emplace(id, nullptr);
		CNetInfo* pInfo = mIter->second.get();
		if (bInserted)
		{
			mIter->second = std::make_unique<CNetInfo>();
			pInfo = mIter->second.get();
		}
		pInfo->Empty();
		pInfo->m_fd = id;
		pInfo->m_pEvent = pEvent;
		SockAddrSafeCopy(pInfo->m_addr, *pAddr);
		ParseSockAddr(pInfo->m_strAddress, pInfo->m_nPort, *pAddr);
		return true;
	}

	struct bufferevent* CNetPool::RegisterAConnection(_TyConnectionId id, struct bufferevent* pEvent, struct sockaddr_storage* pAddr)
	{
		if ((nullptr == pAddr) || (nullptr == pEvent))
		{
			return nullptr;
		}

		std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		auto [mIter, bInserted] = m_pool.try_emplace(id, nullptr);
		CNetInfo* pInfo = mIter->second.get();
		if (bInserted)
		{
			mIter->second = std::make_unique<CNetInfo>();
			pInfo = mIter->second.get();
		}
		pInfo->Empty();
		pInfo->m_fd = id;
		pInfo->m_pEvent = pEvent;
		memset(&pInfo->m_addr, 0, sizeof(pInfo->m_addr)); // 初始化目标结构体（可选但安全）
		memcpy(&pInfo->m_addr, pAddr, sizeof(*pAddr));
		ParseSockAddr(pInfo->m_strAddress, pInfo->m_nPort, *pAddr);
		return pEvent;
	}

	struct bufferevent* CNetPool::RegisterConnect(_TyConnectionId id, struct event_base* pNet, struct sockaddr* pAddr, int nLength, bufferevent_data_cb readcb, bufferevent_data_cb writecb, bufferevent_event_cb eventcb, void* cbarg)
	{
		if (!CheckSockAddress(pAddr, nLength))
		{
			evutil_closesocket(id);
			return nullptr;
		}

		int nOptions = (BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS | BEV_OPT_UNLOCK_CALLBACKS);
		if (net::IsThreadEnable())
		{
			nOptions |= BEV_OPT_THREADSAFE;
		}

		struct bufferevent* pBuffer = bufferevent_socket_new(pNet, id, nOptions);
		if (nullptr == pBuffer)
		{
			std::cerr << "Failed to create bufferevent" << std::endl;
			evutil_closesocket(id);
			return nullptr;
		}

		bufferevent_setcb(pBuffer, readcb, writecb, eventcb, cbarg);
		bufferevent_enable(pBuffer, EV_READ | EV_WRITE);
		if (!RegisterAConnection(id, pBuffer, pAddr))
		{
			bufferevent_free(pBuffer);
			return nullptr;
		}
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
		info.m_pEvent = nullptr;
		return true;
	}

	net::_TyConnectionId CNetPool::CloseAConnection(_TyConnectionId id)
	{
		std::unique_ptr<CNetInfo> pInfo;
		{
			std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
			auto mIter = m_pool.find(id);
			if (mIter == m_pool.end())
			{
				return -1;
			}
			pInfo = std::move(mIter->second);
			m_pool.erase(mIter);
		}

		CloseAConnection(*pInfo);
		return id;
	}

	net::_TyConnectionId CNetPool::CloseAConnection(struct bufferevent* pEvent)
	{
		if (pEvent == nullptr)
		{
			return -1;
		}

		std::unique_ptr<CNetInfo> pInfo;
		_TyConnectionId id = -1;
		{
			std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
			auto mIter = m_pool.begin();
			for (; mIter != m_pool.end(); ++mIter)
			{
				if (mIter->second->m_pEvent == pEvent)
				{
					break;
				}
			}
			if (mIter == m_pool.end())
			{
				return -1;
			}
			id = mIter->first;
			pInfo = std::move(mIter->second);
			m_pool.erase(mIter);
		}

		CloseAConnection(*pInfo);
		return id;
	}

	std::optional<std::vector<char>*> CNetPool::GetRecvBuffer(_TyConnectionId id)
	{
		std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		const auto mIter = m_pool.find(id);
		if (mIter == m_pool.end())
		{
			return std::nullopt;
		}
		return &mIter->second->m_buffer_recv;
	}

	std::optional<std::vector<char>*> CNetPool::GetRecvBuffer(struct bufferevent* pEvent)
	{
		_TyConnectionId id = ;
		return GetRecvBuffer(id);
	}

	std::optional<std::vector<char>*> CNetPool::GetSendBuffer(_TyConnectionId id)
	{
		std::unique_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		const auto mIter = m_pool.find(id);
		if (mIter == m_pool.end())
		{
			return std::nullopt;
		}
		return &mIter->second->m_buffer_send;
	}

	std::optional<std::vector<char>*> CNetPool::GetSendBuffer(struct bufferevent* pEvent)
	{
		_TyConnectionId id = ;
		return GetSendBuffer(id);
	}

	bool CNetPool::SendData2Client(_TyConnectionId id, const char* data, size_t nLength)
	{
		if ((nullptr == data) || (0 == nLength))
		{
			return false;
		}

		std::shared_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		auto mIter = m_pool.find(id);
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

		return evbuffer_add(pBuffer, data, nLength) == 0;
	}

	std::size_t CNetPool::Count() const
	{
		std::shared_lock<std::shared_mutex> lock(m_shared_mtx_pool);
		return m_pool.size();
	}

} // namespace net
