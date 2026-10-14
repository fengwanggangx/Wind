#ifndef WIND_STRATEGY_CSIMULATEDTRADINGSERVICE_H
#define WIND_STRATEGY_CSIMULATEDTRADINGSERVICE_H

#include "ITradeService.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

class CSimulatedTradingService final : public ITradeService
{
	public:
	explicit CSimulatedTradingService(std::int64_t initialBalance = 1000000000, std::uint64_t accountId = 1);
	bool HasActiveOrders() const;

	COrderSubmitResult Submit(const COrderIntent& intent) override;
	bool Cancel(_TyStrategyId strategyId, _TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const CSecurity& security) const override;
	CAccountSnapshot GetAccount() const override;
	void SetOrderEventHandler(_TyOrderEventHandler&& handler) override;
	void Stop() override;

	private:
	struct CSimulatedOrder
	{
		COrderIntent m_intent;
		OrderStatus m_status{ OrderStatus::Accepted };
		_TyClientOrderId m_clientOrderId{ 0 };
		_TyOrderId m_orderId{ 0 };
	};

	static std::string MakePositionKey(const CSecurity& security);

	private:
	mutable std::mutex m_mtx_state;
	std::unordered_map<_TyOrderId, CSimulatedOrder> m_orders;
	std::unordered_map<std::string, CPositionSnapshot> m_positions;
	CAccountSnapshot m_account;
	_TyOrderEventHandler m_orderEventHandler;
	std::uint64_t m_accountId{ 1 };
	std::atomic_bool m_bStopping{ false };
};

#endif
