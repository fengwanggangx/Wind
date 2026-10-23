#include "TradeProtocol.h"
#include <iostream>
#include <stdexcept>
namespace
{
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
	try
	{
		trading::CEvent event;
		event.m_accountId = 7;
		event.m_requestId = 20;
		event.m_sequence = 40;
		event.m_account = { 7, 100000, 10000, true, true };
		event.m_order.m_accountId = 7;
		event.m_order.m_orderId = 30;
		event.m_order.m_clientId = 10;
		event.m_order.m_requestId = 11;
		event.m_order.m_strSecurity = "600000.SSE";
		event.m_order.m_status = trading::Status::Cancelled;
		event.m_trade.m_accountId = 7;
		event.m_trade.m_orderId = 30;
		event.m_trade.m_tradeId = 50;
		event.m_trade.m_strSecurity = "600000.SSE";
		event.m_position.m_accountId = 7;
		event.m_position.m_strSecurity = "600000.SSE";
		request::TradeAccountRecord account;
		account.set_account_id(7);
		for (trading::EventKind kind : { trading::EventKind::Session, trading::EventKind::Order, trading::EventKind::Trade, trading::EventKind::Funds, trading::EventKind::Position })
		{
			event.m_kind = kind;
			CRequest push = trading::protocol::Push(event, account);
			Check(0 == push.GetId() && "40" == push.GetExtraData("sequence") && "20" == push.GetExtraData("request_id"), "push correlation");
			request::TradeEventRecord replay;
			trading::protocol::WriteEvent(event, replay);
			Check(40 == replay.sequence() && 20 == replay.request_id() && 7 == replay.account_id(), "replay correlation");
			switch (kind)
			{
			case trading::EventKind::Order:
				Check(_TyRequest::TRADE_ORDER_PUSH == push.GetType() && replay.order().SerializeAsString() == push.GetData().trade_orders().orders(0).SerializeAsString(), "live and replay order match");
				Check(11 == replay.order().request_id() && 20 == replay.request_id() && OrderStatus::Cancelled == trading::protocol::OrderEvent(event).m_status, "cancel correlation preserves submit request");
				break;
			case trading::EventKind::Trade:
				Check(_TyRequest::TRADE_TRADE_PUSH == push.GetType() && replay.trade().SerializeAsString() == push.GetData().trades().trades(0).SerializeAsString(), "live and replay trade match");
				break;
			case trading::EventKind::Position:
				Check(_TyRequest::TRADE_POSITION_PUSH == push.GetType() && replay.position().SerializeAsString() == push.GetData().trade_positions().positions(0).SerializeAsString(), "live and replay position match");
				break;
			case trading::EventKind::Funds:
				Check(_TyRequest::TRADE_ACCOUNT_INFO_PUSH == push.GetType() && replay.funds().SerializeAsString() == push.GetData().account_info_list().accounts(0).SerializeAsString() && 90000 == replay.funds().available(), "live and replay funds match");
				break;
			case trading::EventKind::Session:
				Check(_TyRequest::TRADE_ACCOUNT_STATE_PUSH == push.GetType(), "session push type");
				break;
			}
		}
		std::cout << "counter protocol tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
