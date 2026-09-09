#include "CSession.h"
#include "../system/RequestCenter.h"
#include "../network/CTcpClient.h"
#include "../network/common_net.h"
#include <algorithm>
#include <iostream>
#include <utility>

namespace
{
	bool IsAccepted(const CRequest& response)
	{
		std::string strAccepted = response.GetReturnData("accepted");
		return ("1" == strAccepted) || ("true" == strAccepted) || ("ok" == response.GetReturnData("status"));
	}
}


CSession::CSession(const CLoginInfo& info)
{
	m_auth = info;
}

CSession::~CSession()
{
	Stop();
}

bool CSession::Start()
{
	{
		std::lock_guard<std::mutex> lock(m_mtx_auth);
		bool bCredentialsValid = (m_auth.has_value() && m_auth->Valid()) || (m_login.has_value() && m_login->Valid());
		if (!bCredentialsValid)
		{
			return false;
		}
	}
	if (m_thread_conn.joinable() || m_thread_heartbeat.joinable())
	{
		return true;
	}
	m_bStopping.store(false);
	m_thread_conn = std::thread(&CSession::ConnectionLoop, this);
	m_thread_heartbeat = std::thread(&CSession::MaintenanceLoop, this);
	return true;
}

void CSession::Stop()
{
	if (!m_thread_conn.joinable() && !m_thread_heartbeat.joinable())
	{
		return;
	}
	m_bStopping.store(true);
	NotifyState(SessionState::Stopping, "HQMarket session is stopping");
	m_cv_loops.notify_all();
	{
		std::lock_guard<std::mutex> lock(m_mtx_client);
		if (nullptr != m_client)
		{
			m_client->ShutDown();
		}
	}
	if (m_thread_heartbeat.joinable())
	{
		m_thread_heartbeat.join();
	}
	if (m_thread_conn.joinable())
	{
		m_thread_conn.join();
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_client);
		m_client.reset();
	}
	NotifyState(SessionState::Disconnected, "HQMarket session stopped");
}

bool CSession::SendRequest(const CRequest& request)
{
	CRequest::Type t = request.GetType();
	bool bAuthRequest = (CRequest::Type::QUERY_AUTH == t) || (CRequest::Type::UPDATE_AUTH == t) || ("auth" == request.GetCmd());
	if (!bAuthRequest && !IsAuthenticated())
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mtx_client);
	return (nullptr != m_client) && m_client->SendRequest(request);
}

bool CSession::SubscribeQuote(const market::CQuoteInfo& quote)
{
	if (!quote.IsValid())
	{
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		m_subscriptions.insert_or_assign(MakeSubscriptionKey(quote), Subscription{ quote });
	}
	bool bSent = SendRequest(request::Subscription(quote));
	if (!bSent)
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		m_subscriptions.erase(MakeSubscriptionKey(quote));
	}
	return bSent;
}

bool CSession::UnsubscribeQuote(const market::CQuoteInfo& quote)
{
	if (!quote.IsValid())
	{
		return false;
	}
	std::string strKey = MakeSubscriptionKey(quote);
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		if (m_subscriptions.end() == m_subscriptions.find(strKey))
		{
			return false;
		}
	}
	bool bSent = SendRequest(request::UnSubscription(quote));
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		m_subscriptions.erase(strKey);
	}
	return bSent;
}

void CSession::RegisterHandler(ResponseHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_handlers);
	m_handlers.emplace_back(std::move(handler));
}

void CSession::SetStateHandler(StateHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_handlers);
	m_stateHandler = std::move(handler);
}

SessionState CSession::GetState() const
{
	return m_state.load();
}

bool CSession::IsConnected() const
{
	SessionState state = m_state.load();
	return (SessionState::Connected == state) || (SessionState::Authenticating == state) || (SessionState::Ready == state);
}

bool CSession::IsAuthenticated() const
{
	return SessionState::Ready == m_state.load();
}

