#include "CHQRequest.h"

CRequest* CHQRequest::Create(const std::string& strCommand)
{
	CRequest* p = new CRequest();
	p->SetType(CRequest::Type::HQMARKET);
	p->SetCmd(strCommand);
	return p;
}

CRequest* CHQRequest::GetSubscribeRequest(const std::string& strInstrument, const std::string& strChannel, bool bSub)
{
	CRequest* p = Create(bSub ? "subscribe" : "unsubscribe");
	p->SetExtraData("security", strInstrument);
	p->SetExtraData("channel", strChannel);
	return p;
}

CRequest* CHQRequest::QueryQuote(const std::string& strInstrument)
{
	CRequest* p = Create("query_quote");
	p->SetExtraData("instrument", strInstrument);
	return p;
}

CRequest* CHQRequest::QueryBars(const std::string& strInstrument, const std::string& strChannel, std::int64_t nBeginTime, std::int64_t nEndTime)
{
	CRequest* p = Create("query_bars");
	p->SetExtraData("instrument", strInstrument);
	p->SetExtraData("channel", strChannel);
	p->SetExtraData("begin_time_ms", std::to_string(nBeginTime));
	p->SetExtraData("end_time_ms", std::to_string(nEndTime));
	return p;
}

CRequest* CHQRequest::Heartbeat(std::int64_t nClientTime)
{
	CRequest* p = Create("heartbeat");
	p->SetExtraData("client_time_ms", std::to_string(nClientTime));
	return p;
}
