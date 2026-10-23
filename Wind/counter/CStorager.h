#pragma once
#include "ITradingGateway.h"
#include "../database/IDataBase.h"

namespace trading
{
	class CStorager final : public ICounterStore
	{
		public:
		explicit CStorager(std::string strOwner);
		CResult Acquire(CState& state) override;
		CResult Commit(const CState& state, Id nExpectedSequence) override;
		CResult Renew() override;
		void Release() override;

		private:
		bool CheckFence(db::IDataBase& session, bool bAcquire);
		std::string m_strOwner;
		Id m_fence{ 0 };
	};
} // namespace trading
