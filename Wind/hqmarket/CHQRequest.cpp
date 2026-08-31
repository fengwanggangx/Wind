#include "CHQRequest.h"

CRequest* CHQRequest::Create(const std::string& strCommand)
{
	CRequest* request = new CRequest();
	request->SetType(CRequest::Type::HQMARKET);
	request->SetCmd(strCommand);
	return request;
}

CRequest* CHQRequest::GetSubscribeRequest(const std::string& strInstrument, const std::string& strChannel, bool bSub)
{
	auto request = Create(bSub ? "subscribe" : "unsubscribe");
	request->SetExtraData("instrument", strInstrument);
	request->SetExtraData("channel", strChannel);
	return request;
}

CRequest* CHQRequest::QueryQuote(const std::string& strInstrument)
{
	auto request = Create("query_quote");
	request->SetExtraData("instrument", strInstrument);
	return request;
}

CRequest* CHQRequest::QueryBars(const std::string& strInstrument, const std::string& strChannel,
	std::int64_t nBeginTime, std::int64_t nEndTime)
{
	auto request = Create("query_bars");
	request->SetExtraData("instrument", strInstrument);
	request->SetExtraData("channel", strChannel);
	request->SetExtraData("begin_time_ms", std::to_string(nBeginTime));
	request->SetExtraData("end_time_ms", std::to_string(nEndTime));
	return request;
}

CRequest* CHQRequest::Heartbeat(std::int64_t nClientTime)
{
	auto request = Create("heartbeat");
	request->SetExtraData("client_time_ms", std::to_string(nClientTime));
	return request;
}
