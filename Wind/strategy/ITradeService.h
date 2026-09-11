#ifndef WIND_STRATEGY_ITRADESERVICE_H
#define WIND_STRATEGY_ITRADESERVICE_H

#include "IStrategyOrderSink.h"
#include "ITradingSnapshotProvider.h"

#include <functional>

class ITradeService : public IStrategyOrderSink, public ITradingSnapshotProvider
{
  public:
	using _TyOrderEventHandler = std::function<void(const COrderEvent&)>;

	~ITradeService() override = default;

	virtual void SetOrderEventHandler(_TyOrderEventHandler&& handler) = 0;
	virtual void Stop() = 0;
};

#endif
