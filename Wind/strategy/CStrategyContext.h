#ifndef WIND_STRATEGY_CSTRATEGYCONTEXT_H
#define WIND_STRATEGY_CSTRATEGYCONTEXT_H

#include "IStrategyContext.h"

#include <atomic>

class CStrategyEngine;
class ITradingSnapshotProvider;

class CStrategyContext final : public IStrategyContext
{
  public:
	CStrategyContext(_TyStrategyId strategyId, CStrategyEngine* pEngine, ITradingSnapshotProvider* pSnapshotProvider);

	COrderSubmitResult SubmitOrder(const COrderIntent& intent) override;
	bool CancelOrder(_TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const market::CSecurity& security) const override;
	CAccountSnapshot GetAccount() const override;
	void WriteLog(LogLevel level, const std::string& strMessage) override;
	bool IsMarketAvailable() const override;

	void Disable();

  private:
	_TyStrategyId m_strategyId{ 0 };
	CStrategyEngine* m_pEngine{ nullptr };
	ITradingSnapshotProvider* m_pSnapshotProvider{ nullptr };
	std::atomic_bool m_bEnabled{ true };
};

#endif
