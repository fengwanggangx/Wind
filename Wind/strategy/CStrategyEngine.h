#ifndef WIND_STRATEGY_CSTRATEGYENGINE_H
#define WIND_STRATEGY_CSTRATEGYENGINE_H

#include "IStrategy.h"
#include "IStrategyOrderSink.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

class CRequest;
class CSession;
class CStrategyContext;
class CThreadPool;
class ITradingSnapshotProvider;
enum class SessionState;

class CStrategyEngine final
{
  public:
	using StrategyFactory = std::function<std::unique_ptr<IStrategy>()>;
	using StateHandler = std::function<void(const CStrategySnapshot&)>;

	CStrategyEngine(CSession* pSession, IStrategyOrderSink* pOrderSink, ITradingSnapshotProvider* pSnapshotProvider = nullptr);
	~CStrategyEngine();
	CStrategyEngine(const CStrategyEngine&) = delete;
	CStrategyEngine& operator=(const CStrategyEngine&) = delete;

	bool RegisterFactory(const std::string& strType, StrategyFactory&& factory);
	bool CreateStrategy(const CStrategyConfig& config);
	bool RemoveStrategy(_TyStrategyId strategyId);

	bool StartStrategy(_TyStrategyId strategyId);
	bool PauseStrategy(_TyStrategyId strategyId);
	bool StopStrategy(_TyStrategyId strategyId);
	void StopAll();

	void OnMarketResponse(const CRequest& request);
	void OnMarketState(SessionState state, const std::string& strReason);
	void OnOrderEvent(const COrderEvent& event);
	void OnTradeEvent(const CTradeEvent& event);

	std::optional<CStrategySnapshot> GetSnapshot(_TyStrategyId strategyId) const;
	std::vector<CStrategySnapshot> GetSnapshots() const;
	void RegisterStateHandler(StateHandler&& handler);
	bool IsMarketAvailable() const;

  private:
	friend class CStrategyContext;

	enum class EventType
	{
		Start,
		Pause,
		Stop,
		Quote,
		Depth,
		MarketTrade,
		Bar,
		Order,
		Trade,
		Timer
	};

	using EventData = std::variant<std::monostate, _TyQuoteData, _TyDepthData, _TyMarketTradeData, _TyBarData, COrderEvent, CTradeEvent, CTimerEvent>;

	struct CStrategyEvent
	{
		EventType m_type{ EventType::Quote };
		std::uint64_t m_sequence{ 0 };
		EventData m_data;
		std::shared_ptr<std::promise<bool>> m_completion;

		bool IsMarketEvent() const;
	};

	struct CStrategyRuntime
	{
		CStrategyConfig m_config;
		std::unique_ptr<IStrategy> m_strategy;
		std::unique_ptr<CStrategyContext> m_context;
		std::atomic<StrategyState> m_state{ StrategyState::Created };
		std::atomic_bool m_bAcceptMarketEvents{ false };
		std::atomic_bool m_bRemoving{ false };

		std::mutex m_mtx_events;
		std::condition_variable m_cv_idle;
		std::deque<CStrategyEvent> m_events;
		bool m_bScheduled{ false };
		bool m_bExecutingCallback{ false };

		std::mutex m_mtx_orders;
		std::unordered_set<_TyOrderId> m_activeOrderIds;

		std::mutex m_mtx_metrics;
		std::uint64_t m_lastEventSequence{ 0 };
		std::uint64_t m_droppedMarketEvents{ 0 };
		std::int64_t m_lastCallbackDurationUs{ 0 };
		std::int64_t m_maxCallbackDurationUs{ 0 };
		std::string m_strLastError;
	};

	struct CSubscriptionEntry
	{
		market::CQuoteInfo m_quote;
		std::unordered_set<_TyStrategyId> m_strategyIds;
		bool m_bSubscribed{ false };
		bool m_bSubscribing{ false };
	};

	struct CSignalKey
	{
		_TyStrategyId m_strategyId{ 0 };
		_TySignalId m_signalId{ 0 };

		bool operator==(const CSignalKey& arg) const;
	};

	struct CSignalKeyHash
	{
		std::size_t operator()(const CSignalKey& key) const;
	};

	enum class SignalState
	{
		Pending,
		Accepted,
		Rejected
	};

	std::shared_ptr<CStrategyRuntime> FindRuntime(_TyStrategyId strategyId) const;
	bool ExecuteControl(_TyStrategyId strategyId, EventType type);
	bool EnqueueEvent(const std::shared_ptr<CStrategyRuntime>& runtime, CStrategyEvent&& event);
	void DrainEvents(const std::shared_ptr<CStrategyRuntime>& runtime);
	bool DispatchEvent(CStrategyRuntime& runtime, CStrategyEvent& event);
	void HandleStrategyException(CStrategyRuntime& runtime, const std::string& strError);
	void RouteMarketEvent(const std::string& strKey, CStrategyEvent&& event);
	void AddRoutes(const CStrategyConfig& config);
	void RemoveRoutes(const CStrategyConfig& config);
	void SubscribeRequiredQuotes(const CStrategyConfig& config);
	void UnsubscribeUnusedQuotes(const CStrategyConfig& config);
	void RestoreRequiredSubscriptions();
	COrderSubmitResult SubmitOrder(_TyStrategyId strategyId, const COrderIntent& intent);
	bool CancelOrder(_TyStrategyId strategyId, _TyOrderId orderId);
	CStrategySnapshot MakeSnapshot(const std::shared_ptr<CStrategyRuntime>& runtime) const;
	void NotifySnapshot(const std::shared_ptr<CStrategyRuntime>& runtime);
	void WaitUntilIdle(const std::shared_ptr<CStrategyRuntime>& runtime);
	static std::string GetMarketKey(const CRequest& request);
	static bool IsFinishedOrderStatus(OrderStatus status);

  private:
	CSession* m_pSession{ nullptr };
	IStrategyOrderSink* m_pOrderSink{ nullptr };
	ITradingSnapshotProvider* m_pSnapshotProvider{ nullptr };

	mutable std::shared_mutex m_smtx_strategies;
	std::unordered_map<_TyStrategyId, std::shared_ptr<CStrategyRuntime>> m_runtimes;
	mutable std::shared_mutex m_smtx_factories;
	std::unordered_map<std::string, StrategyFactory> m_factories;
	mutable std::shared_mutex m_smtx_routes;
	std::unordered_map<std::string, std::unordered_set<_TyStrategyId>> m_marketRoutes;
	std::unordered_map<std::string, CSubscriptionEntry> m_subscriptions;
	mutable std::shared_mutex m_smtx_orders;
	std::unordered_map<_TyOrderId, _TyStrategyId> m_orderRoutes;
	std::unordered_map<_TyClientOrderId, _TyStrategyId> m_clientOrderRoutes;
	std::mutex m_mtx_signals;
	std::unordered_map<CSignalKey, SignalState, CSignalKeyHash> m_signals;
	std::mutex m_mtx_handlers;
	std::vector<StateHandler> m_stateHandlers;

	std::unique_ptr<CThreadPool> m_threadPool;
	std::atomic_uint64_t m_nextEventSequence{ 1 };
	std::atomic_bool m_bMarketAvailable{ false };
	std::atomic_bool m_bStopping{ false };
};

#endif
