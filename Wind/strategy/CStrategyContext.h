#ifndef WIND_STRATEGY_CSTRATEGYCONTEXT_H
#define WIND_STRATEGY_CSTRATEGYCONTEXT_H

#include "IStrategyContext.h"

#include <atomic>

class CStrategyEngine;
class CTradeService;

class CStrategyContext final : public IStrategyContext
{
	public:
	CStrategyContext(_TyStrategyId strategyId, std::uint64_t accountId, CStrategyEngine* pEngine, CTradeService* pSnapshotProvider);

	COrderSubmitResult SubmitOrder(const COrderIntent& intent) override;
	bool CancelOrder(_TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const CSecurity& security) const override;
	CAccountSnapshot GetAccount() const override;
	void WriteLog(LogLevel level, const std::string& strMessage) override;
	bool IsMarketAvailable() const override;

	void Disable();

	private:
	_TyStrategyId m_strategyId{ 0 };
	CStrategyEngine* m_pEngine{ nullptr };
	std::uint64_t m_accountId{ 1 };
	CTradeService* m_pSnapshotProvider{ nullptr };
	std::atomic_bool m_bEnabled{ true };
};

#endif
