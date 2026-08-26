#include "request.pb.h"
#include "request.h"
#include "../hqmarket/v1/market.pb.h"
#include <atomic>
#include <utility>

namespace
{
	constexpr const char* DataTypeKey = "data_type";
	constexpr const char* DataPayloadKey = "data";
}

namespace
{
#define FOR_EACH_DATA_TYPE(Action) \
	Action("hqmarket.market.v1.AuthRequest", hqmarket::market::v1::AuthRequest) \
	Action("hqmarket.market.v1.AuthResponse", hqmarket::market::v1::AuthResponse) \
	Action("hqmarket.market.v1.SubscribeRequest", hqmarket::market::v1::SubscribeRequest) \
	Action("hqmarket.market.v1.UnsubscribeRequest", hqmarket::market::v1::UnsubscribeRequest) \
	Action("hqmarket.market.v1.SubscriptionAck", hqmarket::market::v1::SubscriptionAck) \
	Action("hqmarket.market.v1.QuoteData", hqmarket::market::v1::QuoteData) \
	Action("hqmarket.market.v1.DepthData", hqmarket::market::v1::DepthData) \
	Action("hqmarket.market.v1.TradeData", hqmarket::market::v1::TradeData) \
	Action("hqmarket.market.v1.BarData", hqmarket::market::v1::BarData) \
	Action("hqmarket.market.v1.MarketStatusData", hqmarket::market::v1::MarketStatusData) \
	Action("hqmarket.market.v1.HeartbeatData", hqmarket::market::v1::HeartbeatData) \
	Action("hqmarket.market.v1.ProviderStatusData", hqmarket::market::v1::ProviderStatusData) \
	Action("hqmarket.market.v1.ErrorData", hqmarket::market::v1::ErrorData) \
	Action("hqmarket.market.v1.QueryRequest", hqmarket::market::v1::QueryRequest) \
	Action("hqmarket.market.v1.QueryResponse", hqmarket::market::v1::QueryResponse)
}

CData::CData(std::string type, void* pData) : m_type(std::move(type)), m_pData(pData)
{
}

CData::~CData()
{
	Reset();
}

void CData::Reset()
{
#define DELETE_DATA(TypeName, DataType) \
	if (TypeName == m_type) \
	{ \
		delete static_cast<DataType*>(m_pData); \
		m_pData = nullptr; \
		m_type.clear(); \
		return; \
	}
	FOR_EACH_DATA_TYPE(DELETE_DATA)
#undef DELETE_DATA
	m_pData = nullptr;
	m_type.clear();
}

const std::string& CData::GetType() const
{
	return m_type;
}

void* CData::GetData()
{
	return m_pData;
}

const void* CData::GetData() const
{
	return m_pData;
}

bool CData::Serialize(std::string* output) const
{
	if ((nullptr == output) || (nullptr == m_pData))
	{
		return false;
	}
#define SERIALIZE_DATA(TypeName, DataType) \
	if (TypeName == m_type) \
	{ \
		return static_cast<const DataType*>(m_pData)->SerializeToString(output); \
	}
	FOR_EACH_DATA_TYPE(SERIALIZE_DATA)
#undef SERIALIZE_DATA
	return false;
}

bool CData::Deserialize(const std::string& type, const std::string& payload)
{
#define DESERIALIZE_DATA(TypeName, DataType) \
	if (TypeName == type) \
	{ \
		auto data = std::make_unique<DataType>(); \
		if (!data->ParseFromString(payload)) \
		{ \
			return false; \
		} \
		Reset(); \
		m_type = type; \
		m_pData = data.release(); \
		return true; \
	}
	FOR_EACH_DATA_TYPE(DESERIALIZE_DATA)
#undef DESERIALIZE_DATA
	return false;
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
	m_data = google::protobuf::Arena::CreateMessage<request::RequestData>(m_arena.get());

	static std::atomic_uint64_t s_id{ 1 };
	SetId(s_id.fetch_add(1, std::memory_order_relaxed));
}

CRequest::~CRequest() = default;

bool CRequest::Serialize(std::string* output) const
{
	if ((nullptr == output) || (nullptr == m_data))
	{
		return false;
	}
	if (nullptr != m_cdata)
	{
		std::string payload;
		if (!m_cdata->Serialize(&payload))
		{
			return false;
		}
		(*(m_data->mutable_ret()))[DataTypeKey] = m_cdata->GetType();
		(*(m_data->mutable_ret()))[DataPayloadKey] = std::move(payload);
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
		auto cdata = std::make_unique<CData>();
		if (!cdata->Deserialize(type->second, payload->second))
		{
			return false;
		}
		m_cdata = std::move(cdata);
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
	std::string payload;
	if (!m_cdata->Serialize(&payload))
	{
		m_cdata.reset();
		m_data->mutable_ret()->erase(DataTypeKey);
		m_data->mutable_ret()->erase(DataPayloadKey);
		return;
	}
	(*(m_data->mutable_ret()))[DataTypeKey] = m_cdata->GetType();
	(*(m_data->mutable_ret()))[DataPayloadKey] = std::move(payload);
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

#undef FOR_EACH_DATA_TYPE
