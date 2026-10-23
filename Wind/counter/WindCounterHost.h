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
	struct CHost
	{
		std::shared_ptr<ITradingGateway> m_gateway;
		std::map<Id, std::shared_ptr<ITradingGateway>> m_routes;
		std::function<CResult(Id, std::int64_t)> m_addAccount;
	};
	// Wind-specific composition root. None of these dependencies enter the counter core.
	CResult CreateWindCounterHost(const std::vector<CAccountRoute>& routes, const std::string& strRulesPath, CSession& session, CHost& host);
} // namespace trading
