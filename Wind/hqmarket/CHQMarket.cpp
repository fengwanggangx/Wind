#include "CHQMarket.h"
#include "../network/CTcpClient.h"
#include "../network/common_net.h"
#include <charconv>
#include <cstring>

namespace wire = hqmarket::market::v1;

namespace
{
	wire::Exchange ParseExchange(const std::string& value)
	{
		if ("SSE" == value)
		{
			return wire::SSE;
		}
		if ("SZSE" == value)
		{
			return wire::SZSE;
		}
		if ("BSE" == value)
		{
			return wire::BSE;
		}
		if ("HKEX" == value)
		{
			return wire::HKEX;
		}
		return wire::EXCHANGE_UNSPECIFIED;
	}

	bool ParseInstrument(const std::string& value, wire::Instrument* instrument)
	{
		if (nullptr == instrument)
		{
			return false;
		}
		std::size_t nDot = value.rfind('.');
		if ((std::string::npos == nDot) || (0 == nDot) || (value.size() <= nDot + 1))
		{
			return false;
		}
		instrument->set_symbol(value.substr(0, nDot));
		instrument->set_exchange(ParseExchange(value.substr(nDot + 1)));
		return wire::EXCHANGE_UNSPECIFIED != instrument->exchange();
	}

	wire::Channel ParseChannel(const std::string& value)
	{
		if ("quote" == value)
		{
			return wire::CHANNEL_QUOTE;
		}
		if ("depth" == value)
		{
			return wire::CHANNEL_DEPTH;
		}
		if ("bar_1m" == value)
		{
			return wire::CHANNEL_BAR_1M;
		}
		if ("bar_1d" == value)
		{
			return wire::CHANNEL_BAR_1D;
		}
		return wire::CHANNEL_UNSPECIFIED;
	}

	std::int64_t ParseInt64(const std::string& value)
	{
		std::int64_t result = 0;
		auto [pEnd, error] = std::from_chars(value.data(), value.data() + value.size(), result);
		return ((std::errc() == error) && (pEnd == value.data() + value.size())) ? result : 0;
	}

	std::string EncodeFrame(const std::string& payload)
	{
		std::uint32_t nSize = static_cast<std::uint32_t>(payload.size());
		std::string frame(4, '\0');
		frame[0] = static_cast<char>(nSize >> 24);
		frame[1] = static_cast<char>(nSize >> 16);
		frame[2] = static_cast<char>(nSize >> 8);
		frame[3] = static_cast<char>(nSize);
		frame += payload;
		return frame;
	}
}

CHQMarket::CHQMarket(net::CTcpClient* pTcpClient) : m_pTcpClient(pTcpClient)
{
}
CHQMarket::~CHQMarket()
{
	Stop();
}

bool CHQMarket::Initialize(const std::string& strToken)
{
	if ((nullptr == m_pTcpClient) || strToken.empty())
	{
		return false;
	}
	m_strToken = strToken;
	m_pTcpClient->RegisterHandler([this](const net::CNetEvent& ev)
	{
		return OnNetEvent(ev);
	});
	return true;
}

void CHQMarket::Stop()
{
	if (nullptr != m_pTcpClient)
	{
		m_pTcpClient->ClearHandlers();
		m_pTcpClient = nullptr;
	}
	std::lock_guard<std::mutex> lck(m_mtx_state);
	m_buffer.clear();
	m_bConnected = false;
	m_bAuthenticated = false;
}

bool CHQMarket::IsConnected() const
{
	return m_bConnected;
}

bool CHQMarket::IsAuthenticated() const
{
	return m_bAuthenticated;
}

void CHQMarket::RegisterHandler(_TyHandler handler)
{
	std::lock_guard<std::mutex> lck(m_mtx_state);
	m_handler = std::move(handler);
}

bool CHQMarket::SendRequest(CRequest* pRequest)
{
	std::unique_ptr<CRequest> pRequestHolder(pRequest);
	if ((nullptr == pRequest) || !m_bAuthenticated)
	{
		return false;
	}
	wire::MarketEnvelope envelope;
	if (!BuildEnvelope(*pRequest, envelope))
	{
		return false;
	}
	return SendEnvelope(envelope);
}

bool CHQMarket::BuildEnvelope(const CRequest& request, wire::MarketEnvelope& envelope)
{
	envelope.set_protocol_major(1);
	envelope.set_protocol_minor(0);
	std::string requestId = request.GetExtraData("request_id");
	envelope.set_request_id(requestId.empty() ? ++m_nRequestId : static_cast<std::uint64_t>(ParseInt64(requestId)));
	std::string command = request.GetCmd();
	if (("subscribe" == command) || ("unsubscribe" == command))
	{
		wire::Instrument instrument;
		if (!ParseInstrument(request.GetExtraData("instrument"), &instrument))
		{
			return false;
		}
		wire::Channel channel = ParseChannel(request.GetExtraData("channel"));
		if ((wire::CHANNEL_QUOTE != channel) && (wire::CHANNEL_DEPTH != channel))
		{
			return false;
		}
		if ("subscribe" == command)
		{
			envelope.set_type(wire::SUBSCRIBE_REQUEST);
			envelope.mutable_subscribe_request()->add_instruments()->CopyFrom(instrument);
			envelope.mutable_subscribe_request()->add_channels(channel);
		}
		else
		{
			envelope.set_type(wire::UNSUBSCRIBE_REQUEST);
			envelope.mutable_unsubscribe_request()->add_instruments()->CopyFrom(instrument);
			envelope.mutable_unsubscribe_request()->add_channels(channel);
		}
		return true;
	}
	if (("query_quote" == command) || ("query_bars" == command))
	{
		envelope.set_type(wire::QUERY_REQUEST);
		wire::QueryRequest* query = envelope.mutable_query_request();
		if (!ParseInstrument(request.GetExtraData("instrument"), query->mutable_instrument()))
		{
			return false;
		}
		query->set_channel("query_quote" == command ? wire::CHANNEL_QUOTE : ParseChannel(request.GetExtraData("channel")));
		if ((wire::CHANNEL_QUOTE != query->channel()) && (wire::CHANNEL_BAR_1M != query->channel()) && (wire::CHANNEL_BAR_1D != query->channel()))
		{
			return false;
		}
		query->set_begin_time_ms(ParseInt64(request.GetExtraData("begin_time_ms")));
		query->set_end_time_ms(ParseInt64(request.GetExtraData("end_time_ms")));
		return true;
	}
	if ("heartbeat" == command)
	{
		envelope.set_type(wire::HEARTBEAT);
		envelope.mutable_heartbeat()->set_client_time_ms(ParseInt64(request.GetExtraData("client_time_ms")));
		return true;
	}
	return false;
}

