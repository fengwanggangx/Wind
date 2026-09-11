#ifndef WIND_STRATEGY_CTRADESERVICE_H
#define WIND_STRATEGY_CTRADESERVICE_H

#include "ITradeService.h"

#include <memory>
#include <string>

enum class TradeServiceMode
{
	Simulated,
	Production
};

class CTradeService final : public IStrategyOrderSink, public ITradingSnapshotProvider
{
  public:
	using _TyOrderEventHandler = ITradeService::_TyOrderEventHandler;

	CTradeService() = default;
	~CTradeService();

	bool Initialize();
	void Stop();
	void SetOrderEventHandler(_TyOrderEventHandler&& handler);

	COrderSubmitResult Submit(const COrderIntent& intent) override;
	bool Cancel(_TyStrategyId strategyId, _TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const market::CSecurity& security) const override;
	CAccountSnapshot GetAccount() const override;

	TradeServiceMode GetMode() const;
	const std::string& GetLastError() const;

  private:
	std::unique_ptr<ITradeService> m_service;
	TradeServiceMode m_mode{ TradeServiceMode::Simulated };
	std::string m_strLastError;
};

#endif
