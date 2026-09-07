#include "CSession.h"

#include "CHQRequest.h"
#include "../network/CTcpClient.h"
#include "../network/common_net.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

namespace
{
	std::int64_t NowMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	bool IsAccepted(const CRequest& response)
	{
		std::string strAccepted = response.GetReturnData("accepted");
		return ("1" == strAccepted) || ("true" == strAccepted);
	}
}

CSession::CSession(std::string strHost, int nPort, std::string strToken) : m_strHost(std::move(strHost)), m_nPort(nPort), m_strToken(std::move(strToken))
{
}

CSession::~CSession()
{
	Stop();
}

bool CSession::Start()
{
	if (m_strHost.empty() || (0 >= m_nPort) || (65535 < m_nPort) || m_strToken.empty())
	{
		return false;
	}
	if (m_threadConnection.joinable() || m_threadMaintenance.joinable())
	{
		return true;
	}
	m_bStopping.store(false);
	m_threadConnection = std::thread(&CSession::ConnectionLoop, this);
	m_threadMaintenance = std::thread(&CSession::MaintenanceLoop, this);
	return true;
}

void CSession::Stop()
{
	if (!m_threadConnection.joinable() && !m_threadMaintenance.joinable())
	{
		return;
	}
	m_bStopping.store(true);
	NotifyState(SessionState::Stopping, "HQMarket session is stopping");
	m_cv_wait.notify_all();
	{
		std::lock_guard<std::mutex> lock(m_mtx_client);
		if (nullptr != m_pClient)
		{
			m_pClient->ShutDown();
		}
	}
	if (m_threadMaintenance.joinable())
	{
		m_threadMaintenance.join();
	}
	if (m_threadConnection.joinable())
	{
		m_threadConnection.join();
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_client);
		m_pClient.reset();
	}
	NotifyState(SessionState::Disconnected, "HQMarket session stopped");
}

bool CSession::SendRequest(const CRequest& request)
{
	return SendRequestInternal(request, true);
}

bool CSession::SubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel)
{
	std::string strSecurity = FmtSecurityString(strCode, mk);
	std::string strChannel = market::GetChannelString(channel);
	if (strSecurity.empty() || strChannel.empty())
	{
		return false;
	}
	std::unique_ptr<CRequest> request(CHQRequest::GetSubscribeRequest(strSecurity, strChannel, true));
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		m_subscriptions.insert_or_assign(MakeSubscriptionKey(strSecurity, strChannel), Subscription{ strSecurity, strChannel });
	}
	return !IsAuthenticated() || SendRequest(*request);
}

bool CSession::UnsubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel)
{
	std::string strSecurity = FmtSecurityString(strCode, mk);
	std::string strChannel = market::GetChannelString(channel);
	std::string strKey = MakeSubscriptionKey(strSecurity, strChannel);
	{
		std::lock_guard<std::mutex> lock(m_mtx_subscriptions);
		if (m_subscriptions.end() == m_subscriptions.find(strKey))
		{
			return false;
		}
	}
	std::unique_ptr<CRequest> request(CHQRequest::GetSubscribeRequest(strSecurity, strChannel, false));
	bool bSent = !IsAuthenticated() || SendRequest(*request);
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
	int nReconnectSeconds = 1;
	while (!m_bStopping.load())
	{
		NotifyState(1 == nReconnectSeconds ? SessionState::Connecting : SessionState::Reconnecting,
			1 == nReconnectSeconds ? "Connecting to HQMarket" : "Reconnecting to HQMarket in " + std::to_string(nReconnectSeconds) + " seconds");
		std::unique_ptr<net::CTcpClient> pClient = std::make_unique<net::CTcpClient>(m_strHost, m_nPort);
		pClient->RegisterHandler([this](const net::CNetEvent& ev)
		{
			return OnNetEvent(ev);
		});
		int nResult = pClient->Initialize();
		if (0 == nResult)
		{
			{
				std::lock_guard<std::mutex> lock(m_mtx_client);
				m_pClient = std::move(pClient);
			}
			m_pClient->Start(true);
		}
		else
		{
			std::cerr << "HQMarket connection initialization failed: " << nResult << '\n';
		}
		bool bAuthenticated = IsAuthenticated();
		{
			std::lock_guard<std::mutex> lock(m_mtx_client);
			m_pClient.reset();
		}
		if (m_bStopping.load())
		{
			break;
		}
		NotifyState(SessionState::Disconnected, "HQMarket connection closed");
		std::unique_lock<std::mutex> lock(m_mtx_wait);
		m_cv_wait.wait_for(lock, std::chrono::seconds(nReconnectSeconds), [this]()
		{
			return m_bStopping.load();
		});
		nReconnectSeconds = bAuthenticated ? 1 : (std::min)(m_nMaxReconnectSeconds, nReconnectSeconds * 2);
	}
}

void CSession::MaintenanceLoop()
{
	std::chrono::steady_clock::time_point nextHeartbeat = std::chrono::steady_clock::now();
	while (!m_bStopping.load())
	{
		std::unique_lock<std::mutex> lock(m_mtx_wait);
		m_cv_wait.wait_for(lock, std::chrono::milliseconds(250), [this]()
		{
			return m_bStopping.load();
		});
		lock.unlock();
		if (m_bStopping.load())
		{
			break;
		}
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if (IsAuthenticated() && (nextHeartbeat <= now))
		{
			std::unique_ptr<CRequest> request(CHQRequest::Heartbeat(NowMilliseconds()));
			SendRequest(*request);
			nextHeartbeat = now + std::chrono::seconds(m_nHeartbeatSeconds);
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

void CSession::HandleResponse(const CRequest& response)
{
	if ("auth" == response.GetCmd())
	{
		if (!IsAccepted(response))
		{
			NotifyState(SessionState::Connected, "HQMarket authentication failed: " + response.GetReturnData("reason"));
			return;
		}
		NotifyState(SessionState::Ready, "HQMarket session ready");
		RestoreSubscriptions();
	}
	Dispatch(response);
}

bool CSession::SendAuthentication()
{
	CRequest request;
	request.SetType(CRequest::Type::HQMARKET);
	request.SetCmd("auth");
	request.SetExtraData("token", m_strToken);
	NotifyState(SessionState::Authenticating, "Authenticating with HQMarket");
	return SendRequestInternal(request, false);
}

bool CSession::SendRequestInternal(const CRequest& request, bool bRequireAuthentication)
{
	if (bRequireAuthentication && !IsAuthenticated())
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mtx_client);
	return (nullptr != m_pClient) && m_pClient->SendRequest(request);
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
		std::unique_ptr<CRequest> request(CHQRequest::GetSubscribeRequest(subscription.m_strSecurity, subscription.m_strChannel, true));
		SendRequest(*request);
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

void CSession::Dispatch(const CRequest& response)
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
			handler(response);
		}
	}
}

std::string CSession::MakeSubscriptionKey(const std::string& strSecurity, const std::string& strChannel)
{
	return strSecurity + ':' + strChannel;
}
