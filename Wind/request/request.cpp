#include "request.h"

#include "../common/utility.h"
#include "request.pb.h"
#include "v1/market.pb.h"

#include <atomic>

CRequest::CRequest() : m_arena(std::make_unique<google::protobuf::Arena>())
{
	m_data = google::protobuf::Arena::CreateMessage<_TyReqData>(m_arena.get());

	std::atomic<_TyRequestId> s_id{ 1 };
	SetId(s_id.fetch_add(1, std::memory_order_relaxed));
}

CRequest::~CRequest() = default;

CRequest::CRequest(CRequest&& arg) noexcept = default;

CRequest& CRequest::operator=(CRequest&& arg) noexcept = default;

CRequest::CRequest(const CRequest& arg) : CRequest()
{
	*this = arg;
}

CRequest& CRequest::operator=(const CRequest& arg)
{
	if (this != &arg)
	{
		m_data->CopyFrom(*arg.m_data);
		m_connection_id = arg.m_connection_id;
	}
	return *this;
}

_TyRequestId CRequest::GetId() const
{
	return m_data->id();
}

void CRequest::SetId(_TyRequestId id)
{
	m_data->set_id(id);
}

CRequest::Type CRequest::GetType() const
{
	return static_cast<Type>(m_data->type());
}

void CRequest::SetType(Type t)
{
	m_data->set_type(static_cast<request::RequestType>(t));
}

std::string CRequest::GetCmd() const
{
	return m_data->cmd();
}

void CRequest::SetCmd(const std::string& strCmd)
{
	m_data->set_cmd(strCmd);
}

std::string CRequest::GetExtraData(const std::string& strKey) const
{
	const auto mIter = m_data->extra().find(strKey);
	return m_data->extra().end() == mIter ? std::string() : mIter->second;
}

std::unordered_map<std::string, std::string> CRequest::GetExtraData() const
{
	return { m_data->extra().begin(), m_data->extra().end() };
}

void CRequest::SetExtraData(const std::string& strKey, const std::string& strValue)
{
	(*m_data->mutable_extra())[strKey] = strValue;
}

std::string CRequest::GetReturnData(const std::string& strKey) const
{
	const auto mIter = m_data->ret().find(strKey);
	return m_data->ret().end() == mIter ? std::string() : mIter->second;
}

std::unordered_map<std::string, std::string> CRequest::GetReturnData() const
{
	return { m_data->ret().begin(), m_data->ret().end() };
}

void CRequest::SetReturnData(const std::string& strKey, const std::string& strValue)
{
	(*m_data->mutable_ret())[strKey] = strValue;
}

void CRequest::SetData(const _TySubscriptionAck& value)
{
	m_data->mutable_subscription_ack()->CopyFrom(value);
}

void CRequest::SetData(const _TyQuoteData& value)
{
	m_data->mutable_quote()->CopyFrom(value);
}

void CRequest::SetData(const _TyDepthData& value)
{
	m_data->mutable_depth()->CopyFrom(value);
}

void CRequest::SetData(const _TyQueryResponse& value)
{
	m_data->mutable_query_response()->CopyFrom(value);
}

void CRequest::SetData(const _TyStrategyInfo& value)
{
	m_data->mutable_strategy()->CopyFrom(value);
}

void CRequest::SetData(const _TyStrategyList& value)
{
	m_data->mutable_strategy_list()->CopyFrom(value);
}

const _TyReqData& CRequest::GetData() const
{
	return *m_data;
}

void CRequest::SetConnectionId(net::_TyConnectionId id)
{
	m_connection_id = id;
}

net::_TyConnectionId CRequest::GetConnectionId() const
{
	return m_connection_id;
}

bool CRequest::Serialize(std::string* pOutput) const
{
	return (nullptr != pOutput) && m_data->SerializeToString(pOutput);
}

bool CRequest::Deserialize(const std::string& strData)
{
	return m_data->ParseFromString(strData);
}

std::optional<std::pair<int, std::string>> CRequest::GetErrorInfo() const
{
	std::string strErrorCode = GetReturnData("error_code");
	if (strErrorCode.empty())
	{
		return std::nullopt;
	}

	int nCode = 0;
	utility::to_number(strErrorCode, nCode);
	return std::pair<int, std::string>{ nCode, GetReturnData("error_message") };
}
