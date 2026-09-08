#include "request.h"

#include "request.pb.h"

#include <atomic>
#include "../common/utility.h"

namespace
{
	std::atomic_uint64_t NextRequestId{ 1 };
}

CRequest::CRequest() : m_data(std::make_unique<request::RequestData>())
{
	SetId(NextRequestId.fetch_add(1, std::memory_order_relaxed));
}

CRequest::~CRequest() = default;

CRequest::CRequest(const CRequest& arg) : m_data(std::make_unique<request::RequestData>(*arg.m_data)), m_connectionId(arg.m_connectionId)
{
}

CRequest& CRequest::operator=(const CRequest& arg)
{
	if (this != &arg)
	{
		*m_data = *arg.m_data;
		m_connectionId = arg.m_connectionId;
	}
	return *this;
}

CRequest::CRequest(CRequest&&) noexcept = default;
CRequest& CRequest::operator=(CRequest&&) noexcept = default;

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

void CRequest::SetType(Type type)
{
	m_data->set_type(static_cast<request::RequestType>(type));
}

std::string CRequest::GetCmd() const
{
	return m_data->cmd();
}

void CRequest::SetCmd(const std::string& strCmd)
{
	m_data->set_cmd(strCmd);
}

std::unordered_map<std::string, std::string> CRequest::GetExtraData() const
{
	return { m_data->extra().begin(), m_data->extra().end() };
}

std::string CRequest::GetExtraData(const std::string& strKey) const
{
	google::protobuf::Map<std::string, std::string>::const_iterator iter = m_data->extra().find(strKey);
	return m_data->extra().end() == iter ? std::string() : iter->second;
}

void CRequest::SetExtraData(const std::string& strKey, const std::string& strValue)
{
	(*m_data->mutable_extra())[strKey] = strValue;
}

std::unordered_map<std::string, std::string> CRequest::GetReturnData() const
{
	return { m_data->ret().begin(), m_data->ret().end() };
}

std::string CRequest::GetReturnData(const std::string& strKey) const
{
	google::protobuf::Map<std::string, std::string>::const_iterator iter = m_data->ret().find(strKey);
	return m_data->ret().end() == iter ? std::string() : iter->second;
}

void CRequest::SetReturnData(const std::string& strKey, const std::string& strValue)
{
	(*m_data->mutable_ret())[strKey] = strValue;
}

void CRequest::SetConnectionId(net::_TyConnectionId id)
{
	m_connectionId = id;
}

net::_TyConnectionId CRequest::GetConnectionId() const
{
	return m_connectionId;
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