void CSession::ConnectionLoop()
{
	int nReConnectSeconds = 1;
	while (!m_bStopping.load())
	{
		NotifyState(1 == nReConnectSeconds ? SessionState::Connecting : SessionState::Reconnecting, 1 == nReConnectSeconds ? "Connecting to HQMarket" : "Reconnecting to HQMarket in " + std::to_string(nReConnectSeconds) + " seconds");
		std::optional<CLoginInfo> info;
		{
			std::lock_guard<std::mutex> lock(m_mtx_auth);
			info = m_auth.has_value() ? m_auth : m_login;
		}
		if (!info.has_value() || !info->Valid())
		{
			NotifyState(SessionState::Disconnected, "HQMarket login information is invalid");
			break;
		}
		decltype(m_client) pClient = std::make_unique<net::CTcpClient>(info->m_host.m_strHost, static_cast<int>(info->m_host.m_nPort));
		pClient->RegisterHandler(std::bind_front(&CSession::OnNetEvent, this));
		int nRet = pClient->Initialize();
		if (0 == nRet)
		{
			{
				std::lock_guard<std::mutex> lock(m_mtx_client);
				m_client = std::move(pClient);
			}
			m_client->Start(true);
		}
		else
		{
			std::cerr << "HQMarket connection initialization failed: " << nRet << '\n';
		}
		bool bAuthed = IsAuthenticated();
		{
			std::lock_guard<std::mutex> lock(m_mtx_client);
			m_client.reset();
		}
		if (m_bStopping.load())
		{
			break;
		}
		NotifyState(SessionState::Disconnected, "HQMarket connection closed");
		std::unique_lock<std::mutex> lock(m_mtx_loops);
		m_cv_loops.wait_for(lock, std::chrono::seconds(nReConnectSeconds), [this]()
		{
			return m_bStopping.load();
		});
		nReConnectSeconds = bAuthed ? 1 : (std::min)(m_nMaxReconnectSeconds, nReConnectSeconds * 2);
	}
}

void CSession::MaintenanceLoop()
{
	std::chrono::steady_clock::time_point nTmNextHearbeat = std::chrono::steady_clock::now();
	while (!m_bStopping.load())
	{
		std::unique_lock<std::mutex> lock(m_mtx_loops);
		m_cv_loops.wait_for(lock, std::chrono::milliseconds(250), [this]()
		{
			return m_bStopping.load();
		});
		lock.unlock();
		if (m_bStopping.load())
		{
			break;
		}
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if (IsAuthenticated() && (nTmNextHearbeat <= now))
		{
			SendRequest(request::HeartBeat());
			nTmNextHearbeat = now + std::chrono::seconds(m_nHeartbeatSeconds);
		}
	}
}

int CSession::OnNetEvent(const net::CNetEvent& ev)
{
	if (net::em_event::connected == ev.m_event)
	{
		NotifyState(SessionState::Connected, "Connected to HQMarket");
		SendAuthentication();
		return 1;
	}
	if ((net::em_event::request == ev.m_event) && (nullptr != ev.m_request))
	{
		HandleResponse(*ev.m_request);
		return 1;
	}
	if ((net::em_event::disconnected == ev.m_event) || (net::em_event::error == ev.m_event) || (net::em_event::timeout == ev.m_event))
	{
		NotifyState(SessionState::Disconnected, "HQMarket connection lost");
		return 1;
	}
	return 0;
}

void CSession::HandleResponse(const CRequest& req)
{
	if ("auth" == req.GetCmd())
	{
		if (!IsAccepted(req))
		{
			NotifyState(SessionState::Connected, "HQMarket authentication failed: " + req.GetReturnData("reason"));
			return;
		}
		{
			std::lock_guard<std::mutex> lock(m_mtx_auth);
			if (m_auth.has_value())
			{
				m_login = std::move(m_auth);
				m_auth.reset();
			}
		}
		NotifyState(SessionState::Ready, "HQMarket session ready");
		RestoreSubscriptions();
		return;
	}
	Dispatch(req);
}

bool CSession::SendAuthentication()
{
	std::optional<CLoginInfo> info;
	{
		std::lock_guard<std::mutex> lock(m_mtx_auth);
		info = m_auth.has_value() ? m_auth : m_login;
	}
	if (!info.has_value() || !info->Valid())
	{
		NotifyState(SessionState::Connected, "HQMarket credentials are invalid");
		return false;
	}
	NotifyState(SessionState::Authenticating, "Authenticating with HQMarket");
	return SendRequest(request::Auth(info->m_strToken, info->m_strPassword));
}

void CSession::RestoreSubscriptions()
{
	std::vector<Subscription> subscriptions;
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		subscriptions.reserve(m_subscriptions.size());
		for (const auto& [strKey, subscription] : m_subscriptions)
		{
			subscriptions.emplace_back(subscription);
		}
	}
	for (const auto& subscription : subscriptions)
	{
		SendRequest(request::Subscription(subscription.m_quote));
	}
}

void CSession::NotifyState(SessionState state, const std::string& strMessage)
{
	m_state.store(state);
	StateHandler handler;
	{
		std::lock_guard<std::mutex> lock(m_mtx_handlers);
		handler = m_stateHandler;
	}
	if (nullptr != handler)
	{
		handler(state, strMessage);
	}
}

void CSession::Dispatch(const CRequest& req)
{
	std::vector<ResponseHandler> handlers;
	{
		std::lock_guard<std::mutex> lock(m_mtx_handlers);
		handlers = m_handlers;
	}
	for (const auto& handler : handlers)
	{
		if (nullptr != handler)
		{
			handler(req);
		}
	}
}

std::string CSession::MakeSubscriptionKey(const market::CQuoteInfo& quote)
{
	return quote.m_security.String() + ':' + market::GetChannelString(quote.m_channel);
}
