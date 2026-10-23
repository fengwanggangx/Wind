#include "CSimulatedCounter.h"
#include <algorithm>
#include <limits>
#include <set>

namespace trading
{
	namespace
	{
		constexpr std::int64_t MaxAgeMs = 5000;
		CResult Reject(const std::string& strMessage)
		{
			return { Error::Rejected, strMessage };
		}
		bool Add(std::int64_t nLeft, std::int64_t nRight, std::int64_t& nValue)
		{
			if ((0 > nLeft) || (0 > nRight) || (std::numeric_limits<std::int64_t>::max() - nLeft < nRight))
			{
				return false;
			}
			nValue = nLeft + nRight;
			return true;
		}
		bool Money(std::int64_t nPrice, std::int64_t nQuantity, int nScale, std::int64_t& nValue)
		{
			if ((0 > nPrice) || (0 > nQuantity) || (0 > nScale) || (6 < nScale) || ((0 != nQuantity) && (std::numeric_limits<std::int64_t>::max() / nQuantity < nPrice)))
			{
				return false;
			}
			std::int64_t nProduct = nPrice * nQuantity;
			std::int64_t nDivisor = 1;
			for (int nIndex = 0; nIndex < nScale; ++nIndex)
			{
				nDivisor *= 10;
			}
			// Reject sub-cent turnover: rounding every partial fill would otherwise change the bill.
			if ((2 <= nScale) && (0 != nProduct % (nDivisor / 100)))
			{
				return false;
			}
			if (2 <= nScale)
			{
				nValue = nProduct / (nDivisor / 100);
				return true;
			}
			std::int64_t nMultiplier = 100 / nDivisor;
			if (std::numeric_limits<std::int64_t>::max() / nMultiplier < nProduct)
			{
				return false;
			}
			nValue = nProduct * nMultiplier;
			return true;
		}
		bool Rate(std::int64_t nMoney, std::int64_t nPpm, std::int64_t& nFee)
		{
			if ((0 > nPpm) || (1000000 < nPpm))
			{
				return false;
			}
			std::int64_t nRemainder = nMoney % 1000000 * nPpm;
			return Add(nMoney / 1000000 * nPpm, nRemainder / 1000000 + (0 != nRemainder % 1000000 ? 1 : 0), nFee);
		}
		bool Fee(std::int64_t nTurnover, const CRule& rule, Side side, std::int64_t& nFee)
		{
			if (0 == nTurnover)
			{
				nFee = 0;
				return true;
			}
			std::int64_t nCommission = 0, nTransfer = 0, nTax = 0;
			if (!Rate(nTurnover, rule.m_nCommissionPpm, nCommission) || !Rate(nTurnover, rule.m_nTransferPpm, nTransfer) || !Rate(nTurnover, Side::Sell == side ? rule.m_nSellTaxPpm : 0, nTax))
			{
				return false;
			}
			return Add(std::max(nCommission, rule.m_nMinimumFee), nTransfer, nFee) && Add(nFee, nTax, nFee);
		}
		bool InSession(const CCalendar& day, std::int64_t nNow)
		{
			return ((day.m_nOpen <= nNow) && (nNow < day.m_nBreak)) || ((day.m_nResume <= nNow) && (nNow < day.m_nClose));
		}
	} // namespace
	bool IsActive(Status status)
	{
		return (Status::Accepted == status) || (Status::PartiallyFilled == status);
	}
	CSimulatedCounter::CSimulatedCounter(std::unique_ptr<ICounterStore> store) : m_store(std::move(store)) {}
	CResult CSimulatedCounter::Start(std::int64_t nNow)
	{
		CResult result = m_store->Acquire(m_state);
		if (!result.Ok())
		{
			return result;
		}
		result = Validate(m_state);
		m_bReady = result.Ok();
		if (!m_bReady)
		{
			m_store->Release();
			return result;
		}
		m_depths.clear();
		m_nStartTime = nNow;
		m_bMarketAvailable = false;
		// A restored session is not a live transport connection.
		CStateEdit edit(m_state);
		CState& next = edit.State();
		for (auto& item : next.m_accounts)
		{
			edit.Save(&CState::m_accounts, item.first);
			item.second.m_bConnected = false;
		}
		result = Commit(edit);
		return result.Ok() ? Advance(nNow) : result;
	}
	void CSimulatedCounter::Stop()
	{
		m_bReady = false;
		m_bMarketAvailable = false;
		m_depths.clear();
		m_store->Release();
	}
	CResult CSimulatedCounter::Renew()
	{
		if (!m_bReady)
		{
			return { Error::Unavailable, "counter not ready" };
		}
		CResult result = m_store->Renew();
		if (!result.Ok())
		{
			m_bReady = false;
			m_depths.clear();
		}
		return result;
	}
	Id CSimulatedCounter::Version() const
	{
		return m_state.m_sequence;
	}
	bool CSimulatedCounter::Ready() const
	{
		return m_bReady;
	}
	CResult CSimulatedCounter::Validate(const CState& state) const
	{
		std::map<Id, std::int64_t> frozen;
		std::map<std::pair<Id, std::string>, std::int64_t> shares;
		std::set<std::pair<Id, Id>> clients;
		std::map<Id, std::int64_t> filled, turnover, fees;
		for (const auto& item : state.m_trades)
		{
			const CTrade& trade = item.second;
			std::int64_t nMoney = 0;
			auto order = state.m_orders.find(trade.m_orderId);
			if (item.first != trade.m_tradeId || state.m_nextTrade <= item.first || state.m_orders.end() == order || order->second.m_accountId != trade.m_accountId || order->second.m_strSecurity != trade.m_strSecurity || order->second.m_side != trade.m_side || 0 >= trade.m_nQuantity || 0 > trade.m_nFee || !Money(trade.m_nPrice, trade.m_nQuantity, trade.m_nPriceScale, nMoney) || !Add(filled[trade.m_orderId], trade.m_nQuantity, filled[trade.m_orderId]) || !Add(turnover[trade.m_orderId], nMoney, turnover[trade.m_orderId]) || !Add(fees[trade.m_orderId], trade.m_nFee, fees[trade.m_orderId]))
			{
				return Reject("trade journal invariant failed");
			}
		}

		for (const auto& item : state.m_rules)
		{
			const CRule& rule = item.second;
			if (!state.m_calendar.contains(rule.m_nDay) || 0 >= rule.m_nTick || 0 >= rule.m_nBuyStep || 0 >= rule.m_nSellStep || 0 >= rule.m_nBuyMinimum || 0 >= rule.m_nSellMinimum || 0 >= rule.m_nLower || rule.m_nUpper < rule.m_nLower || 0 > rule.m_nPriceScale || 6 < rule.m_nPriceScale || 0 > rule.m_nCommissionPpm || 1000000 < rule.m_nCommissionPpm || 0 > rule.m_nMinimumFee || 0 > rule.m_nSellTaxPpm || 1000000 < rule.m_nSellTaxPpm || 0 > rule.m_nTransferPpm || 1000000 < rule.m_nTransferPpm)
			{
				return Reject("persisted rule invalid");
			}
		}
		Id nLastSequence = 0;
		for (const auto& event : state.m_events)
		{
			if ((nLastSequence + 1 != event.m_sequence) || (event.m_sequence > state.m_sequence))
			{
				return Reject("event sequence corrupted");
			}
			nLastSequence = event.m_sequence;
		}
		if (nLastSequence != state.m_sequence)
		{
			return Reject("event tail missing");
		}
		for (const auto& item : state.m_orders)
		{
			const COrder& order = item.second;
			if (0 == order.m_clientId || 0 >= order.m_nQuantity || 0 >= order.m_nPrice || Status::Accepted > order.m_status || Status::Rejected < order.m_status || (Side::Buy != order.m_side && Side::Sell != order.m_side) || filled[item.first] != order.m_nFilled || turnover[item.first] != order.m_nTurnover || fees[item.first] != order.m_nFee)
			{
				return Reject("order/trade journal mismatch");
			}

			if ((item.first != order.m_orderId) || (state.m_nextOrder <= order.m_orderId) || !state.m_accounts.contains(order.m_accountId) || !clients.emplace(order.m_accountId, order.m_clientId).second || (0 > order.m_nFilled) || (order.m_nFilled > order.m_nQuantity) || (0 > order.m_nFrozen))
			{
				return Reject("invalid persisted order");
			}
			if (IsActive(order.m_status))
			{
				if (Side::Buy == order.m_side)
				{
					if (!Add(frozen[order.m_accountId], order.m_nFrozen, frozen[order.m_accountId]))
					{
						return Reject("frozen overflow");
					}
				}
				else
				{
					if (!Add(shares[{ order.m_accountId, order.m_strSecurity }], order.m_nQuantity - order.m_nFilled, shares[{ order.m_accountId, order.m_strSecurity }]))
					{
						return Reject("share overflow");
					}
				}
			}
			else if (0 != order.m_nFrozen)
			{
				return Reject("terminal order still frozen");
			}
		}
		for (const auto& item : state.m_accounts)
		{
			if ((item.first != item.second.m_accountId) || (0 > item.second.m_nBalance) || (0 > item.second.m_nFrozen) || (item.second.m_nBalance < item.second.m_nFrozen) || (frozen[item.first] != item.second.m_nFrozen))
			{
				return Reject("cash ledger invariant failed");
			}
		}
		for (const auto& item : state.m_positions)
		{
			const CPosition& position = item.second;
			std::int64_t nTotal = 0;
			if (!state.m_accounts.contains(position.m_accountId) || !Add(position.m_nAvailable, position.m_nFrozen, nTotal) || !Add(nTotal, position.m_nToday, nTotal) || (nTotal != position.m_nTotal) || (position.m_nFrozen != shares[item.first]) || (0 > position.m_nCost))
			{
				return Reject("position ledger invariant failed");
			}
		}
		for (const auto& item : shares)
		{
			if ((0 != item.second) && !state.m_positions.contains(item.first))
			{
				return Reject("missing frozen position");
			}
		}
		return {};
	}
	CResult CSimulatedCounter::Commit(CStateEdit& edit)
	{
		CResult result = Validate(m_state);
		if (result.Ok())
		{
			result = m_store->Commit(edit.Changes(), edit.Sequence());
			if (!result.Ok())
			{
				m_bReady = false;
				m_depths.clear();
			}
		}
		if (result.Ok())
		{
			edit.Accept();
		}
		else
		{
			edit.Rollback();
		}
		return result;
	}
	CResult CSimulatedCounter::AddAccount(Id nAccountId, std::int64_t nInitialBalance)
	{
		if (!m_bReady)
		{
			return { Error::Unavailable, "counter not ready" };
		}
		if (m_state.m_accounts.contains(nAccountId))
		{
			return {};
		}
		if ((0 == nAccountId) || (0 >= nInitialBalance))
		{
			return Reject("invalid migration account");
		}
		CStateEdit edit(m_state);
		CState& next = edit.State();
		edit.Save(&CState::m_accounts, nAccountId);
		next.m_accounts.emplace(nAccountId, CAccount{ nAccountId, nInitialBalance, 0, false, true });
		COrder order;
		order.m_accountId = nAccountId;
		Emit(next, EventKind::Funds, order);
		return Commit(edit);
	}
	CResult CSimulatedCounter::ImportRules(const std::vector<CRule>& rules, const std::vector<CCalendar>& calendar)
	{
		if (!m_bReady)
		{
			return { Error::Unavailable, "counter not ready" };
		}
		CStateEdit edit(m_state);
		CState& next = edit.State();
		for (const auto& day : calendar)
		{
			if ((0 >= day.m_nDay) || !(day.m_nOpen < day.m_nBreak && day.m_nBreak < day.m_nResume && day.m_nResume < day.m_nClose))
			{
				return Reject("invalid calendar");
			}
			auto existing = next.m_calendar.find(day.m_nDay);
			// Existing calendar entries cannot be changed after orders have been accepted.
			if ((next.m_calendar.end() != existing) && !next.m_orders.empty() && ((existing->second.m_nOpen != day.m_nOpen) || (existing->second.m_nBreak != day.m_nBreak) || (existing->second.m_nResume != day.m_nResume) || (existing->second.m_nClose != day.m_nClose)))
			{
				return Reject("calendar immutable after trading");
			}
			edit.Save(&CState::m_calendar, day.m_nDay);
			next.m_calendar[day.m_nDay] = day;
		}
		std::int64_t nPreviousClose = 0;
		for (const auto& entry : next.m_calendar)
		{
			if (entry.second.m_nOpen <= nPreviousClose)
			{
				return Reject("overlapping or unordered calendar");
			}
			nPreviousClose = entry.second.m_nClose;
		}
		for (const auto& rule : rules)
		{
			if (rule.m_strVersion.empty() || rule.m_strSecurity.empty() || !next.m_calendar.contains(rule.m_nDay) || (0 > rule.m_nPriceScale) || (6 < rule.m_nPriceScale) || (0 >= rule.m_nTick) || (0 >= rule.m_nBuyMinimum) || (0 >= rule.m_nBuyStep) || (0 >= rule.m_nSellMinimum) || (0 >= rule.m_nSellStep) || (0 >= rule.m_nLower) || (rule.m_nLower > rule.m_nUpper) || (0 > rule.m_nMinimumFee) || (0 > rule.m_nCommissionPpm) || (1000000 < rule.m_nCommissionPpm) || (0 > rule.m_nSellTaxPpm) || (1000000 < rule.m_nSellTaxPpm) || (0 > rule.m_nTransferPpm) || (1000000 < rule.m_nTransferPpm))
			{
				return Reject("invalid security rule");
			}
			bool bExchange = rule.m_strSecurity.ends_with(".SSE") || rule.m_strSecurity.ends_with(".SZSE");
			if (!bExchange || (("MAIN" != rule.m_strBoard) && ("CHINEXT" != rule.m_strBoard) && ("STAR" != rule.m_strBoard)))
			{
				return { Error::Unsupported, "unsupported security board" };
			}
			std::pair<int, std::string> key = std::make_pair(rule.m_nDay, rule.m_strSecurity);
			auto previous = next.m_rules.find(key);
			if (next.m_rules.end() != previous)
			{
				CState left, right;
				left.m_rules.emplace(key, previous->second);
				right.m_rules.emplace(key, rule);
				if (EncodeState(left) != EncodeState(right))
				{
					return Reject("rule immutable; import a new trading day/version");
				}
			}
			edit.Save(&CState::m_rules, key);
			next.m_rules[key] = rule;
		}
		return Commit(edit);
	}
	const CCalendar* CSimulatedCounter::Calendar(std::int64_t nNow) const
	{
		for (const auto& item : m_state.m_calendar)
		{
			if ((item.second.m_nOpen <= nNow) && (nNow < item.second.m_nClose))
			{
				return &item.second;
			}
		}
		return nullptr;
	}
	void CSimulatedCounter::Emit(CState& state, EventKind kind, const COrder& order, const CTrade& trade, Id nRequestId)
	{
		CEvent event;
		event.m_sequence = ++state.m_sequence;
		event.m_requestId = 0 == nRequestId ? order.m_requestId : nRequestId;
		event.m_accountId = order.m_accountId;
		event.m_kind = kind;
		event.m_order = order;
		event.m_trade = trade;
		event.m_account = state.m_accounts.at(order.m_accountId);
		auto position = state.m_positions.find({ order.m_accountId, order.m_strSecurity });
		if (state.m_positions.end() != position)
		{
			event.m_position = position->second;
		}
		state.m_events.emplace_back(std::move(event));
	}
	CResult CSimulatedCounter::Submit(CStateEdit& edit, COrder order, std::int64_t nNow, Id& nOrderId)
	{
		CState& state = edit.State();
		for (const auto& item : state.m_orders)
		{
			const COrder& previous = item.second;
			if ((previous.m_accountId == order.m_accountId) && ((previous.m_clientId == order.m_clientId) || ((0 != order.m_strategyId) && (previous.m_strategyId == order.m_strategyId) && (previous.m_signalId == order.m_signalId))))
			{
				if ((previous.m_strSecurity != order.m_strSecurity) || (previous.m_side != order.m_side) || (previous.m_nPrice != order.m_nPrice) || (previous.m_nQuantity != order.m_nQuantity) || (previous.m_nPriceScale != order.m_nPriceScale) || (previous.m_strategyId != order.m_strategyId) || (previous.m_signalId != order.m_signalId))
				{
					return { Error::Conflict, "client order ID reused with different parameters" };
				}
				nOrderId = previous.m_orderId;
				return {};
			}
		}
		const CCalendar* day = Calendar(nNow);
		if ((nullptr == day) || !InSession(*day, nNow))
		{
			return { Error::Unsupported, "only continuous trading sessions supported" };
		}
		auto ruleIter = state.m_rules.find({ day->m_nDay, order.m_strSecurity });
		if (state.m_rules.end() == ruleIter)
		{
			return Reject("missing security rules");
		}
		const CRule& rule = ruleIter->second;
		if (rule.m_bCorporateAction)
		{
			return Reject("corporate action pending");
		}
		if ((0 == order.m_clientId) || (0 >= order.m_nQuantity) || (rule.m_nPriceScale != order.m_nPriceScale) || (rule.m_nLower > order.m_nPrice) || (rule.m_nUpper < order.m_nPrice) || (0 != order.m_nPrice % rule.m_nTick))
		{
			return Reject("invalid limit order");
		}
		order.m_orderId = 0;
		order.m_nFilled = 0;
		order.m_nFrozen = 0;
		order.m_nTurnover = 0;
		order.m_nFee = 0;
		order.m_status = Status::Accepted;
		if ((Side::Buy != order.m_side) && (Side::Sell != order.m_side))
		{
			return Reject("invalid side");
		}
		edit.Save(&CState::m_accounts, order.m_accountId);
		CAccount& account = state.m_accounts.at(order.m_accountId);
		if (!account.m_bConnected)
		{
			return { Error::Unavailable, "account disconnected" };
		}
		if (!m_bMarketAvailable)
		{
			return { Error::Unavailable, "market disconnected" };
		}
		std::int64_t nTurnover = 0, nFee = 0;
		if (!Money(order.m_nPrice, order.m_nQuantity, order.m_nPriceScale, nTurnover) || !Fee(nTurnover, rule, order.m_side, nFee))
		{
			return Reject("money overflow or sub-cent turnover");
		}
		if (Side::Sell == order.m_side && nTurnover < nFee)
		{
			return Reject("sell proceeds do not cover order fees");
		}
		if (Side::Buy == order.m_side)
		{
			if ((order.m_nQuantity < rule.m_nBuyMinimum) || (0 != (order.m_nQuantity - rule.m_nBuyMinimum) % rule.m_nBuyStep) || !Add(nTurnover, nFee, order.m_nFrozen) || (order.m_nFrozen > account.m_nBalance - account.m_nFrozen))
			{
				return Reject("invalid buy size or insufficient funds");
			}
			account.m_nFrozen += order.m_nFrozen;
		}
		else
		{
			auto position = state.m_positions.find({ order.m_accountId, order.m_strSecurity });
			if ((state.m_positions.end() == position) || (position->second.m_nAvailable < order.m_nQuantity) || (order.m_nQuantity < rule.m_nSellMinimum) || (0 != (order.m_nQuantity - rule.m_nSellMinimum) % rule.m_nSellStep))
			{
				return Reject("invalid sell size or insufficient available position");
			}
			edit.Save(&CState::m_positions, position->first);
			position->second.m_nAvailable -= order.m_nQuantity;
			position->second.m_nFrozen += order.m_nQuantity;
		}
		order.m_orderId = state.m_nextOrder++;
		order.m_nDay = day->m_nDay;
		order.m_nTime = nNow;
		order.m_strRuleVersion = rule.m_strVersion;
		nOrderId = order.m_orderId;
		edit.Save(&CState::m_orders, order.m_orderId);
		state.m_orders.emplace(order.m_orderId, order);
		Emit(state, EventKind::Order, order);
		Emit(state, EventKind::Funds, order);
		if (Side::Sell == order.m_side)
		{
			Emit(state, EventKind::Position, order);
		}
		return {};
	}
	void CSimulatedCounter::Cancel(CStateEdit& edit, COrder& order, Status status, Id nRequestId)
	{
		CState& state = edit.State();
		if (!IsActive(order.m_status))
		{
			return;
		}
		edit.Save(&CState::m_orders, order.m_orderId);
		edit.Save(&CState::m_accounts, order.m_accountId);
		if (Side::Sell == order.m_side)
		{
			edit.Save(&CState::m_positions, std::make_pair(order.m_accountId, order.m_strSecurity));
		}
		if (Side::Buy == order.m_side)
		{
			state.m_accounts.at(order.m_accountId).m_nFrozen -= order.m_nFrozen;
		}
		else
		{
			CPosition& position = state.m_positions.at({ order.m_accountId, order.m_strSecurity });
			position.m_nAvailable += order.m_nQuantity - order.m_nFilled;
			position.m_nFrozen -= order.m_nQuantity - order.m_nFilled;
		}
		order.m_nFrozen = 0;
		order.m_status = status;
		Emit(state, EventKind::Order, order, {}, nRequestId);
		Emit(state, EventKind::Funds, order, {}, nRequestId);
		if (Side::Sell == order.m_side)
		{
			Emit(state, EventKind::Position, order, {}, nRequestId);
		}
	}
	CResult CSimulatedCounter::Fill(CStateEdit& edit, COrder& order, const CRule& rule, std::int64_t nPrice, std::int64_t nQuantity, std::int64_t nNow)
	{
		CState& state = edit.State();
		std::int64_t nMoney = 0, nTurnover = 0, nTotalFee = 0;
		if (!Money(nPrice, nQuantity, order.m_nPriceScale, nMoney) || !Add(order.m_nTurnover, nMoney, nTurnover) || !Fee(nTurnover, rule, order.m_side, nTotalFee))
		{
			return Reject("fill arithmetic overflow");
		}
		std::int64_t nFee = nTotalFee - order.m_nFee;
		edit.Save(&CState::m_accounts, order.m_accountId);
		CAccount& account = state.m_accounts.at(order.m_accountId);
		std::pair<Id, std::string> key = std::make_pair(order.m_accountId, order.m_strSecurity);
		edit.Save(&CState::m_orders, order.m_orderId);
		edit.Save(&CState::m_positions, key);
		CPosition& position = state.m_positions[key];
		position.m_accountId = order.m_accountId;
		position.m_strSecurity = order.m_strSecurity;
		std::int64_t nRemaining = order.m_nQuantity - order.m_nFilled - nQuantity;
		if (Side::Buy == order.m_side)
		{
			std::int64_t nDebit = 0, nRemainingMoney = 0, nWorstTurnover = 0, nWorstFee = 0, nReserve = 0, nRequired = 0;
			if (!Add(nMoney, nFee, nDebit) || !Money(order.m_nPrice, nRemaining, order.m_nPriceScale, nRemainingMoney) || !Add(nTurnover, nRemainingMoney, nWorstTurnover) || !Fee(nWorstTurnover, rule, order.m_side, nWorstFee) || !Add(nRemainingMoney, nWorstFee - nTotalFee, nReserve) || (account.m_nBalance < nDebit) || !Add(nDebit, nReserve, nRequired) || (order.m_nFrozen < nRequired))
			{
				return Reject("invalid fill reserve");
			}
			account.m_nBalance -= nDebit;
			account.m_nFrozen -= order.m_nFrozen - nReserve;
			order.m_nFrozen = nReserve;
			if (!Add(position.m_nTotal, nQuantity, position.m_nTotal) || !Add(position.m_nToday, nQuantity, position.m_nToday) || !Add(position.m_nCost, nDebit, position.m_nCost))
			{
				return Reject("position overflow");
			}
		}
		else
		{
			if ((position.m_nFrozen < nQuantity) || (nMoney < nFee) || !Add(account.m_nBalance, nMoney - nFee, account.m_nBalance))
			{
				return Reject("invalid sell settlement");
			}
			// Proportional book cost, using quotient/remainder to avoid multiplication overflow.
			std::int64_t nCost = position.m_nCost / position.m_nTotal * nQuantity;
			std::int64_t nRemainder = position.m_nCost % position.m_nTotal;
			if ((0 != nQuantity) && (std::numeric_limits<std::int64_t>::max() / nQuantity < nRemainder))
			{
				return Reject("cost arithmetic overflow");
			}
			nCost += nRemainder * nQuantity / position.m_nTotal;
			position.m_nCost -= nCost;
			position.m_nTotal -= nQuantity;
			position.m_nFrozen -= nQuantity;
		}
		order.m_nFilled += nQuantity;
		order.m_nTurnover = nTurnover;
		order.m_nFee = nTotalFee;
		order.m_status = 0 == nRemaining ? Status::Filled : Status::PartiallyFilled;
		CTrade trade{ state.m_nextTrade++, order.m_orderId, order.m_accountId, order.m_strSecurity, order.m_side, nPrice, nQuantity, nFee, nNow, order.m_nPriceScale };
		edit.Save(&CState::m_trades, trade.m_tradeId);
		state.m_trades.emplace(trade.m_tradeId, trade);
		Emit(state, EventKind::Trade, order, trade);
		Emit(state, EventKind::Order, order);
		Emit(state, EventKind::Funds, order);
		Emit(state, EventKind::Position, order);
		return {};
	}
	CResult CSimulatedCounter::Match(CStateEdit& edit, CDepth& depth, std::int64_t nNow)
	{
		CState& state = edit.State();
		const CCalendar* day = Calendar(nNow);
		if (!m_bMarketAvailable || (nullptr == day) || !InSession(*day, nNow) || (depth.m_nTime > nNow) || (MaxAgeMs < nNow - depth.m_nTime))
		{
			return {};
		}
		auto ruleIter = state.m_rules.find({ day->m_nDay, depth.m_strSecurity });
		if (state.m_rules.end() == ruleIter)
		{
			return {};
		}
		const CRule& rule = ruleIter->second;
		if (rule.m_bCorporateAction || (rule.m_nPriceScale != depth.m_nPriceScale))
		{
			return {};
		}
		for (const auto& level : depth.m_bids)
		{
			if (level.m_nPrice < rule.m_nLower || level.m_nPrice > rule.m_nUpper || 0 != level.m_nPrice % rule.m_nTick)
			{
				return Reject("bid outside configured price rules");
			}
		}
		for (const auto& level : depth.m_asks)
		{
			if (level.m_nPrice < rule.m_nLower || level.m_nPrice > rule.m_nUpper || 0 != level.m_nPrice % rule.m_nTick)
			{
				return Reject("ask outside configured price rules");
			}
		}
		std::vector<Id> orders;
		for (const auto& item : state.m_orders)
		{
			if (IsActive(item.second.m_status) && (item.second.m_nDay == day->m_nDay) && (item.second.m_strSecurity == depth.m_strSecurity) && state.m_accounts.at(item.second.m_accountId).m_bConnected)
			{
				orders.emplace_back(item.first);
			}
		}
		std::sort(orders.begin(), orders.end(), [&state](Id nLeft, Id nRight)
		{
			const COrder& left = state.m_orders.at(nLeft);
			const COrder& right = state.m_orders.at(nRight);
			if (left.m_side != right.m_side)
			{
				return left.m_side < right.m_side;
			}
			if (left.m_nPrice != right.m_nPrice)
			{
				return Side::Buy == left.m_side ? left.m_nPrice > right.m_nPrice : left.m_nPrice < right.m_nPrice;
			}
			return nLeft < nRight;
		});
		for (Id nId : orders)
		{
			COrder& order = state.m_orders.at(nId);
			std::vector<CLevel>& levels = Side::Buy == order.m_side ? depth.m_asks : depth.m_bids;
			for (auto& level : levels)
			{
				if (!IsActive(order.m_status))
				{
					break;
				}
				if ((0 >= level.m_nQuantity) || (Side::Buy == order.m_side ? level.m_nPrice > order.m_nPrice : level.m_nPrice < order.m_nPrice))
				{
					continue;
				}
				std::int64_t nQuantity = std::min(level.m_nQuantity, order.m_nQuantity - order.m_nFilled);
				CResult result = Fill(edit, order, rule, level.m_nPrice, nQuantity, nNow);
				if (!result.Ok())
				{
					return result;
				}
				level.m_nQuantity -= nQuantity;
			}
		}
		return {};
	}
	CResult CSimulatedCounter::Advance(std::int64_t nNow)
	{
		if (!m_bReady)
		{
			return { Error::Unavailable, "counter not ready" };
		}
		bool bNeedsAdvance = false;
		int nPrevious = 0;
		for (const auto& entry : m_state.m_calendar)
		{
			if ((entry.second.m_nOpen <= nNow && nPrevious > m_state.m_nSettledDay) || (entry.second.m_nClose <= nNow && entry.first > m_state.m_nClosedDay))
			{
				bNeedsAdvance = true;
				break;
			}
			nPrevious = entry.first;
		}
		if (!bNeedsAdvance)
		{
			return {};
		}
		CStateEdit edit(m_state);
		CState& next = edit.State();
		bool bChanged = false;
		int nPreviousDay = 0;
		for (const auto& item : m_state.m_calendar)
		{
			const CCalendar& day = item.second;
			if ((day.m_nOpen <= nNow) && (nPreviousDay > next.m_nSettledDay))
			{
				for (auto& entry : next.m_positions)
				{
					CPosition& position = entry.second;
					edit.Save(&CState::m_positions, entry.first);
					position.m_nAvailable += position.m_nToday;
					position.m_nToday = 0;
					COrder order;
					order.m_accountId = position.m_accountId;
					order.m_strSecurity = position.m_strSecurity;
					Emit(next, EventKind::Position, order);
				}
				next.m_nSettledDay = nPreviousDay;
				bChanged = true;
			}
			if ((day.m_nClose <= nNow) && (day.m_nDay > next.m_nClosedDay))
			{
				for (auto& entry : next.m_orders)
				{
					if (IsActive(entry.second.m_status) && entry.second.m_nDay <= day.m_nDay)
					{
						Cancel(edit, entry.second, Status::Expired, entry.second.m_requestId);
					}
				}
				next.m_nClosedDay = day.m_nDay;
				bChanged = true;
			}
			nPreviousDay = day.m_nDay;
		}
		return bChanged ? Commit(edit) : CResult{};
	}
	CReply CSimulatedCounter::Execute(const CRequest& request, std::int64_t nNow)
	{
		CReply reply;
		reply.m_requestId = request.m_requestId;
		if ((Operation::Login > request.m_operation) || (Operation::Synchronize < request.m_operation))
		{
			reply.m_result = { Error::Unsupported, "operation not supported" };
			return reply;
		}
		reply.m_result = Advance(nNow);
		if (!reply.m_result.Ok())
		{
			return reply;
		}
		if (((0 != request.m_accountId) || ((Operation::Funds != request.m_operation) && (Operation::Positions != request.m_operation) && (Operation::Orders != request.m_operation) && (Operation::Trades != request.m_operation) && (Operation::Synchronize != request.m_operation))) && !m_state.m_accounts.contains(request.m_accountId))
		{
			reply.m_result = { Error::NotFound, "account not found" };
			return reply;
		}
		CStateEdit edit(m_state);
		CState& next = edit.State();
		CResult result;
		if ((Operation::Login == request.m_operation) || (Operation::Connect == request.m_operation) || (Operation::Disconnect == request.m_operation))
		{
			if (Operation::Disconnect == request.m_operation)
			{
				for (const auto& item : next.m_orders)
				{
					if ((request.m_accountId == item.second.m_accountId) && IsActive(item.second.m_status))
					{
						reply.m_result = Reject("active orders prevent disconnect");
						return reply;
					}
				}
			}
			edit.Save(&CState::m_accounts, request.m_accountId);
			next.m_accounts.at(request.m_accountId).m_bConnected = Operation::Disconnect != request.m_operation;
			COrder order;
			order.m_accountId = request.m_accountId;
			order.m_requestId = request.m_requestId;
			Emit(next, EventKind::Session, order);
		}
		else if (Operation::Submit == request.m_operation)
		{
			COrder order = request.m_order;
			order.m_accountId = request.m_accountId;
			order.m_requestId = request.m_requestId;
			result = Submit(edit, order, nNow, reply.m_orderId);
			if (result.Ok())
			{
				auto depth = m_depths.find(order.m_strSecurity);
				if (m_depths.end() != depth)
				{
					CDepth remaining = depth->second;
					result = Match(edit, remaining, nNow);
					if (result.Ok())
					{
						if (next.m_sequence != edit.Sequence())
						{
							edit.Save(&CState::m_consumedBooks, remaining.m_strSecurity);
							next.m_consumedBooks[remaining.m_strSecurity] = { remaining.m_nTime, remaining.m_sequence };
						}
						result = Commit(edit);
						if (result.Ok())
						{
							depth->second = std::move(remaining);
						}
					}
					if (!result.Ok())
					{
						edit.Rollback();
					}
					reply.m_result = result;
					reply.m_snapshot = Snapshot(request.m_accountId, nNow, request.m_operation, reply.m_orderId);
					return reply;
				}
			}
		}
		else if (Operation::Cancel == request.m_operation)
		{
			auto order = next.m_orders.find(request.m_orderId);
			if ((next.m_orders.end() == order) || (request.m_accountId != order->second.m_accountId))
			{
				result = { Error::NotFound, "order not found" };
			}
			else
			{
				Cancel(edit, order->second, Status::Cancelled, request.m_requestId);
				reply.m_orderId = request.m_orderId;
			}
		}
		else
		{
			reply.m_snapshot = Snapshot(request.m_accountId, nNow, request.m_operation, reply.m_orderId);
			if (request.m_afterSequence > m_state.m_sequence)
			{
				reply.m_result = { Error::Conflict, "event cursor ahead of watermark; request a fresh snapshot" };
				return reply;
			}
			if (Operation::Synchronize == request.m_operation)
			{
				reply.m_events = EventsAfter(request.m_afterSequence);
				if (0 != request.m_accountId)
				{
					std::erase_if(reply.m_events, [&request](const CEvent& event)
					{
						return request.m_accountId != event.m_accountId;
					});
				}
			}
			return reply;
		}
		if (!result.Ok())
		{
			edit.Rollback();
		}
		reply.m_result = result.Ok() ? Commit(edit) : result;
		reply.m_snapshot = Snapshot(request.m_accountId, nNow, request.m_operation, reply.m_orderId);
		return reply;
	}
	CResult CSimulatedCounter::OnDepth(CDepth depth, std::int64_t nNow)
	{
		if (!m_bMarketAvailable)
		{
			return { Error::Unavailable, "market disconnected" };
		}
		if (depth.m_nTime < m_nStartTime)
		{
			return Reject("depth predates counter recovery");
		}
		CResult result = Advance(nNow);
		if (!result.Ok())
		{
			return result;
		}
		const CCalendar* currentDay = Calendar(nNow);
		if (nullptr == currentDay || !InSession(*currentDay, nNow) || !InSession(*currentDay, depth.m_nTime))
		{
			return Reject("depth outside continuous trading session");
		}
		auto consumed = m_state.m_consumedBooks.find(depth.m_strSecurity);
		if (m_state.m_consumedBooks.end() != consumed && std::make_pair(depth.m_nTime, depth.m_sequence) <= consumed->second)
		{
			return Reject("book liquidity already consumed before recovery");
		}
		auto previous = m_depths.find(depth.m_strSecurity);
		if ((0 == depth.m_sequence) || (depth.m_nTime > nNow) || (MaxAgeMs < nNow - depth.m_nTime) || ((m_depths.end() != previous) && ((previous->second.m_sequence >= depth.m_sequence) || (previous->second.m_nTime > depth.m_nTime))))
		{
			return Reject("duplicate, out-of-order or stale depth");
		}
		std::erase_if(depth.m_bids, [](const CLevel& level)
		{
			return 0 == level.m_nQuantity;
		});
		std::erase_if(depth.m_asks, [](const CLevel& level)
		{
			return 0 == level.m_nQuantity;
		});
		for (const auto& levels : { depth.m_bids, depth.m_asks })
		{
			for (const auto& level : levels)
			{
				if ((0 >= level.m_nPrice) || (0 > level.m_nQuantity))
				{
					return Reject("invalid depth level");
				}
			}
		}
		std::sort(depth.m_asks.begin(), depth.m_asks.end(), [](const CLevel& left, const CLevel& right)
		{
			return left.m_nPrice < right.m_nPrice;
		});
		std::sort(depth.m_bids.begin(), depth.m_bids.end(), [](const CLevel& left, const CLevel& right)
		{
			return left.m_nPrice > right.m_nPrice;
		});
		CStateEdit edit(m_state);
		CState& next = edit.State();
		result = Match(edit, depth, nNow);
		if (result.Ok() && (next.m_sequence != edit.Sequence()))
		{
			edit.Save(&CState::m_consumedBooks, depth.m_strSecurity);
			next.m_consumedBooks[depth.m_strSecurity] = { depth.m_nTime, depth.m_sequence };
			result = Commit(edit);
		}
		if (result.Ok())
		{
			m_depths[depth.m_strSecurity] = std::move(depth);
		}
		return result;
	}
	void CSimulatedCounter::OnMarketState(bool bAvailable)
	{
		m_bMarketAvailable = bAvailable;
		if (!bAvailable)
		{
			m_depths.clear();
		}
	}
	CSnapshot CSimulatedCounter::Snapshot(Id nAccountId, std::int64_t nNow, Operation operation, Id nOrderId) const
	{
		CSnapshot snapshot;
		snapshot.m_bReady = m_bReady;
		snapshot.m_version = m_state.m_sequence;
		snapshot.m_nTime = nNow;
		for (const auto& item : m_state.m_accounts)
		{
			if ((0 == nAccountId) || (nAccountId == item.first))
			{
				snapshot.m_accounts.emplace_back(item.second);
			}
		}
		if (Operation::Synchronize == operation || Operation::Positions == operation)
		{
			for (const auto& item : m_state.m_positions)
			{
				if ((0 == nAccountId) || (nAccountId == item.second.m_accountId))
				{
					snapshot.m_positions.emplace_back(item.second);
				}
			}
		}
		if (Operation::Synchronize == operation || Operation::Orders == operation || Operation::Submit == operation || Operation::Cancel == operation)
		{
			for (const auto& item : m_state.m_orders)
			{
				if (((0 == nAccountId) || (nAccountId == item.second.m_accountId)) && (0 == nOrderId || nOrderId == item.first))
				{
					snapshot.m_orders.emplace_back(item.second);
				}
			}
		}
		if (Operation::Synchronize == operation || Operation::Trades == operation)
		{
			for (const auto& item : m_state.m_trades)
			{
				if ((0 == nAccountId) || (nAccountId == item.second.m_accountId))
				{
					snapshot.m_trades.emplace_back(item.second);
				}
			}
		}
		return snapshot;
	}
	std::vector<CEvent> CSimulatedCounter::EventsAfter(Id nSequence) const
	{
		if (nSequence >= m_state.m_events.size())
		{
			return {};
		}
		return { m_state.m_events.begin() + static_cast<std::ptrdiff_t>(nSequence), m_state.m_events.end() };
	}
} // namespace trading
