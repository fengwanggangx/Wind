#ifndef WIND_SYSTEM_REQUESTCENTER_H
#define WIND_SYSTEM_REQUESTCENTER_H

#include "../request/request.h"
#include "MarketTypes.h"

#include <cstdint>
#include <string>

namespace request
{
	bool IsAuthRequest(const CString& req);
	bool IsRegisterAccountRequest(const CString& req);
	bool IsQuerySecurityRequest(const CString& req);

	CRequest Auth(const std::string& strAccount, const std::string& strPassword);
	CRequest Auth(const std::string& strToken);
	CRequest Subscription(const CQuoteInfo& quote);
	CRequest UnSubscription(const CQuoteInfo& quote);
	CRequest QueryQuote(const CSecurity& security);
	CRequest QueryBars(const CSecurity& security, Channel channel, std::int64_t nBeginTime, std::int64_t nEndTime);
	CRequest QuerySecurities();
	CRequest HeartBeat();
}

#endif
