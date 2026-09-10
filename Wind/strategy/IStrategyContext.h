#ifndef WIND_STRATEGY_ISTRATEGYCONTEXT_H
#define WIND_STRATEGY_ISTRATEGYCONTEXT_H

#include "StrategyTypes.h"

class IStrategyContext
{
  public:
	virtual ~IStrategyContext() = default;

	virtual COrderSubmitResult SubmitOrder(const COrderIntent& intent) = 0;
	virtual bool CancelOrder(_TyOrderId orderId) = 0;
	virtual CPositionSnapshot GetPosition(const market::CSecurity& security) const = 0;
	virtual CAccountSnapshot GetAccount() const = 0;
	virtual void WriteLog(LogLevel level, const std::string& strMessage) = 0;
	virtual bool IsMarketAvailable() const = 0;
};

#endif
