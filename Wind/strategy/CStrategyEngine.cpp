#include "CStrategyEngine.h"

#include "../request/request.h"
#include "../request/request.pb.h"
#include "../system/CSession.h"
#include "../thread/CThreadPool.h"
#include "CStrategyContext.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace
{
	thread_local _TyStrategyId CurrentStrategyId{ 0 };

	market::CQuoteInfo MakeQuoteInfo(const hqmarket::market::v1::Instrument& instrument, market::Channel channel)
	{
		return market::CQuoteInfo(instrument.symbol(), static_cast<market::Exchange>(static_cast<int>(instrument.exchange())), channel);
	}
} // namespace

CStrategyEngine::CStrategyEngine(CSession* pSession, IStrategyOrderSink* pOrderSink, ITradingSnapshotProvider* pSnapshotProvider) : m_pSession(pSession), m_pOrderSink(pOrderSink), m_pSnapshotProvider(pSnapshotProvider), m_threadPool(std::make_unique<CThreadPool>(pool_type::em_more_calc))
{
	m_bMarketAvailable.store((nullptr != m_pSession) && m_pSession->IsAuthenticated());
}

CStrategyEngine::~CStrategyEngine()
{
	StopAll();
}

bool CStrategyEngine::CStrategyEvent::IsMarketEvent() const
{
	return (EventType::Quote == m_type) || (EventType::Depth == m_type) || (EventType::MarketTrade == m_type) || (EventType::Bar == m_type);
}

bool CStrategyEngine::CSignalKey::operator==(const CSignalKey& arg) const
{
	return (m_strategyId == arg.m_strategyId) && (m_signalId == arg.m_signalId);
}

std::size_t CStrategyEngine::CSignalKeyHash::operator()(const CSignalKey& key) const
{
	std::size_t strategyHash = std::hash<_TyStrategyId>{ }(key.m_strategyId);
	std::size_t signalHash = std::hash<_TySignalId>{ }(key.m_signalId);
	return strategyHash ^ (signalHash + 0x9e3779b9U + (strategyHash << 6) + (strategyHash >> 2));
}

bool CStrategyEngine::RegisterFactory(const std::string& strType, StrategyFactory&& factory)
{
	if (strType.empty() || (nullptr == factory) || m_bStopping.load())
	{
		return false;
	}
	std::unique_lock<std::shared_mutex> lock(m_smtx_factories);
	return m_factories.emplace(strType, std::move(factory)).second;
}

bool CStrategyEngine::CreateStrategy(const CStrategyConfig& config)
{
	if (!config.IsValid() || m_bStopping.load())
	{
		return false;
	}
	StrategyFactory factory;
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_factories);
		auto iter = m_factories.find(config.m_strType);
		if (m_factories.end() == iter)
		{
			return false;
		}
		factory = iter->second;
	}
	std::unique_ptr<IStrategy> strategy = factory();
	if (nullptr == strategy)
	{
		return false;
	}
	std::shared_ptr<CStrategyRuntime> runtime = std::make_shared<CStrategyRuntime>();
	runtime->m_config = config;
	runtime->m_strategy = std::move(strategy);
	runtime->m_context = std::make_unique<CStrategyContext>(config.m_id, this, m_pSnapshotProvider);
	try
	{
		if (!runtime->m_strategy->Initialize(*runtime->m_context, runtime->m_config))
		{
			return false;
		}
	}
	catch (const std::exception& ex)
	{
		return false;
	}
	catch (...)
	{
		return false;
	}
	runtime->m_state.store(StrategyState::Initialized);
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_strategies);
		if (!m_runtimes.emplace(config.m_id, runtime).second)
		{
			return false;
		}
	}
	NotifySnapshot(runtime);
	if (config.m_bAutoStart && !StartStrategy(config.m_id))
	{
		RemoveStrategy(config.m_id);
		return false;
	}
	return true;
}

