#ifndef WIND_HQMARKET_MARKETTYPES_H
#define WIND_HQMARKET_MARKETTYPES_H

#include <string>

enum class Exchange
{
	unknown = 0,
	sse,
	szse,
	bse,
	hkex,
	cffex,
	shfe,
	dce,
	czce,
	ine,
	gfex,
	nasdaq,
	nyse,
	crypto
};

enum class Channel
{
	unknown = 0,
	quote,
	depth,
	trade,
	bar_1m,
	bar_1d,
	market_status
};

enum class MarketState
{
	unknown = 0,
	normal,
	suspended,
	delisted
};

struct CSecurity
{
	std::string m_strCode;
	std::string m_strName;
	Exchange m_market{ Exchange::unknown };
	MarketState m_status{ MarketState::normal };

	CSecurity() = default;
	CSecurity(const std::string& strCode, Exchange mk, MarketState status = MarketState::unknown);
	CSecurity(const CSecurity& arg);
	CSecurity& operator=(const CSecurity& arg);
	bool operator==(const CSecurity& arg) const;

	bool IsValid() const;
	std::string String() const;
};

std::string GetMarketString(Exchange exchange);
std::string GetChannelString(Channel channel);
std::string GetMarketStateString(MarketState status);
Exchange ParseMarket(const std::string& strExchange);
Channel ParseChannel(const std::string& strChannel);
MarketState ParseMarketState(const std::string& strStatus);
std::string FmtSecurityString(const std::string& strCode, Exchange mk);

struct CQuoteInfo
{
	CQuoteInfo() = default;
	CQuoteInfo(const std::string& strCode, Exchange mk, Channel channel) : m_security(strCode, mk), m_channel(channel)
	{
	}

	bool IsValid() const;
	std::string String() const;

	CSecurity m_security;
	Channel m_channel{ Channel::unknown };
};

#endif
