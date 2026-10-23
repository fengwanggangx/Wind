#include "CInProcessGateway.h"
#include <future>
namespace trading
{
	CInProcessGateway::CInProcessGateway(std::unique_ptr<CSimulatedCounter> counter, Clock clock) : m_counter(std::move(counter)), m_clock(std::move(clock)) {}
	CInProcessGateway::~CInProcessGateway()
	{
		Stop();
	}
	CResult CInProcessGateway::Start(const std::vector<CAccount>& accounts, const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar)
	{
		CResult result = m_counter->Start(m_clock());
		if (!result.Ok())
		{
			return result;
		}
		for (const auto& account : accounts)
		{
			result = m_counter->AddAccount(account.m_accountId, account.m_nBalance);
			if (!result.Ok())
			{
				m_counter->Stop();
				return result;
			}
		}
		result = m_counter->ImportRules(rules, calendar);
		if (!result.Ok())
		{
			m_counter->Stop();
			return result;
		}
		m_snapshot = m_counter->Snapshot(0, m_clock());
		m_delivered = m_snapshot.m_version;
		m_bStopping = false;
		m_worker = std::thread([this]()
		{
			Run();
		});
		return {};
	}
	CResult CInProcessGateway::Request(CRequest request, ReplyHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (m_bStopping || !m_snapshot.m_bReady)
		{
			return { Error::Unavailable, "gateway not ready" };
		}
		if (4096 <= m_work.size())
		{
			return { Error::Unavailable, "gateway queue full" };
		}
		m_work.emplace_back([this, request = std::move(request), handler = std::move(handler)]()
		{
			CReply reply = m_counter->Execute(request, m_clock());
			Refresh();
			// Consumers must allow events before the reply, as a broker adapter may do the same.
			if (nullptr != handler)
			{
				try
				{
					handler(reply);
				}
				catch (...)
				{ /* A consumer callback cannot stop the counter. */
				}
			}
		});
		m_condition.notify_one();
		return {};
	}
	void CInProcessGateway::Refresh()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_snapshot.m_version == m_counter->Version() && m_snapshot.m_bReady == m_counter->Ready())
			{
				m_snapshot.m_nTime = m_clock();
				return;
			}
		}
		std::vector<CEvent> events = m_counter->EventsAfter(m_delivered);
		EventHandler handler;
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_snapshot.m_bReady = m_counter->Ready();
			m_snapshot.m_version = m_counter->Version();
			m_snapshot.m_nTime = m_clock();
			for (const auto& event : events)
			{
				UpdateSnapshot(m_snapshot, event);
			}
			handler = m_handler;
		}
		for (const auto& event : events)
		{
			if (nullptr != handler)
			{
				try
				{
					handler(event);
				}
				catch (...)
				{ /* Persisted events remain replayable. */
				}
			}
			m_delivered = event.m_sequence;
		}
	}
	void CInProcessGateway::Run()
	{
		std::int64_t nLastRenew = m_clock();
		for (;;)
		{
			std::function<void()> work;
			{
				std::unique_lock<std::mutex> lock(m_mtx_state);
				m_condition.wait_for(lock, std::chrono::milliseconds(250), [this]()
				{
					return m_bStopping || !m_work.empty();
				});
				if (!m_work.empty())
				{
					work = std::move(m_work.front());
					m_work.pop_front();
				}
				else if (m_bStopping)
				{
					break;
				}
			}
			Id nEpoch = m_epoch.load();
			if (nEpoch != m_appliedEpoch)
			{
				m_counter->OnMarketState(false);
				m_appliedEpoch = nEpoch;
			}
			m_counter->OnMarketState(m_bInputAvailable.load());
			std::int64_t nNow = m_clock();
			if ((nNow < nLastRenew) || (1000 <= nNow - nLastRenew))
			{
				m_counter->Renew();
				nLastRenew = nNow;
			}
			try
			{
				if (nullptr != work)
				{
					work();
				}
				else
				{
					m_counter->Advance(nNow);
					Refresh();
				}
			}
			catch (...)
			{
				m_counter->Stop();
				Refresh();
			}
		}
		m_counter->Stop();
		Refresh();
	}
	void CInProcessGateway::SetEventHandler(EventHandler handler)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_handler = std::move(handler);
	}
	CSnapshot CInProcessGateway::Snapshot(Id nAccountId, bool bHistory) const
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		CSnapshot snapshot;
		snapshot.m_bReady = m_snapshot.m_bReady;
		snapshot.m_version = m_snapshot.m_version;
		snapshot.m_nTime = m_snapshot.m_nTime;
		for (const auto& row : m_snapshot.m_accounts)
		{
			if (0 == nAccountId || nAccountId == row.m_accountId)
			{
				snapshot.m_accounts.push_back(row);
			}
		}
		for (const auto& row : m_snapshot.m_positions)
		{
			if (0 == nAccountId || nAccountId == row.m_accountId)
			{
				snapshot.m_positions.push_back(row);
			}
		}
		if (bHistory)
		{
			for (const auto& row : m_snapshot.m_orders)
			{
				if (0 == nAccountId || nAccountId == row.m_accountId)
				{
					snapshot.m_orders.push_back(row);
				}
			}
			for (const auto& row : m_snapshot.m_trades)
			{
				if (0 == nAccountId || nAccountId == row.m_accountId)
				{
					snapshot.m_trades.push_back(row);
				}
			}
		}
		return snapshot;
	}
	std::vector<Operation> CInProcessGateway::Capabilities() const
	{
		return { Operation::Login, Operation::Connect, Operation::Disconnect, Operation::Submit, Operation::Cancel, Operation::Funds, Operation::Positions, Operation::Orders, Operation::Trades, Operation::Synchronize };
	}
	void CInProcessGateway::OnDepth(CDepth depth)
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (m_bStopping || (4096 <= m_work.size()))
		{
			return;
		}
		Id nEpoch = m_epoch.load();
		m_work.emplace_back([this, nEpoch, depth = std::move(depth)]() mutable
		{
			if (nEpoch == m_epoch.load() && m_bInputAvailable.load())
			{
				m_counter->OnDepth(std::move(depth), m_clock());
				Refresh();
			}
		});
		m_condition.notify_one();
	}
	void CInProcessGateway::OnMarketState(bool bAvailable)
	{
		if (m_bInputAvailable.exchange(bAvailable) != bAvailable)
		{
			++m_epoch;
		}
		m_condition.notify_one();
	}
	CResult CInProcessGateway::AddAccount(Id nAccountId, std::int64_t nInitialBalance)
	{
		// Administrative provisioning is not a trading request. A future remote host exposes it separately.
		std::shared_ptr<std::promise<CResult>> promise = std::make_shared<std::promise<CResult>>();
		std::future<CResult> future = promise->get_future();
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			if (m_bStopping)
			{
				return { Error::Unavailable, "gateway stopped" };
			}
			m_work.emplace_back([this, nAccountId, nInitialBalance, promise]()
			{
				promise->set_value(m_counter->AddAccount(nAccountId, nInitialBalance));
				Refresh();
			});
		}
		m_condition.notify_one();
		return future.get();
	}
	void CInProcessGateway::Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_bStopping = true;
		}
		m_condition.notify_all();
		if (m_worker.joinable())
		{
			m_worker.join();
		}
	}
} // namespace trading
