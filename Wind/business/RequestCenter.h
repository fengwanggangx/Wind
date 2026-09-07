#ifndef WIND_BUSINESS_REQUESTCENTER_H
#define WIND_BUSINESS_REQUESTCENTER_H

namespace net
{
	struct CNetEvent;
}

bool InitializeUserStorage();
int OnClientNetEvent(const net::CNetEvent& ev);

#endif
