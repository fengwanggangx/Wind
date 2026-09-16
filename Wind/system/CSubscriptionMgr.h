#ifndef WIND_SYSTEM_CSUBSCRIPTIONMGR_H
#define WIND_SYSTEM_CSUBSCRIPTIONMGR_H

#include "../request/MarketTypes.h"
#include "../network/common_net.h"

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CSubscriptionMgr final
{
  public:
	std::vector<CQuoteInfo> Subscribe(net::_TyConnectionId id, const std::vector<CQuoteInfo>& subscriptions);
	std::vector<CQuoteInfo> Unsubscribe(net::_TyConnectionId id, const std::vector<CQuoteInfo>& subscriptions);
	std::vector<CQuoteInfo> RemoveClient(net::_TyConnectionId id);
	void RemoveSubscription(const std::string& strKey);
	std::vector<net::_TyConnectionId> GetSubscriberIds(const std::string& strKey) const;
	std::vector<CQuoteInfo> GetSubscriptions() const;
	std::size_t GetSubscriptionCount(net::_TyConnectionId id) const;
	std::size_t GetSubscriptionCount(const CQuoteInfo& info) const;
	bool IsSubscribed(net::_TyConnectionId id, const CQuoteInfo& info) const;

  private:
	using _TyClientSubscriptions = std::unordered_map<std::string, CQuoteInfo>;
	using _TySubscriberIds = std::unordered_set<net::_TyConnectionId>;

	mutable std::shared_mutex m_mtx_info;
	std::unordered_map<std::string, _TySubscriberIds> m_info_ids;
	std::unordered_map<net::_TyConnectionId, _TyClientSubscriptions> m_id_infos;
};

#endif
