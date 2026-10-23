#include "CStorager.h"
#include "CSimulatedCounter.h"
#include "../../database/CDBEngine.h"
#include <mysql.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unistd.h>
using namespace trading;
namespace
{
	class CSql final
	{
		public:
		CSql(const std::string& strSocket, const std::string& strDatabase) : m_mysql(mysql_init(nullptr), mysql_close)
		{
			if (nullptr == mysql_real_connect(m_mysql.get(), "localhost", "root", "", strDatabase.empty() ? nullptr : strDatabase.c_str(), 0, strSocket.c_str(), 0))
			{
				throw std::runtime_error(mysql_error(m_mysql.get()));
			}
		}
		bool Execute(const std::string& strSql)
		{
			return 0 == mysql_real_query(m_mysql.get(), strSql.data(), strSql.size());
		}
		bool Query(const std::string& strSql, std::vector<std::vector<std::string>>& rows)
		{
			if (!Execute(strSql))
			{
				return false;
			}
			std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> result(mysql_store_result(m_mysql.get()), mysql_free_result);
			if (nullptr == result)
			{
				return false;
			}
			rows.clear();
			MYSQL_ROW row = nullptr;
			while (nullptr != (row = mysql_fetch_row(result.get())))
			{
				unsigned long* pLengths = mysql_fetch_lengths(result.get());
				std::vector<std::string> values;
				for (unsigned int nIndex = 0; nIndex < mysql_num_fields(result.get()); ++nIndex)
				{
					values.emplace_back(nullptr == row[nIndex] ? "" : std::string(row[nIndex], pLengths[nIndex]));
				}
				rows.emplace_back(std::move(values));
			}
			return true;
		}
		bool Begin()
		{
			return Execute("START TRANSACTION");
		}
		bool Commit()
		{
			return Execute("COMMIT");
		}
		void Rollback()
		{
			Execute("ROLLBACK");
		}

