#include "CHQMarket.h"
#include "../business/RequestCenter.h"
#include "../network/CTcpClient.h"
#include "../network/common_net.h"
#include "../network/CNetTools.h"
#include "../common/defines.h"

namespace
{
	std::unique_ptr<CRequest> CloneRequest(const CRequest& request)
	{
		std::string data;
		if (!request.Serialize(&data))
		{
			return nullptr;
		}
		auto req = std::make_unique<CRequest>();
		if (!req->Deserialize(data))
		{
			return nullptr;
		}
		return req;
	}

	bool IsAccepted(const CRequest& request)
	{
		const std::string accepted = request.GetReturnData("accepted");
		return ("1" == accepted) || ("true" == accepted);
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

bool CHQMarket::SubscribeQuote(const market::CQuoteInfo& quote)
{
	std::string strKey = quote.m_security.String();
	std::string strChannel = market::GetChannelString(quote.m_channel);
	if (strKey.empty() || strChannel.empty())
	{
		return false;
	}
	return SendRequest(request::Subscription(strKey, strChannel));
}

void CHQMarket::RegisterHandler(_TyHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_state);
	m_handlers.emplace_back(std::move(handler));
}

bool CHQMarket::SendRequest(const CRequest& req)
{
	if (!m_bAuthenticated || (nullptr == m_pTcpClient))
	{
		return false;
	}
	return net::SendRequest(m_pTcpClient->GetId(), req);
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
			HandleRequest(*ev.m_request);
		}
		return 1;
	}
	if ((net::em_event::disconnected != ev.m_event) && (net::em_event::error != ev.m_event) && (net::em_event::timeout != ev.m_event))
	{
		return 0;
	}
	m_bConnected = false;
	m_bAuthenticated = false;
	auto result = std::make_unique<CRequest>();
	result->SetType(CRequest::Type::HQMARKET);
	result->SetCmd("connection_closed");
	Dispatch(std::move(result));
	return 1;
}

void CHQMarket::SendAuthRequest()
{
	SendRequest(request::Auth(m_strToken));
}

void CHQMarket::HandleRequest(const CRequest& request)
{
	std::string strCmd = request.GetCmd();
	if (("auth" == strCmd) || ("auth_response" == strCmd))
	{
		m_bAuthenticated = IsAccepted(request);
		if (!m_bAuthenticated)
		{
			std::cerr << "HQMarket declined Authenticate\n";
		}
		SubscribeQuote(market::CQuoteInfo("600010", market::Exchange::sse, market::Channel::quote));
		return;
	}
	
	if (!IsAuthenticated())
	{
		std::cerr << "HQMarket Not Authenticated\n";
		return;
	}
	std::string strErrCode = request.GetReturnData("error_code");
	std::string strErrMsg = request.GetReturnData("error_message");
	Dispatch(CloneRequest(request));
}

void CHQMarket::Dispatch(std::unique_ptr<CRequest> request)
{
	std::vector<_TyHandler> handlers;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		handlers = m_handlers;
	}

	ThreadPoolPtr->PushTask(task_priority::em_normal, 0, [req = std::move(request), handlers = std::move(handlers)]() {
		for (const auto& v : handlers)
		{
			v(*req);
		}
		});
	
}
