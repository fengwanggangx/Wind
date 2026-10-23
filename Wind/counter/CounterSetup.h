#pragma once
#include "ITradingGateway.h"
#include <memory>

class CSession;
namespace trading
{
	struct CAccountRoute
	{
		Id m_accountId{ 0 };
		std::int64_t m_nInitialBalance{ 0 };
		std::string m_strAdapter;
	};
	class CInProcessGateway;
	// Startup wiring only: account provisioning and market subscriptions belong to the host.
	CResult InitializeCounter(const std::vector<CAccountRoute>& routes, const std::string& strRulesPath, CSession& session, std::shared_ptr<CInProcessGateway>& counter);
} // namespace trading
