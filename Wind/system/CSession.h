#ifndef WIND_HQMARKET_CSESSION_H
#define WIND_HQMARKET_CSESSION_H

#include "../request/MarketTypes.h"
#include "../system/CHostMgr.h"
#include "../request/request.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>


struct CLoginInfo
{
	std::string m_strAccount;
	std::string m_strPassword;
	std::string m_strToken;
	CHostInfo m_host;

	bool Valid() const noexcept
	{
		return m_host.Valid() && (!m_strToken.empty() || (!m_strAccount.empty() && !m_strPassword.empty()));
	}
};

namespace net
{
	class CTcpClient;
	struct CNetEvent;
}

enum class SessionState
{
	Disconnected, Connecting, Connected, Authenticating, Ready, Reconnecting, Stopping
};

class CSession final
{
public:
	using ResponseHandler = std::function<void(const CRequest&)>;
	using StateHandler = std::function<void(SessionState, const std::string&)>;

	explicit CSession(const CLoginInfo& info);
	~CSession();
	CSession(const CSession&) = delete;
	CSession& operator=(const CSession&) = delete;

	bool Start();
	void Stop();
	bool SendRequest(const CRequest& request);
	bool SubscribeQuote(const market::CQuoteInfo& quote);
	bool UnsubscribeQuote(const market::CQuoteInfo& quote);
	void RegisterHandler(ResponseHandler&& handler);
	void SetStateHandler(StateHandler&& handler);
	SessionState GetState() const;
	bool IsConnected() const;
	bool IsAuthenticated() const;

private:
	struct Subscription
	{
		market::CQuoteInfo m_quote;
	};

	void ConnectionLoop();
	void MaintenanceLoop();
	int OnNetEvent(const net::CNetEvent& ev);
	void HandleResponse(const CRequest& req);
	bool SendAuthentication();
	void RestoreSubscriptions();
	void NotifyState(SessionState state, const std::string& strMessage);
	void Dispatch(const CRequest& req);
	static std::string MakeSubscriptionKey(const market::CQuoteInfo& quote);

private:
	std::mutex m_mtx_client;
	std::unique_ptr<net::CTcpClient> m_client;

private:
	std::atomic_bool m_bStopping{ false };
	std::atomic<SessionState> m_state{ SessionState::Disconnected };

	std::thread m_thread_conn;
	std::thread m_thread_heartbeat;

	std::mutex m_mtx_loops;
	std::condition_variable m_cv_loops;

	mutable std::mutex m_mtx_auth;
	std::optional<CLoginInfo> m_auth;
	std::optional<CLoginInfo> m_login;

	std::mutex m_mtx_subscriptions;
	std::unordered_map<std::string, Subscription> m_subscriptions;
	std::mutex m_mtx_handlers;
	std::vector<ResponseHandler> m_handlers;

	StateHandler m_stateHandler;
	int m_nHeartbeatSeconds{ 5 };
	int m_nMaxReconnectSeconds{ 30 };
};

#endif
