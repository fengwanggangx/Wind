#include "CStrategyEngine.h"

#include "../common/defines.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../request/request.h"
#include "../request/request.pb.h"
#include "../system/CSession.h"
#include "CStrategyContext.h"
#include "CTradeService.h"
#include "strategies/CMovingAverageStrategy.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <exception>
#include <rapidjson/document.h>
#include <utility>

namespace
{
	thread_local _TyStrategyId CurrentStrategyId{ 0 };

	market::CQuoteInfo MakeQuoteInfo(const hqmarket::market::v1::Instrument& instrument, market::Channel channel)
	{
		return market::CQuoteInfo(instrument.symbol(), static_cast<market::Exchange>(static_cast<int>(instrument.exchange())), channel);
	}

	bool ParseUnsigned(const std::string& strValue, std::uint64_t& value)
	{
		if (strValue.empty())
		{
			return false;
		}
		const char* pBegin = strValue.data();
		const char* pEnd = pBegin + strValue.size();
		std::from_chars_result result = std::from_chars(pBegin, pEnd, value);
		return (std::errc() == result.ec) && (pEnd == result.ptr);
	}

	bool ParseParameters(const std::string& strJson, CStrategyConfig& config)
	{
		rapidjson::Document document;
		document.Parse(strJson.c_str());
		if (document.HasParseError() || !document.IsObject())
		{
			return false;
		}
		for (auto iter = document.MemberBegin(); document.MemberEnd() != iter; ++iter)
		{
			if (!iter->name.IsString())
			{
				return false;
			}
			std::string strValue;
			if (iter->value.IsString())
			{
				strValue.assign(iter->value.GetString(), iter->value.GetStringLength());
			}
			else if (iter->value.IsInt64())
			{
				strValue = std::to_string(iter->value.GetInt64());
			}
			else if (iter->value.IsUint64())
			{
				strValue = std::to_string(iter->value.GetUint64());
			}
			else if (iter->value.IsDouble())
			{
				strValue = std::to_string(iter->value.GetDouble());
			}
			else if (iter->value.IsBool())
			{
				strValue = iter->value.GetBool() ? "true" : "false";
			}
			else
			{
				return false;
			}
			config.m_parameters.emplace(std::string(iter->name.GetString(), iter->name.GetStringLength()), std::move(strValue));
		}
		return true;
	}

	bool ParseSubscriptions(const std::string& strJson, CStrategyConfig& config)
	{
		rapidjson::Document document;
		document.Parse(strJson.c_str());
		if (document.HasParseError() || !document.IsArray())
		{
			return false;
		}
		config.m_subscriptions.reserve(document.Size());
		for (const auto& item : document.GetArray())
		{
			if (!item.IsObject() || !item.HasMember("security") || !item["security"].IsString() || !item.HasMember("exchange") || !item["exchange"].IsString() || !item.HasMember("channel") || !item["channel"].IsString())
			{
				return false;
			}
			market::CQuoteInfo quote(item["security"].GetString(), market::ParseMarket(item["exchange"].GetString()), market::ParseChannel(item["channel"].GetString()));
			if (!quote.IsValid())
			{
				return false;
			}
			config.m_subscriptions.emplace_back(std::move(quote));
		}
		return !config.m_subscriptions.empty();
	}
} // namespace

bool CStrategyEvent::IsMarketEvent() const
{
	return (StrategyEventType::Quote == m_type) || (StrategyEventType::Depth == m_type) || (StrategyEventType::MarketTrade == m_type) || (StrategyEventType::Bar == m_type);
}

CStrategyEvent::CStrategyEvent()
{
	std::atomic_uint64_t s_id{ 1 };
	m_sequence = s_id.fetch_add(1, std::memory_order_relaxed);
}

bool CSignalKey::operator==(const CSignalKey& arg) const
{
	return (m_strategyId == arg.m_strategyId) && (m_signalId == arg.m_signalId);
}

std::size_t CSignalKeyHash::operator()(const CSignalKey& key) const
{
	std::size_t strategyHash = std::hash<_TyStrategyId>{ }(key.m_strategyId);
	std::size_t signalHash = std::hash<_TySignalId>{ }(key.m_signalId);
	return strategyHash ^ (signalHash + 0x9e3779b9U + (strategyHash << 6) + (strategyHash >> 2));
}

CStrategyEngine::CStrategyEngine(CSession* pSession) : m_pSession(pSession), m_trader(std::make_unique<CTradeService>())
{
	m_bMarketAvailable.store((nullptr != m_pSession) && m_pSession->IsAuthenticated());
}

