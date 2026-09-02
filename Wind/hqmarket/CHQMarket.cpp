#include "CHQMarket.h"
#include "CHQRequest.h"
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

bool CHQMarket::SubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel)
{
	std::string strKey = FmtSecurityString(strCode, mk);
	std::string strChannel = market::GetChannelString(channel);
	if (strKey.empty() || strChannel.empty())
	{
		return false;
	}
	std::unique_ptr<CRequest> req(CHQRequest::GetSubscribeRequest(strKey, strChannel, true));
	return SendRequest(*req);
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
	CRequest request;
	request.SetType(CRequest::Type::HQMARKET);
	request.SetCmd("auth");
	request.SetExtraData("token", m_strToken);
	SendRequest(request);
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
		SubscribeQuote("600010", market::Exchange::sse, market::Channel::quote);
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
