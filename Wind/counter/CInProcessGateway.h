#pragma once
#include "CSimulatedCounter.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

namespace trade
{
	class CInProcessGateway final : public ITradingGateway, public IMarketInput
	{
		public:
		using Clock = std::function<std::int64_t()>;
		CInProcessGateway(std::unique_ptr<CSimulatedCounter> counter, Clock clock);
		~CInProcessGateway() override;
		CResult Start(const std::vector<CAccount>& accounts, const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar);
		CResult Request(CRequest request, ReplyHandler handler) override;
		void SetEventHandler(EventHandler handler) override;
		CSnapshot Snapshot(Id nAccountId = 0, bool bHistory = true) const override;
		std::vector<Operation> Capabilities() const override;
		void OnDepth(CDepth depth) override;
		void OnMarketState(bool bAvailable) override;
		void Stop() override;
		CResult AddAccount(Id nAccountId, std::int64_t nInitialBalance);

		private:
		void Run();
		void Refresh();
		std::unique_ptr<CSimulatedCounter> m_counter;
		Clock m_clock;
		mutable std::mutex m_mtx_state;
		std::condition_variable m_condition;
		std::deque<std::function<void()>> m_work;
		CSnapshot m_snapshot;
		EventHandler m_handler;
		Id m_delivered{ 0 };
		std::atomic_bool m_bInputAvailable{ false };
		std::atomic_uint64_t m_epoch{ 0 };
		Id m_appliedEpoch{ 0 };
		bool m_bStopping{ true };
		std::thread m_worker;
	};
} // namespace trading
