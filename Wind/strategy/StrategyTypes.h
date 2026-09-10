#ifndef WIND_STRATEGY_STRATEGYTYPES_H
#define WIND_STRATEGY_STRATEGYTYPES_H

#include "../request/MarketTypes.h"
#include "../request/v1/market.pb.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using _TyStrategyId = std::uint64_t;
using _TySignalId = std::uint64_t;
using _TyOrderId = std::uint64_t;
using _TyClientOrderId = std::uint64_t;
using _TyTimerId = std::uint64_t;

using _TyQuoteData = hqmarket::market::v1::QuoteData;
using _TyDepthData = hqmarket::market::v1::DepthData;
using _TyMarketTradeData = hqmarket::market::v1::TradeData;
using _TyBarData = hqmarket::market::v1::BarData;

enum class StrategyState
{
	Created,
	Initialized,
	Starting,
	Running,
	Pausing,
	Paused,
	Stopping,
	Stopped,
	Faulted
};

enum class OrderSide
{
	Unknown,
	Buy,
	Sell
};

enum class OrderType
{
	Market,
	Limit
};

enum class PositionEffect
{
	Unknown,
	Open,
	Close,
	CloseToday
};

enum class OrderStatus
{
	Unknown,
	Pending,
	Accepted,
	Rejected,
	PartiallyFilled,
	Filled,
	Cancelling,
	Cancelled
};

enum class LogLevel
{
	Debug,
	Info,
	Warning,
	Error
};

struct CStrategyConfig
{
	_TyStrategyId m_id{ 0 };
	std::string m_strType;
	std::string m_strName;
	std::unordered_map<std::string, std::string> m_parameters;
	std::vector<market::CQuoteInfo> m_subscriptions;
	std::size_t m_eventQueueLimit{ 4096 };
	bool m_bAutoStart{ false };

	bool IsValid() const;
};

struct COrderIntent
{
	_TyStrategyId m_strategyId{ 0 };
	_TySignalId m_signalId{ 0 };
	market::CSecurity m_security;
	OrderSide m_side{ OrderSide::Unknown };
	OrderType m_orderType{ OrderType::Limit };
	PositionEffect m_positionEffect{ PositionEffect::Unknown };
	std::int64_t m_price{ 0 };
	std::int64_t m_quantity{ 0 };
	std::string m_strReason;

	bool IsValid() const;
};

struct COrderSubmitResult
{
	bool m_bAccepted{ false };
	bool m_bRetryable{ false };
	_TyClientOrderId m_clientOrderId{ 0 };
	_TyOrderId m_orderId{ 0 };
	std::string m_strReason;
};

struct COrderEvent
{
	_TyStrategyId m_strategyId{ 0 };
	_TySignalId m_signalId{ 0 };
	_TyClientOrderId m_clientOrderId{ 0 };
	_TyOrderId m_orderId{ 0 };
	market::CSecurity m_security;
	OrderStatus m_status{ OrderStatus::Unknown };
	std::int64_t m_quantity{ 0 };
	std::int64_t m_filledQuantity{ 0 };
	std::int64_t m_eventTimeMs{ 0 };
	std::string m_strReason;
};

struct CTradeEvent
{
	_TyStrategyId m_strategyId{ 0 };
	_TySignalId m_signalId{ 0 };
	_TyClientOrderId m_clientOrderId{ 0 };
	_TyOrderId m_orderId{ 0 };
	std::string m_strTradeId;
	market::CSecurity m_security;
	OrderSide m_side{ OrderSide::Unknown };
	std::int64_t m_price{ 0 };
	std::int64_t m_quantity{ 0 };
	std::int64_t m_tradeTimeMs{ 0 };
};

struct CTimerEvent
{
	_TyTimerId m_timerId{ 0 };
	std::chrono::steady_clock::time_point m_fireTime;
};

struct CPositionSnapshot
{
	market::CSecurity m_security;
	std::int64_t m_totalQuantity{ 0 };
	std::int64_t m_availableQuantity{ 0 };
	std::int64_t m_frozenQuantity{ 0 };
	std::int64_t m_averageCost{ 0 };
};

struct CAccountSnapshot
{
	std::int64_t m_balance{ 0 };
	std::int64_t m_available{ 0 };
	std::int64_t m_frozen{ 0 };
	std::int64_t m_marketValue{ 0 };
};

struct CStrategySnapshot
{
	_TyStrategyId m_strategyId{ 0 };
	std::string m_strType;
	std::string m_strName;
	StrategyState m_state{ StrategyState::Created };
	bool m_bMarketAvailable{ false };
	std::size_t m_queueLength{ 0 };
	std::size_t m_activeOrderCount{ 0 };
	std::uint64_t m_lastEventSequence{ 0 };
	std::uint64_t m_droppedMarketEvents{ 0 };
	std::int64_t m_lastCallbackDurationUs{ 0 };
	std::int64_t m_maxCallbackDurationUs{ 0 };
	std::string m_strLastError;
};

#endif
