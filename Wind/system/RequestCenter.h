#ifndef WIND_SYSTEM_REQUESTCENTER_H
#define WIND_SYSTEM_REQUESTCENTER_H

#include "../request/request.h"
#include "../hqmarket/MarketTypes.h"

#include <cstdint>
#include <string>

namespace request
{
	CRequest Auth(const std::string& strToken, const std::string& strPassword);
	CRequest Subscription(const market::CQuoteInfo& quote);
	CRequest UnSubscription(const market::CQuoteInfo& quote);
	CRequest QueryQuote(const market::CSecurity& security);
	CRequest QueryBars(const market::CSecurity& security, market::Channel channel, std::int64_t nBeginTime, std::int64_t nEndTime);
	CRequest HeartBeat();
}

#endif