CStrategyEngine::~CStrategyEngine()
{
	StopAll();
}

bool CStrategyEngine::Initialize()
{
	m_strLastError.clear();
	if (nullptr == m_pSession)
	{
		m_strLastError = "Strategy session is unavailable";
		return false;
	}
	if (!m_trader->Initialize())
	{
		m_strLastError = m_trader->GetLastError();
		return false;
	}
	m_trader->SetOrderEventHandler(std::bind_front(&CStrategyEngine::OnOrderEvent, this));
	m_pSession->RegisterHandler(std::bind_front(&CStrategyEngine::OnHQMarketResponse, this));
	m_pSession->RegisterStateHandler(std::bind_front(&CStrategyEngine::OnHQMarketState, this));
	return LoadStrategies();
}

const std::string& CStrategyEngine::GetLastError() const
{
	return m_strLastError;
}

bool CStrategyEngine::LoadStrategies()
{
	db::_TyDBPtr pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == pDB)
	{
		m_strLastError = "Strategy database is unavailable";
		return false;
	}
	const db::_TyTableInfo& tableExists = pDB->ExecQuery("SHOW TABLES LIKE 'table_strategy'");
	if (tableExists.second.empty())
	{
		m_strLastError = "MySQL table_strategy does not exist";
		return false;
	}
	const db::_TyTableInfo& table = pDB->ExecQuery("SELECT strategy_id,strategy_type,strategy_name,parameters,subscriptions,event_queue_limit,auto_start FROM table_strategy WHERE enabled=1 ORDER BY strategy_id");
	for (const auto& row : table.second)
	{
		if (7 != row.size())
		{
			m_strLastError = "table_strategy contains an invalid row";
			return false;
		}
		CStrategyConfig cfg;
		std::uint64_t id = 0;
		std::uint64_t eventQueueLimit = 0;
		std::uint64_t autoStart = 0;
		if (!ParseUnsigned(row[0], id) || !ParseUnsigned(row[5], eventQueueLimit) || !ParseUnsigned(row[6], autoStart))
		{
			m_strLastError = "table_strategy contains invalid numeric fields";
			return false;
		}
		cfg.m_id = id;
		cfg.m_strType = row[1];
		cfg.m_strName = row[2];
		cfg.m_eventQueueLimit = static_cast<std::size_t>(eventQueueLimit);
		cfg.m_bAutoStart = 0 != autoStart;
		if (!ParseParameters(row[3], cfg) || !ParseSubscriptions(row[4], cfg) || !CreateStrategy(cfg))
		{
			m_strLastError = "Failed to load strategy " + row[0] + " from table_strategy";
			return false;
		}
	}
	return true;
}

std::unique_ptr<IStrategy> CStrategyEngine::CreateStrategy(const std::string& strType) const
{
	if ("moving_average" == strType)
	{
		return std::make_unique<CMovingAverageStrategy>();
	}
	return nullptr;
}

bool CStrategyEngine::CreateStrategy(const CStrategyConfig& cfg)
{
	if (!cfg.IsValid() || m_bStopping.load())
	{
		return false;
	}
	std::unique_ptr<IStrategy> strategy = CreateStrategy(cfg.m_strType);
	if (nullptr == strategy)
	{
		return false;
	}
	std::shared_ptr<CStrategyRuntime> runtime = std::make_shared<CStrategyRuntime>();
	runtime->m_config = cfg;
	runtime->m_strategy = std::move(strategy);
	runtime->m_context = std::make_unique<CStrategyContext>(cfg.m_id, this, m_trader.get());
	if (!runtime->m_strategy->Initialize(*runtime->m_context, runtime->m_config))
	{
		return false;
	}
	runtime->m_state.store(StrategyState::Initialized);

	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_strategies);
		if (!m_runtimes.emplace(cfg.m_id, runtime).second)
		{
			return false;
		}
	}

	NotifySnapshot(runtime);
	if (cfg.m_bAutoStart && !StartStrategy(cfg.m_id))
	{
		RemoveStrategy(cfg.m_id);
		return false;
	}
	return true;
}

bool CStrategyEngine::RemoveStrategy(_TyStrategyId id)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
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
		std::unique_lock<std::shared_mutex> lock(m_mtx_strategies);
		m_runtimes.erase(id);
	}
	{
		std::lock_guard<std::mutex> lock(m_mtx_signals);
		std::erase_if(m_signals, [id](const auto& item)
					  { return id == item.first.m_strategyId; });
	}
	return true;
}