bool CStrategyEngine::RemoveStrategy(_TyStrategyId strategyId)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return false;
	}
	StrategyState state = runtime->m_state.load();
	if ((StrategyState::Stopped != state) && (StrategyState::Initialized != state) && (StrategyState::Faulted != state))
	{
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_orders);
		if (!runtime->m_activeOrderIds.empty())
		{
			return false;
		}
	}
	runtime->m_bRemoving.store(true);
	runtime->m_bAcceptMarketEvents.store(false);
	RemoveRoutes(runtime->m_config);
	UnsubscribeUnusedQuotes(runtime->m_config);
	WaitUntilIdle(runtime);
	runtime->m_context->Disable();
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_strategies);
		m_runtimes.erase(strategyId);
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_signals);
		std::erase_if(m_signals, [strategyId](const auto& item)
					  { return strategyId == item.first.m_strategyId; });
	}
	return true;
}

bool CStrategyEngine::StartStrategy(_TyStrategyId strategyId)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return false;
	}
	StrategyState state = runtime->m_state.load();
	bool bStateChanged = false;
	while ((StrategyState::Initialized == state) || (StrategyState::Paused == state) || (StrategyState::Stopped == state))
	{
		if (runtime->m_state.compare_exchange_weak(state, StrategyState::Starting))
		{
			bStateChanged = true;
			break;
		}
	}
	if (!bStateChanged)
	{
		return false;
	}
	if ((StrategyState::Initialized == state) || (StrategyState::Stopped == state))
	{
		AddRoutes(runtime->m_config);
		SubscribeRequiredQuotes(runtime->m_config);
	}
	if (!ExecuteControl(strategyId, EventType::Start))
	{
		runtime->m_state.store(state);
		RemoveRoutes(runtime->m_config);
		UnsubscribeUnusedQuotes(runtime->m_config);
		return false;
	}
	return true;
}

bool CStrategyEngine::PauseStrategy(_TyStrategyId strategyId)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return false;
	}
	StrategyState expected = StrategyState::Running;
	if (!runtime->m_state.compare_exchange_strong(expected, StrategyState::Pausing))
	{
		return false;
	}
	runtime->m_bAcceptMarketEvents.store(false);
	if (!ExecuteControl(strategyId, EventType::Pause))
	{
		runtime->m_state.store(StrategyState::Running);
		runtime->m_bAcceptMarketEvents.store(true);
		return false;
	}
	return true;
}

bool CStrategyEngine::StopStrategy(_TyStrategyId strategyId)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return false;
	}
	StrategyState state = runtime->m_state.load();
	if ((StrategyState::Stopped == state) || (StrategyState::Initialized == state))
	{
		return true;
	}
	while ((StrategyState::Stopping != state) && !runtime->m_state.compare_exchange_weak(state, StrategyState::Stopping))
	{
	}
	if (StrategyState::Stopping == state)
	{
		return false;
	}
	runtime->m_bAcceptMarketEvents.store(false);
	bool bStopped = ExecuteControl(strategyId, EventType::Stop);
	if (!bStopped && (StrategyState::Faulted != runtime->m_state.load()))
	{
		runtime->m_state.store(state);
	}
	RemoveRoutes(runtime->m_config);
	UnsubscribeUnusedQuotes(runtime->m_config);
	return bStopped;
}

void CStrategyEngine::StopAll()
{
	if (m_bStopping.exchange(true))
	{
		return;
	}
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_strategies);
		runtimes.reserve(m_runtimes.size());
		for (const auto& item : m_runtimes)
		{
			runtimes.emplace_back(item.second);
		}
	}
	for (const auto& runtime : runtimes)
	{
		runtime->m_bAcceptMarketEvents.store(false);
		StrategyState state = runtime->m_state.load();
		if ((StrategyState::Initialized != state) && (StrategyState::Stopped != state))
		{
			StopStrategy(runtime->m_config.m_id);
		}
		RemoveRoutes(runtime->m_config);
		UnsubscribeUnusedQuotes(runtime->m_config);
	}
	for (const auto& runtime : runtimes)
	{
		WaitUntilIdle(runtime);
		runtime->m_context->Disable();
	}
	if (nullptr != m_threadPool)
	{
		m_threadPool->ShutDown();
	}
}

