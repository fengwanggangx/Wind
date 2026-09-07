#ifndef WIND_HQMARKET_CSESSION_H
#define WIND_HQMARKET_CSESSION_H

#include "MarketTypes.h"
#include "../request/request.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace net
{
	class CTcpClient;
	struct CNetEvent;
}

enum class SessionState
{
	Disconnected,
	Connecting,
	Connected,
	Authenticating,
	Ready,
	Reconnecting,
	Stopping
};

class CSession final
{
	public:
		using ResponseHandler = std::function<void(const CRequest&)>;
		using StateHandler = std::function<void(SessionState, const std::string&)>;

		CSession(std::string strHost, int nPort, std::string strToken);
		~CSession();
		CSession(const CSession&) = delete;
		CSession& operator=(const CSession&) = delete;

		bool Start();
		void Stop();
		bool SendRequest(const CRequest& request);
		bool SubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel);
		bool UnsubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel);
		void RegisterHandler(ResponseHandler&& handler);
		void SetStateHandler(StateHandler&& handler);
		SessionState GetState() const;
		bool IsConnected() const;
		bool IsAuthenticated() const;

	private:
		struct Subscription
		{
			std::string m_strSecurity;
			std::string m_strChannel;
		};

		void ConnectionLoop();
		void MaintenanceLoop();
		int OnNetEvent(const net::CNetEvent& ev);
		void HandleResponse(const CRequest& response);
		bool SendAuthentication();
		bool SendRequestInternal(const CRequest& request, bool bRequireAuthentication);
		void RestoreSubscriptions();
		void NotifyState(SessionState state, const std::string& strMessage);
		void Dispatch(const CRequest& response);
		static std::string MakeSubscriptionKey(const std::string& strSecurity, const std::string& strChannel);

	private:
		std::string m_strHost;
		int m_nPort{ 0 };
		std::string m_strToken;
		std::atomic<SessionState> m_state{ SessionState::Disconnected };
		std::atomic_bool m_bStopping{ false };
		std::thread m_threadConnection;
		std::thread m_threadMaintenance;
		std::mutex m_mtx_wait;
		std::condition_variable m_cv_wait;
		std::mutex m_mtx_client;
		std::unique_ptr<net::CTcpClient> m_pClient;
		std::mutex m_mtx_subscriptions;
		std::unordered_map<std::string, Subscription> m_subscriptions;
		std::mutex m_mtx_handlers;
		std::vector<ResponseHandler> m_handlers;
		StateHandler m_stateHandler;
		int m_nHeartbeatSeconds{ 5 };
		int m_nMaxReconnectSeconds{ 30 };
};

#endif
