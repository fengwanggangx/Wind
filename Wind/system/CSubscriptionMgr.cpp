#include "CSubscriptionMgr.h"

#include <mutex>

std::vector<CQuoteInfo> CSubscriptionMgr::Subscribe(net::_TyConnectionId id, const std::vector<CQuoteInfo>& subscriptions)
{
	std::vector<CQuoteInfo> added;
	added.reserve(subscriptions.size());
	std::lock_guard<std::shared_mutex> lock(m_mtx_info);
	_TyClientSubscriptions& infos = m_id_infos[id];
	for (const auto& info : subscriptions)
	{
		std::string strKey = info.String();
		if (!infos.emplace(strKey, info).second)
		{
			continue;
		}
		_TySubscriberIds& ids = m_info_ids[strKey];
		ids.emplace(id);
		if (1 == ids.size())
		{
			added.emplace_back(info);
		}
	}
	return added;
}

std::vector<CQuoteInfo> CSubscriptionMgr::Unsubscribe(net::_TyConnectionId id, const std::vector<CQuoteInfo>& subscriptions)
{
	std::vector<CQuoteInfo> removed;
	removed.reserve(subscriptions.size());
	std::lock_guard<std::shared_mutex> lock(m_mtx_info);
	auto clientIter = m_id_infos.find(id);
	if (m_id_infos.end() == clientIter)
	{
		return removed;
	}
	for (const auto& info : subscriptions)
	{
		std::string strKey = info.String();
		auto infoIter = clientIter->second.find(strKey);
		if (clientIter->second.end() == infoIter)
		{
			continue;
		}
		auto idsIter = m_info_ids.find(strKey);
		if (m_info_ids.end() != idsIter)
		{
			idsIter->second.erase(id);
			if (idsIter->second.empty())
			{
				removed.emplace_back(infoIter->second);
				m_info_ids.erase(idsIter);
			}
		}
		clientIter->second.erase(infoIter);
	}
	if (clientIter->second.empty())
	{
		m_id_infos.erase(clientIter);
	}
	return removed;
}

std::vector<CQuoteInfo> CSubscriptionMgr::RemoveClient(net::_TyConnectionId id)
{
	std::vector<CQuoteInfo> removed;
	std::lock_guard<std::shared_mutex> lock(m_mtx_info);
	auto clientIter = m_id_infos.find(id);
	if (m_id_infos.end() == clientIter)
	{
		return removed;
	}
	removed.reserve(clientIter->second.size());
	for (const auto& info : clientIter->second)
	{
		auto idsIter = m_info_ids.find(info.first);
		if (m_info_ids.end() == idsIter)
		{
			continue;
		}
		idsIter->second.erase(id);
		if (idsIter->second.empty())
		{
			removed.emplace_back(info.second);
			m_info_ids.erase(idsIter);
		}
	}
	m_id_infos.erase(clientIter);
	return removed;
}

void CSubscriptionMgr::RemoveSubscription(const std::string& strKey)
{
	std::lock_guard<std::shared_mutex> lock(m_mtx_info);
	auto idsIter = m_info_ids.find(strKey);
	if (m_info_ids.end() == idsIter)
	{
		return;
	}
	for (net::_TyConnectionId id : idsIter->second)
	{
		auto clientIter = m_id_infos.find(id);
		if (m_id_infos.end() == clientIter)
		{
			continue;
		}
		clientIter->second.erase(strKey);
		if (clientIter->second.empty())
		{
			m_id_infos.erase(clientIter);
		}
	}
	m_info_ids.erase(idsIter);
}

std::vector<net::_TyConnectionId> CSubscriptionMgr::GetSubscriberIds(const std::string& strKey) const
{
	std::vector<net::_TyConnectionId> ids;
	std::shared_lock<std::shared_mutex> lock(m_mtx_info);
	auto idsIter = m_info_ids.find(strKey);
	if (m_info_ids.end() != idsIter)
	{
		ids.assign(idsIter->second.begin(), idsIter->second.end());
	}
	return ids;
}

std::vector<CQuoteInfo> CSubscriptionMgr::GetSubscriptions() const
{
	std::vector<CQuoteInfo> subscriptions;
	std::shared_lock<std::shared_mutex> lock(m_mtx_info);
	subscriptions.reserve(m_info_ids.size());
	for (const auto& item : m_info_ids)
	{
		for (net::_TyConnectionId id : item.second)
		{
			const auto clientIter = m_id_infos.find(id);
			if (m_id_infos.end() == clientIter)
			{
				continue;
			}
			const auto infoIter = clientIter->second.find(item.first);
			if (clientIter->second.end() != infoIter)
			{
				subscriptions.emplace_back(infoIter->second);
			}
			break;
		}
	}
	return subscriptions;
}

std::size_t CSubscriptionMgr::GetSubscriptionCount(net::_TyConnectionId id) const
{
	std::shared_lock<std::shared_mutex> lock(m_mtx_info);
	auto clientIter = m_id_infos.find(id);
	return m_id_infos.end() == clientIter ? 0 : clientIter->second.size();
}

std::size_t CSubscriptionMgr::GetSubscriptionCount(const CQuoteInfo& info) const
{
	std::string strKey = info.String();
	if (strKey.empty())
	{
		return 0;
	}
	std::shared_lock<std::shared_mutex> lock(m_mtx_info);
	auto idsIter = m_info_ids.find(strKey);
	return m_info_ids.end() == idsIter ? 0 : idsIter->second.size();
}

bool CSubscriptionMgr::IsSubscribed(net::_TyConnectionId id, const CQuoteInfo& info) const
{
	std::shared_lock<std::shared_mutex> lock(m_mtx_info);
	auto clientIter = m_id_infos.find(id);
	return (m_id_infos.end() != clientIter) && clientIter->second.contains(info.String());
}