void CStrategyEngine::OnMarketResponse(const CRequest& request)
{
	if (m_bStopping.load() || !m_bMarketAvailable.load() || (CRequest::Type::HQMARKET != request.GetType()))
	{
		return;
	}
	std::string strKey = GetMarketKey(request);
	if (strKey.empty())
	{
		return;
	}
	const _TyReqData& data = request.GetData();
	CStrategyEvent event;
	event.m_sequence = m_nextEventSequence.fetch_add(1);
	if (data.has_quote())
	{
		event.m_type = EventType::Quote;
		event.m_data = data.quote();
	}
	else if (data.has_depth())
	{
		event.m_type = EventType::Depth;
		event.m_data = data.depth();
	}
	else if (data.has_trade())
	{
		event.m_type = EventType::MarketTrade;
		event.m_data = data.trade();
	}
	else if (data.has_bar())
	{
		event.m_type = EventType::Bar;
		event.m_data = data.bar();
	}
	else
	{
		return;
	}
	RouteMarketEvent(strKey, std::move(event));
}

void CStrategyEngine::OnMarketState(SessionState state, const std::string& strReason)
{
	bool bAvailable = SessionState::Ready == state;
	m_bMarketAvailable.store(bAvailable);
	if (bAvailable)
	{
		m_threadPool->PushTask(task_priority::em_normal, 0, [this]()
							   {
			if (!m_bStopping.load())
			{
				RestoreRequiredSubscriptions();
			} });
	}
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_strategies);
		for (const auto& item : m_runtimes)
		{
			runtimes.emplace_back(item.second);
		}
	}
	for (const auto& runtime : runtimes)
	{
		if (!bAvailable)
		{
			std::lock_guard<std::mutex> lock(runtime->m_mtx_metrics);
			runtime->m_strLastError = strReason;
		}
		NotifySnapshot(runtime);
	}
}

void CStrategyEngine::OnOrderEvent(const COrderEvent& event)
{
	_TyStrategyId strategyId = event.m_strategyId;
	if (0 == strategyId)
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_orders);
		auto iter = m_orderRoutes.find(event.m_orderId);
		if (m_orderRoutes.end() != iter)
		{
			strategyId = iter->second;
		}
		else
		{
			auto clientIter = m_clientOrderRoutes.find(event.m_clientOrderId);
			if (m_clientOrderRoutes.end() != clientIter)
			{
				strategyId = clientIter->second;
			}
		}
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return;
	}
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_orders);
		if (IsFinishedOrderStatus(event.m_status))
		{
			runtime->m_activeOrderIds.erase(event.m_orderId);
		}
		else if (0 != event.m_orderId)
		{
			runtime->m_activeOrderIds.emplace(event.m_orderId);
		}
	}
	if (IsFinishedOrderStatus(event.m_status))
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_orders);
		m_orderRoutes.erase(event.m_orderId);
		m_clientOrderRoutes.erase(event.m_clientOrderId);
	}
	CStrategyEvent strategyEvent;
	strategyEvent.m_type = EventType::Order;
	strategyEvent.m_sequence = m_nextEventSequence.fetch_add(1);
	strategyEvent.m_data = event;
	EnqueueEvent(runtime, std::move(strategyEvent));
}

void CStrategyEngine::OnTradeEvent(const CTradeEvent& event)
{
	_TyStrategyId strategyId = event.m_strategyId;
	if (0 == strategyId)
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_orders);
		auto iter = m_orderRoutes.find(event.m_orderId);
		if (m_orderRoutes.end() != iter)
		{
			strategyId = iter->second;
		}
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return;
	}
	CStrategyEvent strategyEvent;
	strategyEvent.m_type = EventType::Trade;
	strategyEvent.m_sequence = m_nextEventSequence.fetch_add(1);
	strategyEvent.m_data = event;
	EnqueueEvent(runtime, std::move(strategyEvent));
}

std::optional<CStrategySnapshot> CStrategyEngine::GetSnapshot(_TyStrategyId strategyId) const
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return std::nullopt;
	}
	return MakeSnapshot(runtime);
}

std::vector<CStrategySnapshot> CStrategyEngine::GetSnapshots() const
{
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_strategies);
		runtimes.reserve(m_runtimes.size());
		for (const auto& item : m_runtimes)
		{
			runtimes.emplace_back(item.second);
		}
	}
	std::vector<CStrategySnapshot> snapshots;
	snapshots.reserve(runtimes.size());
	for (const auto& runtime : runtimes)
	{
		snapshots.emplace_back(MakeSnapshot(runtime));
	}
	return snapshots;
}