bool CHQMarket::SendEnvelope(const wire::MarketEnvelope& envelope)
{
	std::string payload;
	if (!envelope.SerializeToString(&payload))
	{
		return false;
	}
	std::string frame = EncodeFrame(payload);
	std::lock_guard<std::mutex> lck(m_mtx_state);
	return (nullptr != m_pTcpClient) && m_pTcpClient->Send(frame.data(), frame.size());
}

int CHQMarket::OnNetEvent(const net::CNetEvent& ev)
{
	if (net::em_event::connected == ev.m_event)
	{
		m_bConnected = true;
		SendAuthRequest();
		return 1;
	}
	if (net::em_event::request == ev.m_event)
	{
		if (nullptr != ev.m_request)
		{
			OnData(ev.m_request->GetReturnData("payload"));
		}
		return 1;
	}
	if ((net::em_event::disconnected != ev.m_event) && (net::em_event::error != ev.m_event) && (net::em_event::timeout != ev.m_event))
	{
		return 0;
	}
	m_bConnected = false;
	m_bAuthenticated = false;
	std::unique_ptr<CRequest> result = std::make_unique<CRequest>();
	result->SetType(CRequest::Type::HQMARKET);
	result->SetCmd("connection_closed");
	Dispatch(std::move(result));
	return 1;
}

void CHQMarket::OnData(const std::string& data)
{
	if (data.empty())
	{
		return;
	}
	std::size_t nLength = data.size();
	std::size_t nOldSize = m_buffer.size();
	m_buffer.resize(nOldSize + nLength);
	std::memcpy(m_buffer.data() + nOldSize, data.data(), nLength);
	std::size_t nOffset = 0;
	while (4 <= (m_buffer.size() - nOffset))
	{
		std::uint32_t nSize = (static_cast<std::uint32_t>(m_buffer[nOffset]) << 24) |
			(static_cast<std::uint32_t>(m_buffer[nOffset + 1]) << 16) |
			(static_cast<std::uint32_t>(m_buffer[nOffset + 2]) << 8) | m_buffer[nOffset + 3];
		if ((0 == nSize) || (8 * 1024 * 1024 < nSize))
		{
			m_buffer.clear();
			net::CNetEvent event(net::em_event::error);
			OnNetEvent(event);
			return;
		}
		if ((m_buffer.size() - nOffset - 4) < nSize)
		{
			break;
		}
		wire::MarketEnvelope envelope;
		if (envelope.ParseFromArray(m_buffer.data() + nOffset + 4, static_cast<int>(nSize)))
		{
			HandleEnvelope(envelope);
		}
		nOffset += 4 + nSize;
	}
	if (0 != nOffset)
	{
		m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(nOffset));
	}
}

void CHQMarket::SendAuthRequest()
{
	wire::MarketEnvelope auth;
	auth.set_protocol_major(1);
	auth.set_protocol_minor(0);
	auth.set_type(wire::AUTH_REQUEST);
	auth.set_request_id(++m_nRequestId);
	auth.mutable_auth_request()->set_token(m_strToken);
	SendEnvelope(auth);
}

void CHQMarket::HandleEnvelope(const wire::MarketEnvelope& envelope)
{
	if (wire::AUTH_RESPONSE == envelope.type())
	{
		m_bAuthenticated = envelope.auth_response().accepted();
	}
	std::unique_ptr<CRequest> result = std::make_unique<CRequest>();
	result->SetType(CRequest::Type::HQMARKET);
	result->SetCmd(wire::MessageType_Name(envelope.type()));
	result->SetReturnData("request_id", std::to_string(envelope.request_id()));
	result->SetReturnData("sequence", std::to_string(envelope.sequence()));
	std::string payload;
	envelope.SerializeToString(&payload);
	result->SetReturnData("payload", payload);
	if (wire::ERROR == envelope.type())
	{
		result->SetReturnData("error_code", std::to_string(envelope.error().code()));
		result->SetReturnData("error_message", envelope.error().message());
	}
	Dispatch(std::move(result));
}

void CHQMarket::Dispatch(std::unique_ptr<CRequest> request)
{
	_TyHandler handler;
	{
		std::lock_guard<std::mutex> lck(m_mtx_state);
		handler = m_handler;
	}
	if (handler)
	{
		handler(std::move(request));
	}
}
