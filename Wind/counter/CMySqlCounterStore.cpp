#include "CMySqlCounterStore.h"
#include <charconv>
namespace trading
{
	namespace
	{
		std::string Hex(const std::string& strValue)
		{
			static constexpr char Digits[] = "0123456789abcdef";
			std::string strHex;
			strHex.reserve(strValue.size() * 2 + 3);
			strHex = "X'";
			for (unsigned char nValue : strValue)
			{
				strHex += Digits[nValue >> 4];
				strHex += Digits[nValue & 15];
			}
			return strHex + "'";
		}
		bool Number(const std::string& strText, Id& nValue)
		{
			std::from_chars_result result = std::from_chars(strText.data(), strText.data() + strText.size(), nValue);
			return (std::errc() == result.ec) && (strText.data() + strText.size() == result.ptr);
		}
		class CTransaction
		{
			public:
			explicit CTransaction(ISqlSession& session) : m_session(session) {}
			~CTransaction()
			{
				if (!m_bCommitted)
				{
					m_session.Rollback();
				}
			}
			bool Commit()
			{
				m_bCommitted = m_session.Commit();
				return m_bCommitted;
			}

			private:
			ISqlSession& m_session;
			bool m_bCommitted{ false };
		};
		CResult Failure()
		{
			return { Error::Unavailable, "counter storage or lease unavailable; resynchronize before retry" };
		}
	} // namespace
	CMySqlCounterStore::CMySqlCounterStore(Factory factory, std::string strOwner) : m_factory(std::move(factory)), m_strOwner(std::move(strOwner)) {}
	bool CMySqlCounterStore::CheckFence(ISqlSession& session, bool bAcquire)
	{
		std::vector<std::vector<std::string>> rows;
		if (!session.Query("SELECT owner,fence,IF(expires_at>CURRENT_TIMESTAMP(3),1,0) FROM sim_counter_lease WHERE id=1 FOR UPDATE", rows) || (1 != rows.size()) || (3 != rows.front().size()))
		{
			return false;
		}
		Id nFence = 0;
		if (!Number(rows.front()[1], nFence))
		{
			return false;
		}
		if (bAcquire)
		{
			if ("1" == rows.front()[2])
			{
				return false;
			}
			if (!session.Execute("UPDATE sim_counter_lease SET owner=" + Hex(m_strOwner) + ",fence=fence+1,expires_at=TIMESTAMPADD(SECOND,15,CURRENT_TIMESTAMP(3)) WHERE id=1"))
			{
				return false;
			}
			m_fence = nFence + 1;
			return true;
		}
		if ((rows.front()[0] != m_strOwner) || (m_fence != nFence) || ("1" != rows.front()[2]))
		{
			return false;
		}
		return session.Execute("UPDATE sim_counter_lease SET expires_at=TIMESTAMPADD(SECOND,15,CURRENT_TIMESTAMP(3)) WHERE id=1");
	}
	CResult CMySqlCounterStore::Acquire(CState& state)
	{
		std::unique_ptr<ISqlSession> session = m_factory();
		if (nullptr == session)
		{
			return Failure();
		}
		for (const auto& strSql : {
				 "CREATE TABLE IF NOT EXISTS sim_counter_lease(id INT PRIMARY KEY,owner VARBINARY(128) NOT NULL,fence BIGINT UNSIGNED NOT NULL,expires_at DATETIME(3) NOT NULL) ENGINE=InnoDB",
				 "INSERT IGNORE INTO sim_counter_lease VALUES(1,'',0,'1970-01-01 00:00:01')",
				 "CREATE TABLE IF NOT EXISTS sim_counter_checkpoint(id INT PRIMARY KEY,sequence BIGINT UNSIGNED NOT NULL,body LONGBLOB NOT NULL) ENGINE=InnoDB" })
		{
			if (!session->Execute(strSql))
			{
				return Failure();
			}
		}
		for (const auto& strTable : { "accounts", "positions", "orders", "trades", "rules", "calendar", "daycut", "events" })
		{
			if (!session->Execute("CREATE TABLE IF NOT EXISTS sim_counter_" + std::string(strTable) + "(record_key VARBINARY(512) PRIMARY KEY,body LONGBLOB NOT NULL) ENGINE=InnoDB"))
			{
				return Failure();
			}
		}
		if (!session->Begin())
		{
			return Failure();
		}
		CTransaction transaction(*session);
		if (!CheckFence(*session, true))
		{
			return Failure();
		}
		std::vector<std::vector<std::string>> rows;
		if (!session->Query("SELECT body FROM sim_counter_checkpoint WHERE id=1", rows))
		{
			return Failure();
		}
		CState restored;
		if (!rows.empty() && ((1 != rows.front().size()) || !DecodeState(rows.front()[0], restored)))
		{
			return { Error::Rejected, "corrupted counter checkpoint" };
		}
		if (!transaction.Commit())
		{
			return Failure();
		}
		state = std::move(restored);
		return {};
	}
	CResult CMySqlCounterStore::Commit(const CState& state, Id nExpectedSequence)
	{
		std::unique_ptr<ISqlSession> session = m_factory();
		if ((nullptr == session) || !session->Begin())
		{
			return Failure();
		}
		CTransaction transaction(*session);
		if (!CheckFence(*session, false))
		{
			return Failure();
		}
		std::vector<std::vector<std::string>> rows;
		if (!session->Query("SELECT sequence FROM sim_counter_checkpoint WHERE id=1 FOR UPDATE", rows))
		{
			return Failure();
		}
		Id nSequence = 0;
		if (!rows.empty() && ((1 != rows.front().size()) || !Number(rows.front()[0], nSequence)))
		{
			return Failure();
		}
		if (nSequence != nExpectedSequence)
		{
			return { Error::Conflict, "checkpoint version changed" };
		}
		auto upsert = [&session](const std::string& strTable, const std::string& strKey, const CState& row)
		{
			return session->Execute("INSERT INTO sim_counter_" + strTable + "(record_key,body) VALUES(" + Hex(strKey) + "," + Hex(EncodeState(row)) + ") ON DUPLICATE KEY UPDATE body=VALUES(body)");
		};
		// These projections are transactionally consistent with the authoritative checkpoint.
		for (const auto& item : state.m_accounts)
		{
			CState row;
			row.m_accounts.emplace(item);
			if (!upsert("accounts", std::to_string(item.first), row))
			{
				return Failure();
			}
		}
		for (const auto& item : state.m_positions)
		{
			CState row;
			row.m_positions.emplace(item);
			if (!upsert("positions", std::to_string(item.first.first) + ":" + item.first.second, row))
			{
				return Failure();
			}
		}
		for (const auto& item : state.m_orders)
		{
			CState row;
			row.m_orders.emplace(item);
			if (!upsert("orders", std::to_string(item.first), row))
			{
				return Failure();
			}
		}
		for (const auto& item : state.m_trades)
		{
			CState row;
			row.m_trades.emplace(item);
			if (!upsert("trades", std::to_string(item.first), row))
			{
				return Failure();
			}
		}
		for (const auto& item : state.m_rules)
		{
			CState row;
			row.m_rules.emplace(item);
			if (!upsert("rules", std::to_string(item.first.first) + ":" + item.first.second, row))
			{
				return Failure();
			}
		}
		for (const auto& item : state.m_calendar)
		{
			CState row;
			row.m_calendar.emplace(item);
			if (!upsert("calendar", std::to_string(item.first), row))
			{
				return Failure();
			}
		}
		CState daycut;
		daycut.m_nSettledDay = state.m_nSettledDay;
		daycut.m_nClosedDay = state.m_nClosedDay;
		if (!upsert("daycut", "last", daycut))
		{
			return Failure();
		}
		for (const auto& event : state.m_events)
		{
			if (event.m_sequence <= nExpectedSequence)
			{
				continue;
			}
			CState row;
			row.m_events.emplace_back(event);
			if (!session->Execute("INSERT INTO sim_counter_events(record_key,body) VALUES(" + Hex(std::to_string(event.m_sequence)) + "," + Hex(EncodeState(row)) + ")"))
			{
				return Failure();
			}
		}
		if (!session->Execute("INSERT INTO sim_counter_checkpoint(id,sequence,body) VALUES(1," + std::to_string(state.m_sequence) + "," + Hex(EncodeState(state)) + ") ON DUPLICATE KEY UPDATE sequence=VALUES(sequence),body=VALUES(body)"))
		{
			return Failure();
		}
		// A transaction that outlived the lease cannot publish a new checkpoint.
		if (!CheckFence(*session, false))
		{
			return Failure();
		}
		if (!transaction.Commit())
		{
			return { Error::UnknownResult, "commit outcome unknown; reload checkpoint" };
		}
		return {};
	}
	CResult CMySqlCounterStore::Renew()
	{
		std::unique_ptr<ISqlSession> session = m_factory();
		if ((nullptr == session) || !session->Begin())
		{
			return Failure();
		}
		CTransaction transaction(*session);
		if (!CheckFence(*session, false) || !transaction.Commit())
		{
			return Failure();
		}
		return {};
	}
	void CMySqlCounterStore::Release()
	{
		if (0 == m_fence)
		{
			return;
		}
		std::unique_ptr<ISqlSession> session = m_factory();
		if (nullptr != session)
		{
			session->Execute("UPDATE sim_counter_lease SET expires_at='1970-01-01 00:00:01' WHERE id=1 AND owner=" + Hex(m_strOwner) + " AND fence=" + std::to_string(m_fence));
		}
		m_fence = 0;
	}
} // namespace trading
