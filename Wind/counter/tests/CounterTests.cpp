#include "CSimulatedCounter.h"
#include "CInProcessGateway.h"
#include <iostream>
#include <stdexcept>
#include <future>
#include <deque>
#include <atomic>
#include <limits>
using namespace trading;
namespace
{
	struct CMemory
	{
		CState m_state;
		bool m_bFail{ false }, m_bLease{ false };
	};
	class CStore final : public ICounterStore
	{
		public:
		explicit CStore(std::shared_ptr<CMemory> memory) : m_memory(std::move(memory)) {}
		CResult Acquire(CState& state) override
		{
			if (m_memory->m_bLease)
			{
				return { Error::Unavailable, "lease busy" };
			}
			m_memory->m_bLease = true;
			state = m_memory->m_state;
			return {};
		}
		CResult Commit(const CState& state, Id nExpected) override
		{
			if (m_memory->m_bFail || !m_memory->m_bLease || (nExpected != m_memory->m_state.m_sequence))
			{
				return { Error::Unavailable, "injected failure" };
			}
			ApplyChanges(m_memory->m_state, state);
			return {};
		}
		CResult Renew() override
		{
			return m_memory->m_bLease ? CResult{} : CResult{ Error::Unavailable, "lease lost" };
		}
		void Release() override
		{
			m_memory->m_bLease = false;
		}

		private:
		std::shared_ptr<CMemory> m_memory;
	};
	void Check(bool bOk, const char* pText)
	{
		if (!bOk)
		{
			throw std::runtime_error(pText);
		}
	}
	CRule Rule(int nDay)
	{
		CRule rule;
		rule.m_strVersion = "fixture-1";
		rule.m_strSecurity = "600000.SSE";
		rule.m_strBoard = "MAIN";
		rule.m_nDay = nDay;
		rule.m_nLower = 500;
		rule.m_nUpper = 2000;
		rule.m_nCommissionPpm = 100;
		rule.m_nMinimumFee = 5;
		return rule;
	}
	CRequest Request(Operation operation, Id nAccount = 1, Id nClient = 1, std::int64_t nQuantity = 100, std::int64_t nPrice = 1000)
	{
		CRequest request;
		request.m_operation = operation;
		request.m_accountId = nAccount;
		request.m_requestId = nClient;
		request.m_order.m_clientId = nClient;
		request.m_order.m_nPrice = nPrice;
		request.m_order.m_nQuantity = nQuantity;
		request.m_order.m_strSecurity = "600000.SSE";
		return request;
	}
	void Connect(CSimulatedCounter& counter, Id nId, std::int64_t nNow)
	{
		Check(counter.Execute(Request(Operation::Connect, nId), nNow).m_result.Ok(), "connect");
	}

	class CDeferredBroker final : public ITradingGateway
	{
		public:
		CResult Request(CRequest request, ReplyHandler handler) override
		{
			m_work.emplace_back([this, request, handler]()
			{
				CReply reply;
				reply.m_requestId = request.m_requestId;
				reply.m_snapshot.m_bReady = true;
				reply.m_snapshot.m_accounts.push_back({ 1, 1000000, 0, true, true });
				if (Operation::Submit == request.m_operation)
				{
					CEvent event;
					event.m_kind = EventKind::Order;
					event.m_accountId = 1;
					event.m_order = request.m_order;
					event.m_order.m_accountId = 1;
					event.m_order.m_orderId = 42;
					event.m_sequence = 1;
					if (nullptr != m_handler)
					{
						m_handler(event);
					}
					reply.m_orderId = 42;
				}
				if (nullptr != handler)
				{
					handler(reply);
				}
			});
			return {};
		}
		void SetEventHandler(EventHandler handler) override
		{
			m_handler = std::move(handler);
		}
		CSnapshot Snapshot(Id = 0, bool = true) const override
		{
			CSnapshot snapshot;
			snapshot.m_bReady = true;
			snapshot.m_accounts.push_back({ 1, 1000000, 0, true, true });
			return snapshot;
		}
		std::vector<Operation> Capabilities() const override
		{
			return { Operation::Connect, Operation::Submit, Operation::Funds, Operation::Orders, Operation::Positions, Operation::Trades, Operation::Cancel, Operation::Synchronize };
		}
		void Stop() override
		{
			m_work.clear();
		}
		void Pump()
		{
			while (!m_work.empty())
			{
				std::function<void()> work = std::move(m_work.front());
				m_work.pop_front();
				work();
			}
		}