void CStrategyEngine::RegisterStateHandler(StateHandler&& handler)
{
	if (nullptr == handler)
	{
		return;
	}
	std::lock_guard<std::mutex> lock(m_mtx_handlers);
	m_stateHandlers.emplace_back(std::move(handler));
}

bool CStrategyEngine::IsMarketAvailable() const
{
	return m_bMarketAvailable.load();
}

std::shared_ptr<CStrategyEngine::CStrategyRuntime> CStrategyEngine::FindRuntime(_TyStrategyId strategyId) const
{
	std::shared_lock<std::shared_mutex> lock(m_smtx_strategies);
	auto iter = m_runtimes.find(strategyId);
	return m_runtimes.end() == iter ? nullptr : iter->second;
}

bool CStrategyEngine::ExecuteControl(_TyStrategyId strategyId, EventType type)
{
	if (strategyId == CurrentStrategyId)
	{
		return false;
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if (nullptr == runtime)
	{
		return false;
	}
	CStrategyEvent event;
	event.m_type = type;
	event.m_sequence = m_nextEventSequence.fetch_add(1);
	event.m_completion = std::make_shared<std::promise<bool>>();
	std::future<bool> completion = event.m_completion->get_future();
	if (!EnqueueEvent(runtime, std::move(event)))
	{
		return false;
	}
	return completion.get();
}

bool CStrategyEngine::EnqueueEvent(const std::shared_ptr<CStrategyRuntime>& runtime, CStrategyEvent&& event)
{
	if ((nullptr == runtime) || runtime->m_bRemoving.load() || (event.IsMarketEvent() && !runtime->m_bAcceptMarketEvents.load()))
	{
		return false;
	}
	bool bSchedule = false;
	bool bFault = false;
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_events);
		if (runtime->m_config.m_eventQueueLimit <= runtime->m_events.size())
		{
			auto iter = std::find_if(runtime->m_events.begin(), runtime->m_events.end(), [](const CStrategyEvent& queuedEvent)
									 { return queuedEvent.IsMarketEvent(); });
			if (runtime->m_events.end() != iter)
			{
				runtime->m_events.erase(iter);
				std::lock_guard<std::mutex> metricsLock(runtime->m_mtx_metrics);
				++runtime->m_droppedMarketEvents;
			}
			else if ((runtime->m_config.m_eventQueueLimit * 2) <= runtime->m_events.size())
			{
				bFault = true;
			}
		}
		runtime->m_events.emplace_back(std::move(event));
		if (!runtime->m_bScheduled)
		{
			runtime->m_bScheduled = true;
			bSchedule = true;
		}
	}
	if (bFault)
	{
		HandleStrategyException(*runtime, "Strategy critical event queue exceeded hard limit");
	}
	if (bSchedule)
	{
		m_threadPool->PushTask(task_priority::em_normal, 0, [this, runtime]()
							   { DrainEvents(runtime); });
	}
	return true;
}

void CStrategyEngine::DrainEvents(const std::shared_ptr<CStrategyRuntime>& runtime)
{
	while (true)
	{
		CStrategyEvent event;
		{
			std::lock_guard<std::mutex> lock(runtime->m_mtx_events);
			if (runtime->m_events.empty())
			{
				runtime->m_bScheduled = false;
				runtime->m_bExecutingCallback = false;
				runtime->m_cv_idle.notify_all();
				return;
			}
			event = std::move(runtime->m_events.front());
			runtime->m_events.pop_front();
			runtime->m_bExecutingCallback = true;
		}
		bool bResult = DispatchEvent(*runtime, event);
		if (nullptr != event.m_completion)
		{
			event.m_completion->set_value(bResult);
		}
		{
			std::lock_guard<std::mutex> lock(runtime->m_mtx_events);
			runtime->m_bExecutingCallback = false;
		}
	}
}

