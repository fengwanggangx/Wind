#include "StrategyTypes.h"

#include <unordered_set>

bool CStrategyConfig::IsValid() const
{
	if ((0 == m_id) || m_strType.empty() || m_strName.empty() || (0 == m_eventQueueLimit) || m_subscriptions.empty())
	{
		return false;
	}
	std::unordered_set<std::string> keys;
	keys.reserve(m_subscriptions.size());
	for (const auto& subscription : m_subscriptions)
	{
		if (!subscription.IsValid() || !keys.emplace(subscription.String()).second)
		{
			return false;
		}
	}
	return true;
}

bool COrderIntent::IsValid() const
{
	if ((0 == m_strategyId) || (0 == m_signalId) || !m_security.IsValid() || (OrderSide::Unknown == m_side) || (PositionEffect::Unknown == m_positionEffect))
	{
		return false;
	}
	if ((0 >= m_quantity) || ((OrderType::Limit == m_orderType) && (0 >= m_price)))
	{
		return false;
	}
	return true;
}
