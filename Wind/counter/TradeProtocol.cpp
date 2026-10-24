#include "TradeProtocol.h"
namespace trading::protocol
{
	void WriteOrder(const trading::COrder& row, request::TradeOrderRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_client_order_id(row.m_clientId);
		wire.set_order_id(row.m_orderId);
		wire.set_request_id(row.m_requestId);
		wire.set_strategy_id(row.m_strategyId);
		wire.set_signal_id(row.m_signalId);
		wire.set_security(row.m_strSecurity);
		wire.set_side(static_cast<int>(row.m_side));
		wire.set_state(static_cast<int>(row.m_status));
		wire.set_price(row.m_nPrice);
		wire.set_price_scale(row.m_nPriceScale);
		wire.set_quantity(row.m_nQuantity);
		wire.set_filled_quantity(row.m_nFilled);
		wire.set_frozen(row.m_nFrozen);
		wire.set_turnover(row.m_nTurnover);
		wire.set_fee(row.m_nFee);
		wire.set_time_ms(row.m_nTime);
		wire.set_rule_version(row.m_strRuleVersion);
	}
	void WriteTrade(const trading::CTrade& row, request::TradeRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_trade_id(row.m_tradeId);
		wire.set_order_id(row.m_orderId);
		wire.set_security(row.m_strSecurity);
		wire.set_side(static_cast<int>(row.m_side));
		wire.set_price(row.m_nPrice);
		wire.set_price_scale(row.m_nPriceScale);
		wire.set_quantity(row.m_nQuantity);
		wire.set_fee(row.m_nFee);
		wire.set_time_ms(row.m_nTime);
	}
	void WritePosition(const trading::CPosition& row, request::TradePositionRecord& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_security(row.m_strSecurity);
		wire.set_total_quantity(row.m_nTotal);
		wire.set_available_quantity(row.m_nAvailable);
		wire.set_frozen_quantity(row.m_nFrozen);
		wire.set_today_quantity(row.m_nToday);
		wire.set_book_cost_cents(row.m_nCost);
	}
	CSecurity Security(const std::string& strValue)
	{
		std::size_t nDot = strValue.rfind('.');
		return std::string::npos == nDot ? CSecurity{} : CSecurity(strValue.substr(0, nDot), ParseMarket(strValue.substr(nDot + 1)));
	}
	void WriteFunds(const CAccount& row, bool bReady, request::TradeAccountInfo& wire)
	{
		wire.set_account_id(row.m_accountId);
		wire.set_balance(row.m_nBalance);
		wire.set_frozen(row.m_nFrozen);
		wire.set_available(row.m_nBalance - row.m_nFrozen);
		wire.set_available_snapshot(bReady && row.m_bConnected);
	}
	void WriteEvent(const CEvent& event, request::TradeEventRecord& wire)
	{
		wire.set_sequence(event.m_sequence);
		wire.set_request_id(event.m_requestId);
		wire.set_account_id(event.m_accountId);
		wire.set_kind(static_cast<int>(event.m_kind));
		wire.set_connected(event.m_account.m_bConnected);
		switch (event.m_kind)
		{
		case EventKind::Order:
			WriteOrder(event.m_order, *wire.mutable_order());
			break;
		case EventKind::Trade:
			WriteTrade(event.m_trade, *wire.mutable_trade());
			break;
		case EventKind::Position:
			WritePosition(event.m_position, *wire.mutable_position());
			break;
		case EventKind::Funds:
			WriteFunds(event.m_account, true, *wire.mutable_funds());
			break;
		default:
			break;
		}
	}
	COrderEvent OrderEvent(const CEvent& event)
	{
		const trading::COrder& row = event.m_order;
		COrderEvent order;
		order.m_sequence = event.m_sequence;
		order.m_accountId = event.m_accountId;
		order.m_strategyId = row.m_strategyId;
		order.m_signalId = row.m_signalId;
		order.m_clientOrderId = row.m_clientId;
		order.m_orderId = row.m_orderId;
		order.m_security = Security(row.m_strSecurity);
		order.m_quantity = row.m_nQuantity;
		order.m_filledQuantity = row.m_nFilled;
		order.m_eventTimeMs = row.m_nTime;
		switch (row.m_status)
		{
		case trading::Status::Accepted:
			order.m_status = OrderStatus::Accepted;
			break;
		case trading::Status::PartiallyFilled:
			order.m_status = OrderStatus::PartiallyFilled;
			break;
		case trading::Status::Filled:
			order.m_status = OrderStatus::Filled;
			break;
		case trading::Status::Cancelled:
			order.m_status = OrderStatus::Cancelled;
			break;
		case trading::Status::Expired:
			order.m_status = OrderStatus::Expired;
			break;
		default:
			order.m_status = OrderStatus::Rejected;
		}
		return order;
	}
	CTradeEvent TradeEvent(const CEvent& event)
	{
		CTradeEvent trade;
		trade.m_sequence = event.m_sequence;
		trade.m_accountId = event.m_accountId;
		trade.m_strategyId = event.m_order.m_strategyId;
		trade.m_signalId = event.m_order.m_signalId;
		trade.m_clientOrderId = event.m_order.m_clientId;
		trade.m_orderId = event.m_order.m_orderId;
		trade.m_strTradeId = std::to_string(event.m_trade.m_tradeId);
		trade.m_security = Security(event.m_trade.m_strSecurity);
		trade.m_side = trading::Side::Buy == event.m_trade.m_side ? OrderSide::Buy : OrderSide::Sell;
		trade.m_price = event.m_trade.m_nPrice;
		trade.m_nPriceScale = event.m_trade.m_nPriceScale;
		trade.m_quantity = event.m_trade.m_nQuantity;
		trade.m_tradeTimeMs = event.m_trade.m_nTime;
		return trade;
	}
	request::TradeAccountRecord AccountPayload(const CTradeAccountInfo& info)
	{
		request::TradeAccountRecord payload;
		payload.set_account_id(info.m_accountId);
		payload.set_name(info.m_strName);
		payload.set_account_type(TradeAccountType::Simulated == info.m_type ? request::ACCOUNT_SIMULATED : request::ACCOUNT_PRODUCTION);
		payload.set_initial_balance(info.m_nInitialBalance);
		switch (info.m_state)
		{
		case TradeAccountState::Disconnected:
			payload.set_state(request::ACCOUNT_DISCONNECTED);
			break;
		case TradeAccountState::Connected:
			payload.set_state(request::ACCOUNT_CONNECTED);
			break;
		case TradeAccountState::Error:
			payload.set_state(request::ACCOUNT_ERROR);
			break;
		}
		payload.set_number(info.m_strNumber);
		payload.set_strategy_count(info.m_nStrategyCount);
		payload.set_last_error(info.m_strLastError);
		payload.set_created_at(info.m_strCreatedAt);
		payload.set_updated_at(info.m_strUpdatedAt);
		payload.set_is_default(info.m_bDefault);
		return payload;
	}
	::CRequest Push(const CEvent& event, const CTradeAccountInfo& account)
	{
		::CRequest push;
		push.SetId(0);
		push.SetExtraData("sequence", std::to_string(event.m_sequence));
		push.SetExtraData("request_id", std::to_string(event.m_requestId));
		push.SetExtraData("account_id", std::to_string(event.m_accountId));
		switch (event.m_kind)
		{
		case trading::EventKind::Session:
			push.SetType(_TyRequest::TRADE_ACCOUNT_STATE_PUSH);
			push.SetData(AccountPayload(account));
			break;
		case trading::EventKind::Order:
		{
			push.SetType(_TyRequest::TRADE_ORDER_PUSH);
			request::TradeOrderList rows;
			WriteOrder(event.m_order, *rows.add_orders());
			push.SetData(rows);
		}
		break;
		case trading::EventKind::Trade:
		{
			push.SetType(_TyRequest::TRADE_TRADE_PUSH);
			request::TradeList rows;
			WriteTrade(event.m_trade, *rows.add_trades());
			push.SetData(rows);
		}
		break;
		case trading::EventKind::Funds:
		{
			push.SetType(_TyRequest::TRADE_ACCOUNT_INFO_PUSH);
			request::TradeAccountInfoList rows;
			WriteFunds(event.m_account, true, *rows.add_accounts());
			push.SetData(rows);
		}
		break;
		case trading::EventKind::Position:
		{
			push.SetType(_TyRequest::TRADE_POSITION_PUSH);
			request::TradePositionList rows;
			WritePosition(event.m_position, *rows.add_positions());
			push.SetData(rows);
		}
		break;
		}
		return push;
	}
} // namespace trading::protocol