bool CStrategyEngine::DispatchEvent(CStrategyRuntime& runtime, CStrategyEvent& event)
{
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	bool bResult = true;
	_TyStrategyId previousStrategyId = CurrentStrategyId;
	CurrentStrategyId = runtime.m_config.m_id;
	try
	{
		switch (event.m_type)
		{
		case EventType::Start:
			runtime.m_strategy->OnStart();
			runtime.m_state.store(StrategyState::Running);
			runtime.m_bAcceptMarketEvents.store(true);
			break;
		case EventType::Pause:
			runtime.m_strategy->OnPause();
			runtime.m_state.store(StrategyState::Paused);
			break;
		case EventType::Stop:
			runtime.m_strategy->OnStop();
			runtime.m_state.store(StrategyState::Stopped);
			break;
		case EventType::Quote:
			runtime.m_strategy->OnQuote(std::get<_TyQuoteData>(event.m_data));
			break;
		case EventType::Depth:
			runtime.m_strategy->OnDepth(std::get<_TyDepthData>(event.m_data));
			break;
		case EventType::MarketTrade:
			runtime.m_strategy->OnMarketTrade(std::get<_TyMarketTradeData>(event.m_data));
			break;
		case EventType::Bar:
			runtime.m_strategy->OnBar(std::get<_TyBarData>(event.m_data));
			break;
		case EventType::Order:
			runtime.m_strategy->OnOrder(std::get<COrderEvent>(event.m_data));
			break;
		case EventType::Trade:
			runtime.m_strategy->OnTrade(std::get<CTradeEvent>(event.m_data));
			break;
		case EventType::Timer:
			runtime.m_strategy->OnTimer(std::get<CTimerEvent>(event.m_data));
			break;
		}
	}
	catch (const std::exception& ex)
	{
		HandleStrategyException(runtime, ex.what());
		bResult = false;
	}
	catch (...)
	{
		HandleStrategyException(runtime, "Unknown strategy exception");
		bResult = false;
	}
	CurrentStrategyId = previousStrategyId;
	std::int64_t duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
	{
		std::lock_guard<std::mutex> lock(runtime.m_mtx_metrics);
		runtime.m_lastEventSequence = event.m_sequence;
		runtime.m_lastCallbackDurationUs = duration;
		runtime.m_maxCallbackDurationUs = (std::max)(runtime.m_maxCallbackDurationUs, duration);
	}
	std::shared_ptr<CStrategyRuntime> runtimeHandle = FindRuntime(runtime.m_config.m_id);
	if (nullptr != runtimeHandle)
	{
		NotifySnapshot(runtimeHandle);
	}
	return bResult;
}

void CStrategyEngine::HandleStrategyException(CStrategyRuntime& runtime, const std::string& strError)
{
	runtime.m_bAcceptMarketEvents.store(false);
	runtime.m_state.store(StrategyState::Faulted);
	std::lock_guard<std::mutex> lock(runtime.m_mtx_metrics);
	runtime.m_strLastError = strError;
}

void CStrategyEngine::RouteMarketEvent(const std::string& strKey, CStrategyEvent&& event)
{
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> routesLock(m_smtx_routes);
		auto routeIter = m_marketRoutes.find(strKey);
		if (m_marketRoutes.end() == routeIter)
		{
			return;
		}
		std::shared_lock<std::shared_mutex> strategiesLock(m_smtx_strategies);
		runtimes.reserve(routeIter->second.size());
		for (_TyStrategyId strategyId : routeIter->second)
		{
			auto runtimeIter = m_runtimes.find(strategyId);
			if (m_runtimes.end() != runtimeIter)
			{
				runtimes.emplace_back(runtimeIter->second);
			}
		}
	}
	for (std::size_t i = 0; i < runtimes.size(); ++i)
	{
		CStrategyEvent routedEvent = event;
		EnqueueEvent(runtimes[i], std::move(routedEvent));
	}
}

void CStrategyEngine::AddRoutes(const CStrategyConfig& config)
{
	std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
	for (const auto& quote : config.m_subscriptions)
	{
		std::string strKey = quote.String();
		m_marketRoutes[strKey].emplace(config.m_id);
		CSubscriptionEntry& entry = m_subscriptions[strKey];
		entry.m_quote = quote;
		entry.m_strategyIds.emplace(config.m_id);
	}
}

void CStrategyEngine::RemoveRoutes(const CStrategyConfig& config)
{
	std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
	for (const auto& quote : config.m_subscriptions)
	{
		std::string strKey = quote.String();
		auto routeIter = m_marketRoutes.find(strKey);
		if (m_marketRoutes.end() != routeIter)
		{
			routeIter->second.erase(config.m_id);
			if (routeIter->second.empty())
			{
				m_marketRoutes.erase(routeIter);
			}
		}
		auto subscriptionIter = m_subscriptions.find(strKey);
		if (m_subscriptions.end() != subscriptionIter)
		{
			subscriptionIter->second.m_strategyIds.erase(config.m_id);
		}
	}
}