bool CStrategyEngine::StartStrategy(_TyStrategyId id)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
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
	if (!ExecuteControl(id, StrategyEventType::Start))
	{
		runtime->m_state.store(state);
		RemoveRoutes(runtime->m_config);
		UnsubscribeUnusedQuotes(runtime->m_config);
		return false;
	}
	return true;
}

bool CStrategyEngine::PauseStrategy(_TyStrategyId id)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
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
	if (!ExecuteControl(id, StrategyEventType::Pause))
	{
		runtime->m_state.store(StrategyState::Running);
		runtime->m_bAcceptMarketEvents.store(true);
		return false;
	}
	return true;
}

bool CStrategyEngine::StopStrategy(_TyStrategyId id)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
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
	bool bStopped = ExecuteControl(id, StrategyEventType::Stop);
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
		std::shared_lock<std::shared_mutex> lock(m_mtx_strategies);
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
	if (nullptr != m_trader)
	{
		m_trader->Stop();
	}
}

void CStrategyEngine::OnHQMarketResponse(const CRequest& req)
{
	if (m_bStopping.load() || !m_bMarketAvailable.load() || (CRequest::Type::HQMARKET != req.GetType()))
	{
		return;
	}
	std::string strKey = GetMarketKey(req);
	if (strKey.empty())
	{
		return;
	}
	const _TyReqData& data = req.GetData();
	CStrategyEvent ev;
	if (data.has_quote())
	{
		ev.m_type = StrategyEventType::Quote;
		ev.m_data = data.quote();
	}
	else if (data.has_depth())
	{
		ev.m_type = StrategyEventType::Depth;
		ev.m_data = data.depth();
	}
	else if (data.has_trade())
	{
		ev.m_type = StrategyEventType::MarketTrade;
		ev.m_data = data.trade();
	}
	else if (data.has_bar())
	{
		ev.m_type = StrategyEventType::Bar;
		ev.m_data = data.bar();
	}
	else
	{
		return;
	}
	RouteMarketEvent(strKey, std::move(ev));
}

void CStrategyEngine::OnHQMarketState(SessionState state, const std::string& strReason)
{
	bool bAvailable = SessionState::Ready == state;
	m_bMarketAvailable.store(bAvailable);
	if (bAvailable)
	{
		RestoreRequiredSubscriptions();
	}
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_strategies);
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

void CStrategyEngine::OnOrderEvent(const COrderEvent& ev)
{
	_TyStrategyId id = ev.m_strategyId;
	if (0 == id)
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_orders);
		auto iter = m_orderRoutes.find(ev.m_orderId);
		if (m_orderRoutes.end() != iter)
		{
			id = iter->second;
		}
		else
		{
			auto clientIter = m_clientOrderRoutes.find(ev.m_clientOrderId);
			if (m_clientOrderRoutes.end() != clientIter)
			{
				id = clientIter->second;
			}
		}
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
	if (nullptr == runtime)
	{
		return;
	}
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_orders);
		if (IsFinishedOrderStatus(ev.m_status))
		{
			runtime->m_activeOrderIds.erase(ev.m_orderId);
		}
		else if (0 != ev.m_orderId)
		{
			runtime->m_activeOrderIds.emplace(ev.m_orderId);
		}
	}
	if (IsFinishedOrderStatus(ev.m_status))
	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_orders);
		m_orderRoutes.erase(ev.m_orderId);
		m_clientOrderRoutes.erase(ev.m_clientOrderId);
	}
	CStrategyEvent strategyEvent;
	strategyEvent.m_type = StrategyEventType::Order;
	strategyEvent.m_data = ev;
	EnqueueEvent(runtime, std::move(strategyEvent));
}

void CStrategyEngine::OnTradeEvent(const CTradeEvent& ev)
{
	_TyStrategyId id = ev.m_strategyId;
	if (0 == id)
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_orders);
		auto iter = m_orderRoutes.find(ev.m_orderId);
		if (m_orderRoutes.end() != iter)
		{
			id = iter->second;
		}
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
	if (nullptr == runtime)
	{
		return;
	}
	CStrategyEvent v;
	v.m_type = StrategyEventType::Trade;
	v.m_data = ev;
	EnqueueEvent(runtime, std::move(v));
}

std::optional<CStrategySnapshot> CStrategyEngine::GetSnapshot(_TyStrategyId id) const
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
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
		std::shared_lock<std::shared_mutex> lock(m_mtx_strategies);
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

void CStrategyEngine::RegisterStateHandler(_TyStateHandler&& handler)
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

