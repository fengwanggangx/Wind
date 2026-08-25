#include "CHQRequest.h"

CRequest* CHQRequest::Create(const std::string& strCommand, std::uint64_t nRequestId)
{
	CRequest* request = new CRequest();
	request->SetType(CRequest::Type::HQMARKET);
	request->SetCmd(strCommand);
	if (nRequestId != 0)
	{
		request->SetExtraData("request_id", std::to_string(nRequestId));
	}
	return request;
}

CRequest* CHQRequest::Subscribe(const std::string& strInstrument, const std::string& strChannel,
	std::uint64_t nRequestId)
{
	auto request = Create("subscribe", nRequestId);
	request->SetExtraData("instrument", strInstrument);
	request->SetExtraData("channel", strChannel);
	return request;
}

CRequest* CHQRequest::Unsubscribe(const std::string& strInstrument, const std::string& strChannel,
	std::uint64_t nRequestId)
{
	auto request = Create("unsubscribe", nRequestId);
	request->SetExtraData("instrument", strInstrument);
	request->SetExtraData("channel", strChannel);
	return request;
}

CRequest* CHQRequest::QueryQuote(const std::string& strInstrument, std::uint64_t nRequestId)
{
	auto request = Create("query_quote", nRequestId);
	request->SetExtraData("instrument", strInstrument);
	return request;
}

CRequest* CHQRequest::QueryBars(const std::string& strInstrument, const std::string& strChannel,
	std::int64_t nBeginTime, std::int64_t nEndTime, std::uint64_t nRequestId)
{
	auto request = Create("query_bars", nRequestId);
	request->SetExtraData("instrument", strInstrument);
	request->SetExtraData("channel", strChannel);
	request->SetExtraData("begin_time_ms", std::to_string(nBeginTime));
	request->SetExtraData("end_time_ms", std::to_string(nEndTime));
	return request;
}

CRequest* CHQRequest::Heartbeat(std::int64_t nClientTime, std::uint64_t nRequestId)
{
	auto request = Create("heartbeat", nRequestId);
	request->SetExtraData("client_time_ms", std::to_string(nClientTime));
	return request;
}
