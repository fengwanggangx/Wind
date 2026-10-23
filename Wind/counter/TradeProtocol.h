#pragma once
#include "TradeTypes.h"
#include "TradingTypes.h"
#include "../request/request.h"
#include "../request/request.pb.h"
namespace trading::protocol
{
	void WriteOrder(const COrder& row, request::TradeOrderRecord& wire);
	void WriteTrade(const CTrade& row, request::TradeRecord& wire);
	void WritePosition(const CPosition& row, request::TradePositionRecord& wire);
	void WriteFunds(const CAccount& row, bool bReady, request::TradeAccountInfo& wire);
	void WriteEvent(const CEvent& event, request::TradeEventRecord& wire);
	COrderEvent OrderEvent(const CEvent& event);
	CTradeEvent TradeEvent(const CEvent& event);
	::CRequest Push(const CEvent& event, const request::TradeAccountRecord& account);
} // namespace trading::protocol
