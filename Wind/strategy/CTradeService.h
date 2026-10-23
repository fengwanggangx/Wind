#ifndef WIND_STRATEGY_CTRADESERVICE_H
#define WIND_STRATEGY_CTRADESERVICE_H
#include "../request/request.pb.h"
#include "ITradeService.h"
#include "../counter/CounterSetup.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
class CRequest;
class CSession;

class CTradeService final : public IStrategyOrderSink, public ITradingSnapshotProvider
{
	public:
	using _TyOrderEventHandler = ITradeService::_TyOrderEventHandler;
	CTradeService() = default;
	~CTradeService();
	bool Initialize(CSession& session);
	void Stop();
	void SetOrderEventHandler(_TyOrderEventHandler&& handler);
	bool HandleTradingRequest(const CRequest& req);
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
	const std::string& GetLastError() const;

	private:
	struct CAccountRuntime
	{
		request::TradeAccountRecord m_info;
		std::string m_strAdapter, m_strBroker, m_strSite, m_strExternalAccount;
		std::shared_ptr<trading::ITradingGateway> m_service;
		std::unordered_set<_TyStrategyId> m_runningStrategies;
	};
	std::shared_ptr<trading::ITradingGateway> FindService(std::uint64_t accountId) const;
	mutable std::mutex m_mtx_accounts;
	std::mutex m_mtx_operations;
	std::unordered_map<std::uint64_t, CAccountRuntime> m_accounts;
	std::unordered_map<_TyOrderId, std::uint64_t> m_orderAccounts;
	_TyOrderEventHandler m_orderHandler;
	std::string m_strLastError;
	std::shared_ptr<trading::CInProcessGateway> m_counter;
	using _TyTradeEventHandler = std::function<void(const CTradeEvent&)>;
	_TyTradeEventHandler m_tradeHandler;
	void OnEvent(const trading::CEvent& event);

	public:
	void SetTradeEventHandler(_TyTradeEventHandler handler);
	std::vector<trading::COrder> GetOrders(std::uint64_t accountId) const;
	void SetPushHandler(std::function<void(const CRequest&)> handler);
	void SetAccountReadyHandler(std::function<void(std::uint64_t)> handler);

	private:
	std::function<void(const CRequest&)> m_pushHandler;
	std::function<void(std::uint64_t)> m_accountReadyHandler;
	std::unordered_map<std::uint64_t, std::uint64_t> m_lastEventSequence;
};
#endif
