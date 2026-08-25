#include "CHQMarket.h"
#include "../network/CTcpClient.h"
#include "../network/common_net.h"

namespace
{
	std::unique_ptr<CRequest> CloneRequest(const CRequest& request)
	{
		std::string data;
		if (!request.Serialize(&data))
		{
			return nullptr;
		}
		auto result = std::make_unique<CRequest>();
		if (!result->Deserialize(data))
		{
			return nullptr;
		}
		return result;
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

void CHQMarket::RegisterHandler(_TyHandler handler)
{
	std::lock_guard<std::mutex> lck(m_mtx_state);
	m_handler = std::move(handler);
}

bool CHQMarket::SendRequest(CRequest* pRequest)
{
	std::unique_ptr<CRequest> request(pRequest);
	if ((nullptr == request) || !m_bAuthenticated || (nullptr == m_pTcpClient))
	{
		return false;
	}
	if (request->GetExtraData("request_id").empty())
	{
		request->SetExtraData("request_id", std::to_string(++m_nRequestId));
	}
	return m_pTcpClient->SendRequest(request.get());
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
	if (nullptr == m_pTcpClient)
	{
		return;
	}
	CRequest request;
	request.SetType(CRequest::Type::HQMARKET);
	request.SetCmd("auth");
	request.SetExtraData("token", m_strToken);
	request.SetExtraData("request_id", std::to_string(++m_nRequestId));
	m_pTcpClient->SendRequest(&request);
}

void CHQMarket::HandleRequest(const CRequest& request)
{
	if (("auth" == request.GetCmd()) || ("auth_response" == request.GetCmd()))
	{
		m_bAuthenticated = IsAccepted(request);
	}
	Dispatch(CloneRequest(request));
}

void CHQMarket::Dispatch(std::unique_ptr<CRequest> request)
{
	if (nullptr == request)
	{
		return;
	}
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