		private:
		EventHandler m_handler;
		std::deque<std::function<void()>> m_work;
	};
	void Contract(ITradingGateway& gateway, const std::function<void()>& pump)
	{
		std::atomic_int events{ 0 };
		gateway.SetEventHandler([&events](const CEvent& event)
		{
			if (EventKind::Order == event.m_kind)
			{
				++events;
			}
		});
		auto send = [&gateway, &pump](CRequest request)
		{
			std::shared_ptr<std::promise<CReply>> promise = std::make_shared<std::promise<CReply>>();
			std::future<CReply> future = promise->get_future();
			Check(gateway.Request(std::move(request), [promise](const CReply& reply)
			{
				promise->set_value(reply);
			}).Ok(),
				"request receipt");
			pump();
			Check(std::future_status::ready == future.wait_for(std::chrono::seconds(3)), "async completion");
			return future.get();
		};
		Check(send(Request(Operation::Connect)).m_result.Ok(), "connect contract");
		for (Operation operation : { Operation::Funds, Operation::Positions, Operation::Orders, Operation::Trades, Operation::Synchronize })
		{
			CReply reply = send(Request(operation));
			Check(reply.m_result.Ok() && reply.m_snapshot.m_bReady && 1 == reply.m_snapshot.m_accounts.size(), "query contract");
		}
		Check(send(Request(Operation::Submit, 1, 99)).m_result.Ok(), "submit contract");
		Check(0 < events.load(), "order event may precede reply");
		gateway.SetEventHandler({});
		gateway.Stop();
	}
	void Boundaries()
	{
		std::shared_ptr<CMemory> memory = std::make_shared<CMemory>();
		CSimulatedCounter counter(std::make_unique<CStore>(memory));
		Check(counter.Start(0).Ok(), "boundary start");
		Check(counter.AddAccount(1, 1000000).Ok(), "boundary account");
		Connect(counter, 1, 1100);
		counter.OnMarketState(true);
		Check(!counter.Execute(Request(Operation::Submit), 1100).m_result.Ok(), "missing rules");
		Check(counter.ImportRules({ Rule(1) }, { { 1, 1000, 2000, 3000, 4000 } }).Ok(), "boundary rules");
		Check(!counter.Execute(Request(Operation::Submit, 1, 1, 101), 1100).m_result.Ok(), "buy lot");
		Check(!counter.Execute(Request(Operation::Submit, 1, 2, 1000000), 1100).m_result.Ok(), "insufficient cash");
		Check(!counter.Execute(Request(Operation::Submit, 1, 3, 100, 2001), 1100).m_result.Ok(), "price band");
		Check(!counter.Execute(Request(Operation::Submit, 1, 4, 100), 2500).m_result.Ok(), "lunch break");
		Check(!counter.Execute(Request(Operation::Submit, 1, 5, (std::numeric_limits<std::int64_t>::max)()), 1100).m_result.Ok(), "overflow");
		Check(counter.Execute(Request(Operation::Submit, 1, 6, 200), 1100).m_result.Ok(), "boundary submit");
		CDepth depth{ "600000.SSE", 1, 1100, 2, { { 990, 500 } }, { { 999, 100 }, { 1000, 100 } } };
		Check(counter.OnDepth(depth, 1100).Ok(), "multi level");
		CSnapshot snapshot = counter.Snapshot(1, 1100);
		Check(2 == snapshot.m_trades.size() && Status::Filled == snapshot.m_orders[0].m_status && 0 == snapshot.m_accounts[0].m_nFrozen, "filled multi level");
		Check(snapshot.m_orders[0].m_nFee == 20 && snapshot.m_accounts[0].m_nBalance == 800080, "cumulative fee exact");
		Check(!counter.OnDepth(CDepth{ "600000.SSE", 2, 1000, 2, {}, { { 1000, 100 } } }, 1100).Ok(), "out of order timestamp");
		Check(!counter.OnDepth(CDepth{ "600000.SSE", 3, 0, 2, {}, { { 1000, 100 } } }, 7000).Ok(), "stale depth");
		counter.OnMarketState(false);
		Check(!counter.Execute(Request(Operation::Submit, 1, 7), 1200).m_result.Ok(), "offline new order");
		Check(counter.Execute(Request(Operation::Funds), 1200).m_result.Ok(), "offline query");
		CRequest cancel = Request(Operation::Cancel);
		cancel.m_orderId = snapshot.m_orders[0].m_orderId;
		Check(counter.Execute(cancel, 1200).m_result.Ok(), "terminal cancel idempotent");
		counter.Stop();
		std::shared_ptr<CMemory> asyncMemory = std::make_shared<CMemory>();
		CInProcessGateway gateway(std::make_unique<CSimulatedCounter>(std::make_unique<CStore>(asyncMemory)), []()
		{
			return 1100;
		});
		Check(gateway.Start({ { 1, 1000000, 0, false, true } }, { Rule(1) }, { { 1, 1000, 2000, 3000, 4000 } }).Ok(), "gateway start");
		gateway.OnMarketState(true);
		Contract(gateway, []()
		{
		});
		CDeferredBroker broker;
		Contract(broker, [&broker]()
		{
			broker.Pump();
		});
	}
	void IncrementalState()
	{
		CState state;
		state.m_accounts.emplace(1, CAccount{ 1, 100000, 0, true, true });
		state.m_accounts.emplace(2, CAccount{ 2, 200000, 0, true, true });
		std::string strBefore = EncodeState(state);
		{
			CStateEdit edit(state);
			edit.Save(&CState::m_accounts, Id{ 1 });
			state.m_accounts.at(1).m_nBalance -= 1000;
			edit.Save(&CState::m_accounts, Id{ 1 });
			state.m_accounts.at(1).m_nBalance -= 1000;
			edit.Save(&CState::m_orders, Id{ 1 });
			state.m_orders.emplace(1, COrder{});
			state.m_nextOrder++;
			state.m_events.push_back(CEvent{});
			CState changes = edit.Changes();
			Check(1 == changes.m_accounts.size() && 1 == changes.m_orders.size() && changes.m_trades.empty() && 1 == changes.m_events.size(), "sparse delta excludes untouched history");
		}
		Check(strBefore == EncodeState(state), "journal restores existing and inserted rows and metadata");
		CSnapshot cache;
		CEvent event;
		event.m_accountId = 1;
		event.m_account = state.m_accounts.at(1);
		event.m_kind = EventKind::Order;
		event.m_order.m_orderId = 1;
		event.m_order.m_accountId = 1;
		UpdateSnapshot(cache, event);
		event.m_order.m_status = Status::Cancelled;
		UpdateSnapshot(cache, event);
		Check(1 == cache.m_orders.size() && Status::Cancelled == cache.m_orders[0].m_status && 1 == cache.m_accounts.size(), "incremental cache replaces rows without duplicating history");
	}
	void Run()
	{
		std::shared_ptr<CMemory> memory = std::make_shared<CMemory>();
		CSimulatedCounter counter(std::make_unique<CStore>(memory));
		Check(counter.Start(0).Ok(), "start");
		Check(counter.AddAccount(1, 1000000).Ok() && counter.AddAccount(2, 1000000).Ok(), "migration");
		Check(counter.ImportRules({ Rule(1), Rule(2) }, { { 1, 1000, 2000, 3000, 4000 }, { 2, 5000, 6000, 7000, 8000 } }).Ok(), "rules");
		Connect(counter, 1, 1100);
		Connect(counter, 2, 1100);
		counter.OnMarketState(true);
		CReply first = counter.Execute(Request(Operation::Submit, 1, 10, 200), 1100);
		Check(first.m_result.Ok(), "submit");
		CReply second = counter.Execute(Request(Operation::Submit, 2, 11, 100, 1100), 1100);
		Check(second.m_result.Ok(), "submit2");
		CDepth depth{ "600000.SSE", 1, 1100, 2, { { 990, 500 } }, { { 1000, 150 } } };
		Check(counter.OnDepth(depth, 1100).Ok(), "depth");
		CSnapshot snapshot = counter.Snapshot(0, 1100);
		Check(2 == snapshot.m_trades.size(), "partial fills");
		Check(100 == snapshot.m_orders[1].m_nFilled && 50 == snapshot.m_orders[0].m_nFilled, "price priority across accounts");
		Check(!counter.OnDepth(depth, 1100).Ok() && 2 == counter.Snapshot(0, 1100).m_trades.size(), "duplicate liquidity");
		Check(counter.Execute(Request(Operation::Submit, 1, 10, 200), 1100).m_orderId == first.m_orderId, "idempotency");
		Check(!counter.Execute(Request(Operation::Submit, 1, 10, 300), 1100).m_result.Ok(), "id conflict");
		CRequest sell = Request(Operation::Submit, 1, 12, 1);
		sell.m_order.m_side = Side::Sell;
		Check(!counter.Execute(sell, 1100).m_result.Ok(), "T+1");
		CRequest cancel = Request(Operation::Cancel);
		cancel.m_orderId = first.m_orderId;
		Check(counter.Execute(cancel, 1200).m_result.Ok(), "cancel");
		Check(0 == counter.Snapshot(1, 1200).m_accounts[0].m_nFrozen, "release freeze");
		Check(counter.Advance(4100).Ok(), "daycut");
		Check(0 == counter.Snapshot(1, 4100).m_positions[0].m_nAvailable, "cannot sell before next trading day");
		Id nVersion = counter.Snapshot(0, 4100).m_version;
		Check(counter.Advance(4200).Ok() && nVersion == counter.Snapshot(0, 4200).m_version, "daycut idempotency");
		sell.m_order.m_clientId = 13;
		sell.m_requestId = 13;
		Check(counter.Execute(sell, 5100).m_result.Ok(), "sell next day");
		depth.m_sequence = 2;
		depth.m_nTime = 5100;
		depth.m_bids[0].m_nPrice = 1000;
		Check(counter.OnDepth(depth, 5100).Ok(), "sell fill");
		Check(3 == counter.Snapshot(0, 5100).m_trades.size(), "sell recorded");
		std::string strSaved = EncodeState(memory->m_state);
		CState restored;
		Check(DecodeState(strSaved, restored) && strSaved == EncodeState(restored), "codec");
		Check(!DecodeState(strSaved + " bad", restored), "bad codec");
		std::int64_t nCash = counter.Snapshot(1, 5100).m_accounts[0].m_nBalance;
		counter.Stop();
		CSimulatedCounter restarted(std::make_unique<CStore>(memory));
		Check(restarted.Start(5200).Ok(), "restart");
		Check(nCash == restarted.Snapshot(1, 5200).m_accounts[0].m_nBalance, "cash persisted");
		Connect(restarted, 1, 5200);
		restarted.OnMarketState(true);
		Check(restarted.Execute(Request(Operation::Submit, 1, 14), 5200).m_result.Ok() && 3 == restarted.Snapshot(0, 5200).m_trades.size(), "restart requires fresh depth");
		memory->m_bFail = true;
		CRequest failed = Request(Operation::Cancel);
		failed.m_orderId = restarted.Snapshot(1, 5200).m_orders.back().m_orderId;
		Id nTail = memory->m_state.m_sequence;
		std::string strBeforeFailure = EncodeState(memory->m_state);
		Check(!restarted.Execute(failed, 5200).m_result.Ok() && !restarted.Ready() && nTail == memory->m_state.m_sequence, "transaction failure fail closed");
		Check(strBeforeFailure == EncodeState(memory->m_state), "failed command preserves durable state");
		Check(restarted.Snapshot(1, 5200).m_accounts[0].m_nFrozen == memory->m_state.m_accounts.at(1).m_nFrozen, "failed cancellation restores in-memory freeze");
		restarted.Stop();
		memory->m_bFail = false;
		CSimulatedCounter replay(std::make_unique<CStore>(memory));
		Check(replay.Start(5300).Ok() && !replay.EventsAfter(nVersion).empty(), "replay");
		memory->m_bLease = false;
		Check(!replay.Renew().Ok() && !replay.Ready(), "fencing");
		replay.Stop();
	}
} // namespace
int main()
{
	try
	{
		IncrementalState();
		Run();
		Boundaries();
		std::cout << "counter tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
