#pragma once
#include "ITradingGateway.h"
#include <functional>
#include <memory>

namespace trading
{
	class ISqlSession
	{
		public:
		virtual ~ISqlSession() = default;
		virtual bool Execute(const std::string& strSql) = 0;
		virtual bool Query(const std::string& strSql, std::vector<std::vector<std::string>>& rows) = 0;
		virtual bool Begin() = 0;
		virtual bool Commit() = 0;
		virtual void Rollback() = 0;
	};
	class CMySqlCounterStore final : public ICounterStore
	{
		public:
		using Factory = std::function<std::unique_ptr<ISqlSession>()>;
		CMySqlCounterStore(Factory factory, std::string strOwner);
		CResult Acquire(CState& state) override;
		CResult Commit(const CState& state, Id nExpectedSequence) override;
		CResult Renew() override;
		void Release() override;

		private:
		bool CheckFence(ISqlSession& session, bool bAcquire);
		Factory m_factory;
		std::string m_strOwner;
		Id m_fence{ 0 };
	};
} // namespace trading
