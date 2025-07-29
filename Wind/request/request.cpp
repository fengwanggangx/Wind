#include "request.pb.h"
#include "request.h"


static request::RequestType ToProtoType(CRequest::Type type) 
{
	static std::unordered_map<CRequest::Type, request::RequestType> s_map
	{
		{CRequest::Type::UNKNOWN, request::RequestType::UNKNOWN},
		{CRequest::Type::QUERY_AUTH, request::RequestType::QUERY_AUTH},
		{CRequest::Type::QUERY_USERINFO, request::RequestType::QUERY_USERINFO},
		{CRequest::Type::UPDATE_AUTH, request::RequestType::UPDATE_AUTH},
		{CRequest::Type::UPDAT_PRODUCT, request::RequestType::UPDAT_PRODUCT}
	};
	const auto& mIter = s_map.find(type);
	return s_map.end() == mIter ? request::RequestType::UNKNOWN : mIter->second;
}


google::protobuf::Arena* CRequest::m_arena = new google::protobuf::Arena();

CRequest::CRequest() 
{
	google::protobuf::Arena* px = new google::protobuf::Arena();
	m_data = google::protobuf::Arena::CreateMessage<request::RequestData>(px);
}

CRequest::~CRequest()
{
	m_data = nullptr;
}

bool CRequest::Serialize(std::string* output) const
{
	if (nullptr == m_data)
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
	return m_data->ParseFromString(data);
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
	return { m_data->extra().begin(), m_data->extra().end() };
}

std::string CRequest::GetExtraData(const std::string& strKey) const
{
	const auto& data = GetExtraData();
	const auto& mIter = data.find(strKey);
	return data.end() == mIter ? "" : mIter->second;
}

std::string CRequest::GetReturnData(const std::string& strKey) const
{
	const auto& data = GetReturnData();
	const auto& mIter = data.find(strKey);
	return data.end() == mIter ? "" : mIter->second;
}

std::unordered_map<std::string, std::string> CRequest::GetReturnData() const
{
	if (nullptr == m_data)
	{
		return {};
	}
	return { m_data->ret().begin(), m_data->ret().end() };
}