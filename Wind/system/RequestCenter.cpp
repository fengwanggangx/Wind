#include "RequestCenter.h"

#include <chrono>

namespace request
{
	CRequest Auth(const std::string& strToken, const std::string& strPassword)
	{
		CRequest req;
		req.SetType(CRequest::Type::QUERY_AUTH);
		req.SetCmd("auth");
		req.SetExtraData("token", strToken);
		req.SetExtraData("password", strPassword);
		return req;
	}

	CRequest Subscription(const market::CQuoteInfo& quote)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("subscribe");
		req.SetExtraData("security", quote.m_security.String());
		req.SetExtraData("channel", market::GetChannelString(quote.m_channel));
		return req;
	}

	CRequest UnSubscription(const market::CQuoteInfo& quote)
	{
		CRequest req = Subscription(quote);
		req.SetCmd("unsubscribe");
		return req;
	}

	CRequest QueryQuote(const market::CSecurity& security)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("query_quote");
		req.SetExtraData("security", security.String());
		return req;
	}

	CRequest QueryBars(const market::CSecurity& security, market::Channel channel, std::int64_t nBeginTime, std::int64_t nEndTime)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("query_bars");
		req.SetExtraData("security", security.String());
		req.SetExtraData("channel", market::GetChannelString(channel));
		req.SetExtraData("begin_time_ms", std::to_string(nBeginTime));
		req.SetExtraData("end_time_ms", std::to_string(nEndTime));
		return req;
	}

	CRequest HeartBeat()
	{
		CRequest req;
		req.SetType(CRequest::Type::HEARTBEAT);
		req.SetCmd("heartbeat");
		std::int64_t nClientTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		req.SetExtraData("client_time_ms", std::to_string(nClientTime));
		return req;
	}
} // namespace request
