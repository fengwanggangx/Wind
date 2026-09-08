#ifndef WIND_BUSINESS_REQUESTCENTER_H
#define WIND_BUSINESS_REQUESTCENTER_H

#include "../request/request.h"

#include <cstdint>
#include <string>

namespace net
{
	struct CNetEvent;
}

namespace request
{
	CRequest Auth(const std::string& strAccount, const std::string& strPassword);
	CRequest Subscription(const std::string& strInstrument, const std::string& strChannel);
	CRequest UnSubscription(const std::string& strInstrument, const std::string& strChannel);
	CRequest QueryQuote(const std::string& strInstrument);
	CRequest QueryBars(const std::string& strInstrument, const std::string& strChannel, std::int64_t nBeginTime, std::int64_t nEndTime);
	CRequest HeartBeat();
}

bool InitializeUserStorage();
int OnClientNetEvent(const net::CNetEvent& ev);

#endif
