#ifndef WIND_STRATEGY_CTRADESERVICE_H
#define WIND_STRATEGY_CTRADESERVICE_H
#include "../request/request.pb.h"
#include "ITradeService.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
class CRequest;
class CSimulatedTradingService;
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
	bool HandleAccountRequest(const CRequest& req, const std::string& actor);
	bool HasAccount(std::uint64_t accountId) const;
	bool IsConnected(std::uint64_t accountId) const;
	bool BeginStrategy(std::uint64_t accountId, _TyStrategyId strategyId);
	void EndStrategy(std::uint64_t accountId, _TyStrategyId strategyId);
	bool HasActiveOrders(std::uint64_t accountId) const;
	COrderSubmitResult Submit(const COrderIntent& intent) override;
	bool Cancel(_TyStrategyId strategyId, _TyOrderId orderId) override;
	CPositionSnapshot GetPosition(const CSecurity& security) const override;
	CAccountSnapshot GetAccount() const override;
	CPositionSnapshot GetPosition(std::uint64_t accountId, const CSecurity& security) const;
	CAccountSnapshot GetAccount(std::uint64_t accountId) const;
	TradeServiceMode GetMode() const;
	const std::string& GetLastError() const;

	private:
	struct CAccountRuntime
	{
		request::TradingAccountInfo m_info;
		std::shared_ptr<CSimulatedTradingService> m_service;
		std::unordered_set<_TyStrategyId> m_runningStrategies;
	};
	std::shared_ptr<CSimulatedTradingService> FindService(std::uint64_t accountId) const;
	mutable std::mutex m_mtx_accounts;
	std::mutex m_mtx_operations;
	std::unordered_map<std::uint64_t, CAccountRuntime> m_accounts;
	std::unordered_map<_TyOrderId, std::uint64_t> m_orderAccounts;
	_TyOrderEventHandler m_orderHandler;
	std::string m_strLastError;
};
#endif
