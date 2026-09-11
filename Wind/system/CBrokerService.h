#ifndef WIND_SYSTEM_CBROKERSERVICE_H
#define WIND_SYSTEM_CBROKERSERVICE_H

#include "CSubscriptionMgr.h"
#include "../request/MarketTypes.h"
#include "../network/common_net.h"
#include "../request/request.h"

#include <functional>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CSession;
class CStrategyEngine;
enum class SessionState;

namespace net
{
	class CTcpServer;
}

class CBrokerService final
{
  private:
	struct PendingSubscription
	{
		net::_TyConnectionId m_id{ -1 };
		_TyRequestId m_requestId{ 0 };
		market::CQuoteInfo m_quote;
		std::chrono::steady_clock::time_point m_deadline;
	};

  public:
	CBrokerService(net::CTcpServer* pTcpServer, CSession* pSession, CStrategyEngine* pStrategyEngine);
	CBrokerService(const CBrokerService&) = delete;
	CBrokerService& operator=(const CBrokerService&) = delete;

	bool Initialize();

  private:
	int OnNetEvent(const net::CNetEvent& ev);
	void OnClientRequest(net::_TyConnectionId id, const CRequest& request);
	bool HandleAuth(net::_TyConnectionId id, const CRequest& req);
	bool HandleRegisterAuth(net::_TyConnectionId id, const CRequest& req);
	bool HandleSubscription(net::_TyConnectionId id, const CRequest& req);
	bool HandleHeartbeat(net::_TyConnectionId id, const CRequest& req);
	bool HandleStrategy(net::_TyConnectionId id, const CRequest& req);

	int HandleDisconnected(net::_TyConnectionId id);
	bool HandleReAuth(const CRequest& req);
	void OnHQMarketResponse(const CRequest& req);
	void OnHQMarketState(SessionState state, const std::string& strMessage);
	void ExpirePendingSubscriptions();
	void FailPendingSubscriptions(const std::string& strReason);

private:
	bool IsAuthenticated(net::_TyConnectionId id) const;
	market::CQuoteInfo GetQuoteInfo(const CRequest& req) const;
	void SendSubscriptionResponse(net::_TyConnectionId id, _TyRequestId requestId, bool bAccepted, const std::string& strReason) const;
	std::string GetMarketResponseKey(const CRequest& req) const;
	void SendResponse(const CRequest& req, int nErrorCode, const std::string& strMessage) const;
	bool Login(const CRequest& req, std::string& strToken);

  private:
	std::unordered_map<std::string, std::function<bool(net::_TyConnectionId, const CRequest&)>> m_handler;

	mutable std::mutex m_mtx_state;
	std::unordered_set<net::_TyConnectionId> m_auth_clients;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_client_tokens;

	CSubscriptionMgr m_subscriptions;
	std::unordered_map<std::string, std::vector<PendingSubscription>> m_pendingSubscriptions;

private:
	net::CTcpServer* m_pTcpServer{ nullptr };
	CSession* m_pSession{ nullptr };
	CStrategyEngine* m_pStrategyEngine{ nullptr };
};

#endif
