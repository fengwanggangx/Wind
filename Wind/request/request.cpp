#include "request.pb.h"
#include "request.h"
#include <atomic>
#include <utility>

namespace
{
	constexpr const char* DataTypeKey = "data_type";
	constexpr const char* DataPayloadKey = "data";
}

CData::CData(std::string type, std::string payload) : m_type(std::move(type)), m_payload(std::move(payload))
{
}

const std::string& CData::GetType() const
{
	return m_type;
}

const std::string& CData::GetPayload() const
{
	return m_payload;
}

static request::RequestType ToProtoType(CRequest::Type type)
{
	switch (type)
	{
	case CRequest::Type::QUERY_AUTH:
		return request::RequestType::QUERY_AUTH;
	case CRequest::Type::QUERY_USERINFO:
		return request::RequestType::QUERY_USERINFO;
	case CRequest::Type::UPDATE_AUTH:
		return request::RequestType::UPDATE_AUTH;
	case CRequest::Type::UPDAT_PRODUCT:
		return request::RequestType::UPDAT_PRODUCT;
	case CRequest::Type::HQMARKET:
		return request::RequestType::HQMARKET;
	case CRequest::Type::UNKNOWN:
	default:
		return request::RequestType::UNKNOWN;
	}
}

CRequest::CRequest() : m_arena(std::make_unique<google::protobuf::Arena>())
{
	static std::atomic_uint64_t s_id{1};
	m_data = google::protobuf::Arena::CreateMessage<request::RequestData>(m_arena.get());
	m_data->set_id(s_id.fetch_add(1, std::memory_order_relaxed));
}

CRequest::~CRequest() = default;

bool CRequest::Serialize(std::string* output) const
{
	if (nullptr == output)
	{
		return false;
	}
	return m_data->SerializeToString(output);
}

bool CRequest::Deserialize(const std::string& data)
{
	if (nullptr == m_data)
	{
		return false;
	}
	if (!m_data->ParseFromString(data))
	{
		return false;
	}
	google::protobuf::Map<std::string, std::string>::const_iterator type = m_data->ret().find(DataTypeKey);
	google::protobuf::Map<std::string, std::string>::const_iterator payload = m_data->ret().find(DataPayloadKey);
	if ((m_data->ret().end() != type) && (m_data->ret().end() != payload))
	{
		m_cdata = std::make_unique<CData>(type->second, payload->second);
	}
	else
	{
		m_cdata.reset();
	}
	return true;
}

void CRequest::SetConnectionId(net::_TyConnectionId id)
{
	m_connection_id = id;
}

net::_TyConnectionId CRequest::GetConnectionId() const
{
	return m_connection_id;
}

void CRequest::SetType(CRequest::Type type)
{
	if (nullptr == m_data)
	{
		return;
	}
	m_data->set_type(static_cast<request::RequestType>(ToProtoType(type)));
}

void CRequest::SetCmd(const std::string& strCmd)
{
	if (nullptr == m_data)
	{
		return;
	}
	m_data->set_cmd(strCmd);
}

void CRequest::SetExtraData(const std::string& strKey, const std::string& strVal)
{
	if (nullptr == m_data)
	{
		return;
	}
	(*m_data->mutable_extra())[strKey] = strVal;
}

void CRequest::SetReturnData(const std::string& strKey, const std::string& strVal)
{
	if (nullptr == m_data)
	{
		return;
	}
	(*(m_data->mutable_ret()))[strKey] = strVal;
}

void CRequest::SetData(std::unique_ptr<CData> data)
{
	m_cdata = std::move(data);
	if (nullptr == m_data)
	{
		return;
	}
	if (nullptr == m_cdata)
	{
		m_data->mutable_ret()->erase(DataTypeKey);
		m_data->mutable_ret()->erase(DataPayloadKey);
		return;
	}
	(*(m_data->mutable_ret()))[DataTypeKey] = m_cdata->GetType();
	(*(m_data->mutable_ret()))[DataPayloadKey] = m_cdata->GetPayload();
}

const CData* CRequest::GetData() const
{
	return m_cdata.get();
}

std::uint64_t CRequest::GetId() const
{
	return (nullptr == m_data) ? 0 : m_data->id();
}

void CRequest::SetId(std::uint64_t nId)
{
	if (nullptr != m_data)
	{
		m_data->set_id(nId);
	}
}

CRequest::Type CRequest::GetType() const
{
	if (nullptr == m_data)
	{
		return CRequest::Type::UNKNOWN;
	}
	return static_cast<CRequest::Type>(m_data->type());
}

std::string CRequest::GetCmd() const
{
	if (nullptr == m_data)
	{
		return "";
	}
	return m_data->cmd();
}

std::unordered_map<std::string, std::string> CRequest::GetExtraData() const
{
	if (nullptr == m_data)
	{
		return {};
	}
	return {m_data->extra().begin(), m_data->extra().end()};
}

std::string CRequest::GetExtraData(const std::string& strKey) const
{
	std::unordered_map<std::string, std::string> data = GetExtraData();
	std::unordered_map<std::string, std::string>::const_iterator iter = data.find(strKey);
	return data.end() == iter ? "" : iter->second;
}

std::string CRequest::GetReturnData(const std::string& strKey) const
{
	std::unordered_map<std::string, std::string> data = GetReturnData();
	std::unordered_map<std::string, std::string>::const_iterator iter = data.find(strKey);
	return data.end() == iter ? "" : iter->second;
}

std::unordered_map<std::string, std::string> CRequest::GetReturnData() const
{
	if (nullptr == m_data)
	{
		return {};
	}
	return {m_data->ret().begin(), m_data->ret().end()};
}
