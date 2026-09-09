#ifndef WIND_SYSTEM_CBROKERSERVICE_H
#define WIND_SYSTEM_CBROKERSERVICE_H

#include "CSubscriptionMgr.h"
#include "../hqmarket/MarketTypes.h"
#include "../network/common_net.h"
#include "../request/request.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CSession;

namespace net
{
	class CTcpServer;
}

class CBrokerService final
{
  private:
	struct PendingSubscription
	{
		net::_TyConnectionId m_id{-1};
		_TyRequestId m_requestId{0};
	};

  public:
	CBrokerService(net::CTcpServer* pTcpServer, CSession* pSession);
	CBrokerService(const CBrokerService&) = delete;
	CBrokerService& operator=(const CBrokerService&) = delete;

	bool Initialize();

  private:
	int OnNetEvent(const net::CNetEvent& ev);
	bool HandleAuth(net::_TyConnectionId id, const CRequest& req);
	bool HandleRegisterAuth(net::_TyConnectionId id, const CRequest& req);
	bool HandleSubscription(net::_TyConnectionId id, const CRequest& req);

	int HandleDisconnected(net::_TyConnectionId id);
	bool HandleReAuth(const CRequest& req);
	void OnHQMarketResponse(const CRequest& req);

private:

	market::CQuoteInfo GetQuoteInfo(const CRequest& req) const;
	void SendSubscriptionResponse(net::_TyConnectionId id, _TyRequestId requestId, bool bAccepted, const std::string& strReason) const;
	std::string GetMarketResponseKey(const CRequest& req) const;
	void SendResponse(const CRequest& req, int nErrorCode, const std::string& strMessage) const;
	bool Login(const CRequest& req, std::string& strToken);

  private:
	std::unordered_map<std::string, std::function<bool(net::_TyConnectionId, CRequest&)>> m_handler;
	std::mutex m_mtx_state;
	std::unordered_set<net::_TyConnectionId> m_authenticatedClients;
	std::unordered_set<std::string> m_loginTokens;
	CSubscriptionMgr m_subscriptions;
	std::unordered_map<std::string, std::vector<PendingSubscription>> m_pendingSubscriptions;

private:
	net::CTcpServer* m_pTcpServer{ nullptr};
	CSession* m_pSession{ nullptr };
};

#endif
