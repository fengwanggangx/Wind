#include "CTcpClient.h"
#include "CNetTools.h"
#include "../basic/CDistributor.h"
#include "../request/request.h"
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <utility>
#include <vector>
#include "CNetPool.h"

namespace net
{
	CTcpClient::CTcpClient(const std::string& strAddr, int nPort) : m_dispatcher(std::make_unique<_TyDistributor>()), m_strAddr(strAddr), m_nPort(nPort)
	{
		m_buffer_recv.reserve(4096);
		m_buffer_send.reserve(4096);
	}

	CTcpClient::~CTcpClient()
	{
		Release();
	}

	void CTcpClient::CBufferEventDeleter::operator()(bufferevent* pEvent) const
	{
		if (nullptr != pEvent)
		{
			bufferevent_free(pEvent);
		}
	}

	int CTcpClient::Initialize()
	{
		if ((nullptr == GetNet()) || m_strAddr.empty() || (0 >= m_nPort) || (65535 < m_nPort))
		{
			return -1;
		}

		int nOptions = net::IsThreadEnable() ? (BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE) : BEV_OPT_CLOSE_ON_FREE;
		if (nullptr != m_pEvent)
		{
			return 0;
		}

		decltype(m_pEvent) pEvent;
		pEvent.reset(bufferevent_socket_new(GetNet(), -1, nOptions));
		if (nullptr == pEvent)
		{
			return -2;
		}

		sockaddr_in svr{};
		if (!net::FmtAddress(svr, m_nPort, m_strAddr))
		{
			return -3;
		}
		bufferevent_setcb(pEvent.get(), CTcpClient::Read_Callback, nullptr, CTcpClient::Event_Callback, this);
		if (0 != bufferevent_enable(pEvent.get(), EV_READ | EV_WRITE))
		{
			return -4;
		}
		if (0 != bufferevent_socket_connect(pEvent.get(), reinterpret_cast<sockaddr*>(&svr), sizeof(svr)))
		{
			return -5;
		}
		m_pEvent = std::move(pEvent);
		return 0;
	}

	void CTcpClient::Release()
	{
		ShutDown();
		m_pEvent.reset();
		m_bConnected = false;
	}

	bool CTcpClient::Send(const void* pData, std::size_t nLength)
	{
		if ((nullptr == pData) || (0 == nLength) || !m_bConnected)
		{
			return false;
		}
		return (nullptr != m_pEvent) && (0 == bufferevent_write(m_pEvent.get(), pData, nLength));
	}

	bool CTcpClient::SendRequest(CRequest* pRequest)
	{
		if ((nullptr == pRequest) || !m_bConnected || (nullptr == m_pEvent))
		{
			return false;
		}
		return net::utility::SendRequest(pRequest, m_pEvent.get(), m_buffer_send);
	}

	void CTcpClient::RegisterHandler(_TyHandler&& handler)
	{
		if (nullptr != m_dispatcher)
		{
			m_dispatcher->RegisterHandler(std::move(handler));
		}
	}

	void CTcpClient::ClearHandlers()
	{
		if (nullptr != m_dispatcher)
		{
			m_dispatcher->ClearHandlers();
		}
	}

	bool CTcpClient::IsConnected() const
	{
		return m_bConnected;
	}

	std::size_t CTcpClient::OnRead(bufferevent* pEvent)
	{
		_TyConnectionId id = bufferevent_getfd(pEvent);
		auto ret = CNetPool::InstancePtr()->GetRecvBuffer(id);
		if (!ret.has_value())
		{
			return -1;
		}
		std::vector<std::unique_ptr<CRequest>> reqs;
		std::size_t sz = net::utility::RequestFromBuffer(reqs, pEvent, *ret.value());
		if (m_dispatcher != nullptr)
		{
			std::vector<CNetEvent> events;
			events.reserve(reqs.size());
			for (auto& v : reqs)
			{
				events.emplace_back(em_event::request);
				auto& ev = events.back();
				ev.m_request = std::move(v);
			}
			m_dispatcher->Dispatch(std::move(events));
		}
		return sz;
	}

	void CTcpClient::OnConnected(bufferevent*)
	{
		m_bConnected = true;
		if (nullptr != m_dispatcher)
		{
			std::vector<CNetEvent> events;
			events.emplace_back(em_event::connected);
			m_dispatcher->Dispatch(std::move(events));
		}
	}

	void CTcpClient::OnEvent(bufferevent* pEvent, short events)
	{
		if (pEvent == nullptr)
		{
			return;
		}
		if (0 == (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)))
		{
			return;
		}

		_TyConnectionId id = CNetPool::InstancePtr()->CloseAConnection(pEvent);
		if ((id >= 0) && (nullptr != m_dispatcher))
		{
			std::vector<CNetEvent> events;
			events.emplace_back(em_event::disconnected, id);
			m_dispatcher->Dispatch(std::move(events));
		}
	}
} // namespace net
