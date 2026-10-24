#ifndef XENGINE_STRATEGY_ISTRATEGYORDERSINK_H
#define XENGINE_STRATEGY_ISTRATEGYORDERSINK_H

#include "StrategyTypes.h"

class IStrategyOrderSink
{
	public:
	virtual ~IStrategyOrderSink() = default;

	virtual COrderSubmitResult Submit(const COrderIntent& intent) = 0;
	virtual bool Cancel(_TyStrategyId strategyId, _TyOrderId orderId) = 0;
};

#endif
