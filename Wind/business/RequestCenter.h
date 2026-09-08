#ifndef WIND_BUSINESS_REQUESTCENTER_H
#define WIND_BUSINESS_REQUESTCENTER_H

#include "../request/request.h"
#include "../hqmarket/MarketTypes.h"

#include <cstdint>
#include <string>

namespace net
{
	struct CNetEvent;
}

class CSession;

namespace request
{
	bool IsAuthRequest(const CRequest& req);
	bool IsSubcriptionRequest(const CRequest& req);
	bool IsUnSubcriptionRequest(const CRequest& req);

	CRequest Auth(const std::string& strToken, const std::string& strPassword);
	CRequest Subscription(const market::CQuoteInfo& quote);
	CRequest UnSubscription(const market::CQuoteInfo& quote);
	CRequest QueryQuote(const market::CSecurity& security);
	CRequest QueryBars(const market::CSecurity& security, market::Channel channel, std::int64_t nBeginTime, std::int64_t nEndTime);
	CRequest HeartBeat();
}

bool InitializeUserStorage();
void InitializeRequestCenter(CSession* pSession);
int OnClientNetEvent(const net::CNetEvent& ev);

#endif
