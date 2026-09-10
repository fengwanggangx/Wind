#ifndef WIND_STRATEGY_ITRADINGSNAPSHOTPROVIDER_H
#define WIND_STRATEGY_ITRADINGSNAPSHOTPROVIDER_H

#include "StrategyTypes.h"

class ITradingSnapshotProvider
{
  public:
	virtual ~ITradingSnapshotProvider() = default;

	virtual CPositionSnapshot GetPosition(const market::CSecurity& security) const = 0;
	virtual CAccountSnapshot GetAccount() const = 0;
};

#endif