std::shared_ptr<CStrategyRuntime> CStrategyEngine::FindRuntime(_TyStrategyId id) const
{
	std::shared_lock<std::shared_mutex> lock(m_mtx_strategies);
	const auto mIter = m_runtimes.find(id);
	return m_runtimes.end() == mIter ? nullptr : mIter->second;
}

bool CStrategyEngine::ExecuteControl(_TyStrategyId id, StrategyEventType type)
{
	if (id == CurrentStrategyId)
	{
		return false;
	}
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
	if (nullptr == runtime)
	{
		return false;
	}
	CStrategyEvent ev;
	ev.m_type = type;
	ev.m_completion = std::make_shared<std::promise<bool>>();
	std::future<bool> completion = ev.m_completion->get_future();
	if (!EnqueueEvent(runtime, std::move(ev)))
	{
		return false;
	}
	return completion.get();
}

bool CStrategyEngine::EnqueueEvent(const std::shared_ptr<CStrategyRuntime>& runtime, CStrategyEvent&& ev)
{
	if ((nullptr == runtime) || runtime->m_bRemoving.load() || (ev.IsMarketEvent() && !runtime->m_bAcceptMarketEvents.load()))
	{
		return false;
	}
	bool bSchedule = false;
	bool bFault = false;
	{
		std::lock_guard<std::mutex> lock(runtime->m_mtx_events);
		if (runtime->m_config.m_eventQueueLimit <= runtime->m_events.size())
		{
			const auto vIter = std::find_if(runtime->m_events.begin(), runtime->m_events.end(), [](const CStrategyEvent& queuedEvent)
											{ return queuedEvent.IsMarketEvent(); });
			if (runtime->m_events.end() != vIter)
			{
				runtime->m_events.erase(vIter);
				std::lock_guard<std::mutex> metricsLock(runtime->m_mtx_metrics);
				++runtime->m_droppedMarketEvents;
			}
			else if ((runtime->m_config.m_eventQueueLimit * 2) <= runtime->m_events.size())
			{
				bFault = true;
			}
		}
		runtime->m_events.emplace_back(std::move(ev));
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
		ThreadPoolPtr->PushTask(task_priority::em_normal, 0, [this, runtime]()
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
		case StrategyEventType::Start:
			runtime.m_strategy->OnStart();
			runtime.m_state.store(StrategyState::Running);
			runtime.m_bAcceptMarketEvents.store(true);
			break;
		case StrategyEventType::Pause:
			runtime.m_strategy->OnPause();
			runtime.m_state.store(StrategyState::Paused);
			break;
		case StrategyEventType::Stop:
			runtime.m_strategy->OnStop();
			runtime.m_state.store(StrategyState::Stopped);
			break;
		case StrategyEventType::Quote:
			runtime.m_strategy->OnQuote(std::get<_TyQuoteData>(event.m_data));
			break;
		case StrategyEventType::Depth:
			runtime.m_strategy->OnDepth(std::get<_TyDepthData>(event.m_data));
			break;
		case StrategyEventType::MarketTrade:
			runtime.m_strategy->OnMarketTrade(std::get<_TyMarketTradeData>(event.m_data));
			break;
		case StrategyEventType::Bar:
			runtime.m_strategy->OnBar(std::get<_TyBarData>(event.m_data));
			break;
		case StrategyEventType::Order:
			runtime.m_strategy->OnOrder(std::get<COrderEvent>(event.m_data));
			break;
		case StrategyEventType::Trade:
			runtime.m_strategy->OnTrade(std::get<CTradeEvent>(event.m_data));
			break;
		case StrategyEventType::Timer:
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

void CStrategyEngine::RouteMarketEvent(const std::string& strKey, CStrategyEvent&& ev)
{
	std::vector<std::shared_ptr<CStrategyRuntime>> runtimes;
	{
		std::shared_lock<std::shared_mutex> routesLock(m_mtx_routes);
		const auto routeIter = m_marketRoutes.find(strKey);
		if (m_marketRoutes.end() == routeIter)
		{
			return;
		}
		std::shared_lock<std::shared_mutex> strategiesLock(m_mtx_strategies);
		runtimes.reserve(routeIter->second.size());
		for (_TyStrategyId id : routeIter->second)
		{
			const auto mIter = m_runtimes.find(id);
			if (m_runtimes.end() != mIter)
			{
				runtimes.emplace_back(mIter->second);
			}
		}
	}
	int sz = runtimes.size();
	for (std::size_t i = 0; i < sz; ++i)
	{
		CStrategyEvent routedEvent = ev;
		EnqueueEvent(runtimes[i], std::move(routedEvent));
	}
}

void CStrategyEngine::AddRoutes(const CStrategyConfig& cfg)
{
	std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
	for (const auto& quote : cfg.m_subscriptions)
	{
		std::string strKey = quote.String();
		m_marketRoutes[strKey].emplace(cfg.m_id);
		CSubscriptionEntry& entry = m_subscriptions[strKey];
		entry.m_quote = quote;
		entry.m_strategyIds.emplace(cfg.m_id);
	}
}

void CStrategyEngine::RemoveRoutes(const CStrategyConfig& cfg)
{
	std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
	for (const auto& quote : cfg.m_subscriptions)
	{
		std::string strKey = quote.String();
		auto routeIter = m_marketRoutes.find(strKey);
		if (m_marketRoutes.end() != routeIter)
		{
			routeIter->second.erase(cfg.m_id);
			if (routeIter->second.empty())
			{
				m_marketRoutes.erase(routeIter);
			}
		}
		auto subscriptionIter = m_subscriptions.find(strKey);
		if (m_subscriptions.end() != subscriptionIter)
		{
			subscriptionIter->second.m_strategyIds.erase(cfg.m_id);
		}
	}
}

void CStrategyEngine::SubscribeRequiredQuotes(const CStrategyConfig& cfg)
{
	if ((nullptr == m_pSession) || !m_bMarketAvailable.load())
	{
		return;
	}
	std::vector<market::CQuoteInfo> quotes;
	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
		quotes.reserve(cfg.m_subscriptions.size());
		for (const auto& quote : cfg.m_subscriptions)
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
		std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
		auto mIter = m_subscriptions.find(quote.String());
		if (m_subscriptions.end() != mIter)
		{
			mIter->second.m_bSubscribed = bSubscribed;
			mIter->second.m_bSubscribing = false;
		}
	}
}

void CStrategyEngine::UnsubscribeUnusedQuotes(const CStrategyConfig& cfg)
{
	std::vector<market::CQuoteInfo> quotes;
	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
		for (const auto& quote : cfg.m_subscriptions)
		{
			const auto mIter = m_subscriptions.find(quote.String());
			if ((m_subscriptions.end() != mIter) && mIter->second.m_strategyIds.empty())
			{
				if (mIter->second.m_bSubscribed)
				{
					quotes.emplace_back(mIter->second.m_quote);
				}
				m_subscriptions.erase(mIter);
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
		std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
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
		std::unique_lock<std::shared_mutex> lock(m_mtx_routes);
		auto iter = m_subscriptions.find(quote.String());
		if (m_subscriptions.end() != iter)
		{
			iter->second.m_bSubscribed = bSubscribed;
			iter->second.m_bSubscribing = false;
		}
	}
}

COrderSubmitResult CStrategyEngine::SubmitOrder(_TyStrategyId id, const COrderIntent& intent)
{
	std::shared_ptr<CStrategyRuntime> runtime = FindRuntime(id);
	if ((nullptr == runtime) || (nullptr == m_trader) || (StrategyState::Running != runtime->m_state.load()) || !intent.IsValid())
	{
		return { false, false, 0, 0, "Strategy cannot submit this order" };
	}
	if (!m_bMarketAvailable.load() && ((PositionEffect::Open == intent.m_positionEffect) || (PositionEffect::Unknown == intent.m_positionEffect)))
	{
		return { false, true, 0, 0, "Market is unavailable for opening orders" };
	}
	CSignalKey key{ id, intent.m_signalId };
	{
		std::lock_guard<std::mutex> lock(m_mtx_signals);
		if (!m_signals.emplace(key, SignalState::Pending).second)
		{
			return { false, false, 0, 0, "Duplicate strategy signal" };
		}
	}
	COrderSubmitResult result = m_trader->Submit(intent);
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
			std::unique_lock<std::shared_mutex> lock(m_mtx_orders);
			if (0 != result.m_orderId)
			{
				m_orderRoutes[result.m_orderId] = id;
			}
			if (0 != result.m_clientOrderId)
			{
				m_clientOrderRoutes[result.m_clientOrderId] = id;
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

bool CStrategyEngine::CancelOrder(_TyStrategyId id, _TyOrderId orderId)
{
	if ((nullptr == m_trader) || (0 == orderId))
	{
		return false;
	}
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_orders);
		const auto mIter = m_orderRoutes.find(orderId);
		if ((m_orderRoutes.end() == mIter) || (id != mIter->second))
		{
			return false;
		}
	}
	return m_trader->Cancel(id, orderId);
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
	std::vector<_TyStateHandler> handlers;
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

std::string CStrategyEngine::GetMarketKey(const CRequest& req)
{
	const _TyReqData& data = req.GetData();
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