void CStrategyEngine::SubscribeRequiredQuotes(const CStrategyConfig& config)
{
	if ((nullptr == m_pSession) || !m_bMarketAvailable.load())
	{
		return;
	}
	std::vector<market::CQuoteInfo> quotes;
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
		quotes.reserve(config.m_subscriptions.size());
		for (const auto& quote : config.m_subscriptions)
		{
			auto iter = m_subscriptions.find(quote.String());
			if ((m_subscriptions.end() != iter) && !iter->second.m_bSubscribed && !iter->second.m_bSubscribing)
			{
				iter->second.m_bSubscribing = true;
				quotes.emplace_back(quote);
			}
		}
	}
	for (const auto& quote : quotes)
	{
		bool bSubscribed = m_pSession->SubscribeQuote(quote);
		std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
		auto iter = m_subscriptions.find(quote.String());
		if (m_subscriptions.end() != iter)
		{
			iter->second.m_bSubscribed = bSubscribed;
			iter->second.m_bSubscribing = false;
		}
	}
}

void CStrategyEngine::UnsubscribeUnusedQuotes(const CStrategyConfig& config)
{
	std::vector<market::CQuoteInfo> quotes;
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
		for (const auto& quote : config.m_subscriptions)
		{
			auto iter = m_subscriptions.find(quote.String());
			if ((m_subscriptions.end() != iter) && iter->second.m_strategyIds.empty())
			{
				if (iter->second.m_bSubscribed)
				{
					quotes.emplace_back(iter->second.m_quote);
				}
				m_subscriptions.erase(iter);
			}
		}
	}
	if (nullptr != m_pSession)
	{
		for (const auto& quote : quotes)
		{
			m_pSession->UnsubscribeQuote(quote);
		}
	}
}

void CStrategyEngine::RestoreRequiredSubscriptions()
{
	if (nullptr == m_pSession)
	{
		return;
	}
	std::vector<market::CQuoteInfo> quotes;
	{
		std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
		quotes.reserve(m_subscriptions.size());
		for (const auto& item : m_subscriptions)
		{
			if (!item.second.m_strategyIds.empty() && !item.second.m_bSubscribed && !item.second.m_bSubscribing)
			{
				m_subscriptions[item.first].m_bSubscribing = true;
				quotes.emplace_back(item.second.m_quote);
			}
		}
	}
	for (const auto& quote : quotes)
	{
		bool bSubscribed = m_pSession->SubscribeQuote(quote);
		std::unique_lock<std::shared_mutex> lock(m_smtx_routes);
		auto iter = m_subscriptions.find(quote.String());
		if (m_subscriptions.end() != iter)
		{
			iter->second.m_bSubscribed = bSubscribed;
			iter->second.m_bSubscribing = false;
		}
	}
}

COrderSubmitResult CStrategyEngine::SubmitOrder(_TyStrategyId strategyId, const COrderIntent& intent)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(strategyId);
	if ((nullptr == runtime) || (nullptr == m_pOrderSink) || (StrategyState::Running != runtime->m_state.load()) || !intent.IsValid())
	{
		return { false, false, 0, 0, "Strategy cannot submit this order" };
	}
	if (!m_bMarketAvailable.load() && ((PositionEffect::Open == intent.m_positionEffect) || (PositionEffect::Unknown == intent.m_positionEffect)))
	{
		return { false, true, 0, 0, "Market is unavailable for opening orders" };
	}
	CSignalKey key{ strategyId, intent.m_signalId };
	{
		std::lock_guard<std::mutex> lock(m_mtx_signals);
		if (!m_signals.emplace(key, SignalState::Pending).second)
		{
			return { false, false, 0, 0, "Duplicate strategy signal" };
		}
	}
	COrderSubmitResult result = m_pOrderSink->Submit(intent);
	{
		std::lock_guard<std::mutex> lock(m_mtx_signals);
		if (!result.m_bAccepted && result.m_bRetryable)
		{
			m_signals.erase(key);
		}
		else
		{
			m_signals[key] = result.m_bAccepted ? SignalState::Accepted : SignalState::Rejected;
		}
	}
	if (result.m_bAccepted)
	{
		{
			std::unique_lock<std::shared_mutex> lock(m_smtx_orders);
			if (0 != result.m_orderId)
			{
				m_orderRoutes[result.m_orderId] = strategyId;
			}
			if (0 != result.m_clientOrderId)
			{
				m_clientOrderRoutes[result.m_clientOrderId] = strategyId;
			}
		}
		if (0 != result.m_orderId)
		{
			std::lock_guard<std::mutex> lock(runtime->m_mtx_orders);
			runtime->m_activeOrderIds.emplace(result.m_orderId);
		}
	}
	return result;
}