		private:
		std::unique_ptr<MYSQL, decltype(&mysql_close)> m_mysql;
	};
	void Check(bool bValue, const char* pMessage)
	{
		if (!bValue)
		{
			throw std::runtime_error(pMessage);
		}
	}
} // namespace
int main()
{
	const char* pSocket = std::getenv("COUNTER_TEST_SOCKET");
	if (nullptr == pSocket)
	{
		std::cerr << "COUNTER_TEST_SOCKET required (isolated test server only)\n";
		return 77;
	}
	std::string strDatabase = "xtrade_counter_test_" + std::to_string(getpid());
	try
	{
		CSql admin(pSocket, "");
		Check(admin.Execute("CREATE DATABASE " + strDatabase), "create test database");
		const char* pPort = std::getenv("COUNTER_TEST_PORT");
		Check(nullptr != pPort, "COUNTER_TEST_PORT required for CDBEngine TCP connection");
		db::CConnectParam param("127.0.0.1", static_cast<unsigned int>(std::stoul(pPort)), "root", "", strDatabase, "utf8mb4");
		Check(0 == CDBEngine::InstanceRef().Initialize(db::em_database::mysql, param, 2), "initialize CDBEngine");
		CState state;
		CStorager first("first");
		Check(first.Acquire(state).Ok(), "acquire");
		CStorager second("second");
		CState other;
		Check(!second.Acquire(other).Ok(), "second owner must fail");
		state.m_accounts.emplace(1, CAccount{ 1, 100000, 0, false, true });
		Check(first.Commit(state, 0).Ok(), "transactional checkpoint");
		CSql session(pSocket, strDatabase);
		Check(session.Execute("UPDATE sim_counter_lease SET expires_at='1970-01-01'"), "expire fixture");
		Check(second.Acquire(other).Ok() && other.m_accounts.at(1).m_nBalance == 100000, "takeover restores cash");
		Check(!first.Commit(state, 0).Ok() && !first.Renew().Ok(), "stale fence must fail");
		first.Release();
		Check(second.Renew().Ok(), "old release cannot release new lease");
		second.Release();
		CSimulatedCounter counter(std::make_unique<CStorager>("core"));
		Check(counter.Start(0).Ok(), "core load");
		Check(counter.AddAccount(2, 500000).Ok(), "create account");
		CRule rule;
		rule.m_strVersion = "sql-fixture";
		rule.m_strSecurity = "600000.SSE";
		rule.m_strBoard = "MAIN";
		rule.m_nDay = 1;
		rule.m_nLower = 500;
		rule.m_nUpper = 2000;
		Check(counter.ImportRules({ rule }, { { 1, 1000, 2000, 3000, 4000 } }).Ok(), "persist rules");
		CRequest connect;
		connect.m_operation = Operation::Connect;
		connect.m_accountId = 2;
		Check(counter.Execute(connect, 1100).m_result.Ok(), "connect SQL");
		counter.OnMarketState(true);
		CRequest submit;
		submit.m_operation = Operation::Submit;
		submit.m_requestId = 99;
		submit.m_accountId = 2;
		submit.m_order.m_clientId = 99;
		submit.m_order.m_nQuantity = 100;
		submit.m_order.m_nPrice = 1000;
		submit.m_order.m_strSecurity = "600000.SSE";
		Check(counter.Execute(submit, 1100).m_result.Ok(), "submit SQL");
		Check(counter.OnDepth(CDepth{ "600000.SSE", 1, 1100, 2, {}, { { 1000, 50 } } }, 1100).Ok(), "fill SQL");
		CSnapshot before = counter.Snapshot(2, 1100);
		Check(before.m_accounts[0].m_nBalance == 450000 && before.m_accounts[0].m_nFrozen == 50000 && before.m_positions[0].m_nToday == 50, "SQL ledger");
		Check(session.Execute("CREATE TRIGGER reject_trade_rewrite BEFORE UPDATE ON sim_counter_trades FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='historical trade rewritten'"), "guard historical rows");
		Check(counter.Execute(connect, 1100).m_result.Ok(), "session update never rewrites historical trades");
		std::vector<std::vector<std::string>> rows;
		Check(session.Query("SELECT body FROM sim_counter_checkpoint WHERE id=1", rows), "read compact metadata");
		CState metadata;
		Check(1 == rows.size() && 0 == rows[0][0].find("ROWS1\n") && DecodeState(rows[0][0].substr(6), metadata) && metadata.m_orders.empty() && metadata.m_trades.empty() && metadata.m_events.empty(), "metadata excludes full ledger");
		Id nTail = counter.Snapshot(0, 1100).m_version;
		Check(session.Execute("RENAME TABLE sim_counter_events TO hidden_events"), "inject failure");
		CRequest cancel;
		cancel.m_operation = Operation::Cancel;
		cancel.m_accountId = 2;
		cancel.m_orderId = before.m_orders[0].m_orderId;
		Check(!counter.Execute(cancel, 1100).m_result.Ok() && !counter.Ready(), "storage fail closed");
		Check(50000 == counter.Snapshot(2, 1100).m_accounts[0].m_nFrozen && Status::PartiallyFilled == counter.Snapshot(2, 1100).m_orders[0].m_status, "failed cancellation rolls back memory");
		Check(session.Execute("RENAME TABLE hidden_events TO sim_counter_events"), "restore storage");
		counter.Stop();
		CSimulatedCounter restored(std::make_unique<CStorager>("recovered"));
		Check(restored.Start(1200).Ok(), "restart");
		CSnapshot after = restored.Snapshot(2, 1200);
		Check(after.m_accounts[0].m_nBalance == 450000 && after.m_accounts[0].m_nFrozen == 50000 && after.m_positions[0].m_nToday == 50 && after.m_trades.size() == 1, "fill state restored atomically");
		Check(restored.Snapshot(0, 0).m_accounts.size() == 2 && restored.Snapshot(0, 0).m_version == nTail, "rollback preserves checkpoint");
		Check(!restored.EventsAfter(0).empty(), "events survive restart");
		restored.Stop();
		CDBEngine::InstanceRef().Close();
		Check(admin.Execute("DROP DATABASE " + strDatabase), "drop owned test database");
		std::cout << "MySQL counter tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
