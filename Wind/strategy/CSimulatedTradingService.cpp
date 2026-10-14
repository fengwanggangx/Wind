#include "CSimulatedTradingService.h"

#include <utility>

CSimulatedTradingService::CSimulatedTradingService(std::int64_t initialBalance, std::uint64_t accountId) : m_accountId(accountId)
{
	m_account.m_balance = initialBalance;
	m_account.m_available = m_account.m_balance;
}

COrderSubmitResult CSimulatedTradingService::Submit(const COrderIntent& intent)
{
	if (m_bStopping.load() || !intent.IsValid())
	{
		return { false, false, 0, 0, "Simulated trading service rejected the order" };
	}
	CSimulatedOrder order;
	order.m_intent = intent;
	static std::atomic_uint64_t nextClientOrderId{ 1 };
	order.m_clientOrderId = nextClientOrderId.fetch_add(1);
	static std::atomic_uint64_t nextOrderId{ 1 };
	order.m_orderId = nextOrderId.fetch_add(1);
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_orders.emplace(order.m_orderId, order);
	}
	_TyOrderEventHandler handler;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		handler = m_orderEventHandler;
	}
	if (nullptr != handler)
	{
		COrderEvent event;
		event.m_accountId = m_accountId;
		event.m_strategyId = intent.m_strategyId;
		event.m_signalId = intent.m_signalId;
		event.m_clientOrderId = order.m_clientOrderId;
		event.m_orderId = order.m_orderId;
		event.m_security = intent.m_security;
		event.m_status = OrderStatus::Accepted;
		event.m_quantity = intent.m_quantity;
		handler(event);
	}
	return { true, false, order.m_clientOrderId, order.m_orderId, "Accepted by simulated trading service" };
}

bool CSimulatedTradingService::Cancel(_TyStrategyId strategyId, _TyOrderId orderId)
{
	if (m_bStopping.load())
	{
		return false;
	}
	COrderEvent event;
	_TyOrderEventHandler handler;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		auto iter = m_orders.find(orderId);
		if ((m_orders.end() == iter) || (strategyId != iter->second.m_intent.m_strategyId) || (OrderStatus::Accepted != iter->second.m_status))
		{
			return false;
		}
		iter->second.m_status = OrderStatus::Cancelled;
		event.m_accountId = m_accountId;
		event.m_strategyId = strategyId;
		event.m_signalId = iter->second.m_intent.m_signalId;
		event.m_clientOrderId = iter->second.m_clientOrderId;
		event.m_orderId = orderId;
		event.m_security = iter->second.m_intent.m_security;
		event.m_status = OrderStatus::Cancelled;
		event.m_quantity = iter->second.m_intent.m_quantity;
		handler = m_orderEventHandler;
	}
	if (nullptr != handler)
	{
		handler(event);
	}
	return true;
}

CPositionSnapshot CSimulatedTradingService::GetPosition(const CSecurity& security) const
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	auto iter = m_positions.find(MakePositionKey(security));
	if (m_positions.end() == iter)
	{
		CPositionSnapshot snapshot;
		snapshot.m_security = security;
		return snapshot;
	}
	return iter->second;
}

CAccountSnapshot CSimulatedTradingService::GetAccount() const
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	return m_account;
}

void CSimulatedTradingService::SetOrderEventHandler(_TyOrderEventHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	m_orderEventHandler = std::move(handler);
}

void CSimulatedTradingService::Stop()
{
	m_bStopping.store(true);
	std::lock_guard<std::mutex> lock(m_mtx_state);
	m_orderEventHandler = nullptr;
}

std::string CSimulatedTradingService::MakePositionKey(const CSecurity& security)
{
	return security.String();
}

bool CSimulatedTradingService::HasActiveOrders() const
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	for (const auto& item : m_orders)
	{
		if ((OrderStatus::Accepted == item.second.m_status) || (OrderStatus::PartiallyFilled == item.second.m_status))
		{
			return true;
		}
	}
	return false;
}
