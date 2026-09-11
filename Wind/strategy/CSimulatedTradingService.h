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
	CSimulatedTradingService();

	COrderSubmitResult Submit(const COrderIntent& intent) override;
	bool Cancel(_TyStrategyId strategyId, _TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const market::CSecurity& security) const override;
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

	static std::string MakePositionKey(const market::CSecurity& security);

  private:
	mutable std::mutex m_mtx_state;
	std::unordered_map<_TyOrderId, CSimulatedOrder> m_orders;
	std::unordered_map<std::string, CPositionSnapshot> m_positions;
	CAccountSnapshot m_account;
	_TyOrderEventHandler m_orderEventHandler;
	std::atomic_uint64_t m_nextClientOrderId{ 1 };
	std::atomic_uint64_t m_nextOrderId{ 1 };
	std::atomic_bool m_bStopping{ false };
};

#endif
