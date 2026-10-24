#pragma once
#include "../strategy/AccountInfo.h"
#include "ITradingGateway.h"
#include <memory>
#include <unordered_map>

class CSession;
namespace trade
{
	class CInProcessGateway;
	// Startup wiring only: account provisioning and market subscriptions belong to the host.
	CResult InitializeCounter(const std::unordered_map<std::uint64_t, CTradeAccount>& accounts, const std::string& strRulesPath, CSession& session, std::shared_ptr<CInProcessGateway>& counter);
} // namespace trade