bool CStrategyEngine::CancelOrder(_TyStrategyId strategyId, _TyOrderId orderId)
{
	if ((nullptr == m_pOrderSink) || (0 == orderId))
	{
		return false;
	}
	{
		std::shared_lock<std::shared_mutex> lock(m_smtx_orders);
		auto iter = m_orderRoutes.find(orderId);
		if ((m_orderRoutes.end() == iter) || (strategyId != iter->second))
		{
			return false;
		}
	}
	return m_pOrderSink->Cancel(strategyId, orderId);
}

CStrategySnapshot CStrategyEngine::MakeSnapshot(const std::shared_ptr<CStrategyRuntime>& runtime) const
{
	CStrategySnapshot snapshot;
	snapshot.m_strategyId = runtime->m_config.m_id;
	snapshot.m_strType = runtime->m_config.m_strType;
	snapshot.m_strName = runtime->m_config.m_strName;
	snapshot.m_state = runtime->m_state.load();
	snapshot.m_bMarketAvailable = m_bMarketAvailable.load();
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_events);
		snapshot.m_queueLength = runtime->m_events.size();
	}
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_orders);
		snapshot.m_activeOrderCount = runtime->m_activeOrderIds.size();
	}
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_metrics);
		snapshot.m_lastEventSequence = runtime->m_lastEventSequence;
		snapshot.m_droppedMarketEvents = runtime->m_droppedMarketEvents;
		snapshot.m_lastCallbackDurationUs = runtime->m_lastCallbackDurationUs;
		snapshot.m_maxCallbackDurationUs = runtime->m_maxCallbackDurationUs;
		snapshot.m_strLastError = runtime->m_strLastError;
	}
	return snapshot;
}

void CStrategyEngine::NotifySnapshot(const std::shared_ptr<CStrategyRuntime>& runtime)
{
	CStrategySnapshot snapshot = MakeSnapshot(runtime);
	std::vector<StateHandler> handlers;
	{
		std::lock_guard<std::mutex> lock(m_mtx_handlers);
		handlers = m_stateHandlers;
	}
	for (const auto& handler : handlers)
	{
		if (nullptr != handler)
		{
			try
			{
				handler(snapshot);
			}
			catch (...)
			{
			}
		}
	}
}

void CStrategyEngine::WaitUntilIdle(const std::shared_ptr<CStrategyRuntime>& runtime)
{
	std::unique_lock<std::mutex> lock(runtime->m_mtx_events);
	runtime->m_cv_idle.wait(lock, [&runtime]()
							{ return runtime->m_events.empty() && !runtime->m_bScheduled && !runtime->m_bExecutingCallback; });
}

std::string CStrategyEngine::GetMarketKey(const CRequest& request)
{
	const _TyReqData& data = request.GetData();
	if (data.has_quote())
	{
		return MakeQuoteInfo(data.quote().instrument(), market::Channel::quote).String();
	}
	if (data.has_depth())
	{
		return MakeQuoteInfo(data.depth().instrument(), market::Channel::depth).String();
	}
	if (data.has_trade())
	{
		return MakeQuoteInfo(data.trade().instrument(), market::Channel::trade).String();
	}
	if (data.has_bar())
	{
		market::Channel channel = static_cast<market::Channel>(static_cast<int>(data.bar().channel()));
		return MakeQuoteInfo(data.bar().instrument(), channel).String();
	}
	return { };
}

bool CStrategyEngine::IsFinishedOrderStatus(OrderStatus status)
{
	return (OrderStatus::Rejected == status) || (OrderStatus::Filled == status) || (OrderStatus::Cancelled == status);
}
