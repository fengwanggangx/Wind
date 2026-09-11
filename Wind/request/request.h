#ifndef HQMARKET_REQUEST_REQUEST_H
#define HQMARKET_REQUEST_REQUEST_H

#include "../network/common_net.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace request
{
	class RequestData;
	class StrategyInfo;
	class StrategyList;
} // namespace request

namespace hqmarket::market::v1
{
	class SubscriptionAck;
	class QuoteData;
	class DepthData;
	class QueryResponse;
} // namespace hqmarket::market::v1

namespace google::protobuf
{
	class Arena;
}

using _TyRequestId = std::uint64_t;
using _TyReqData = request::RequestData;
using _TyStrategyInfo = request::StrategyInfo;
using _TyStrategyList = request::StrategyList;
using _TySubscriptionAck = hqmarket::market::v1::SubscriptionAck;
using _TyQuoteData = hqmarket::market::v1::QuoteData;
using _TyDepthData = hqmarket::market::v1::DepthData;
using _TyQueryResponse = hqmarket::market::v1::QueryResponse;

class CRequest
{
  public:
	enum class Type
	{
		UNKNOWN = 0,
		QUERY_AUTH = 1,
		QUERY_USERINFO = 2,
		UPDATE_AUTH = 3,
		STRATEGY = 4,
		HQMARKET = 5,
		HEARTBEAT = 6
	};

  public:
	CRequest();
	~CRequest();
	CRequest(const CRequest& arg);
	CRequest& operator=(const CRequest& arg);
	CRequest(CRequest&& arg) noexcept;
	CRequest& operator=(CRequest&& arg) noexcept;

  public:
	_TyRequestId GetId() const;
	void SetId(_TyRequestId id);

	Type GetType() const;
	void SetType(Type t);

	std::string GetCmd() const;
	void SetCmd(const std::string& strCmd);

	std::string GetExtraData(const std::string& strKey) const;
	std::unordered_map<std::string, std::string> GetExtraData() const;
	void SetExtraData(const std::string& strKey, const std::string& strValue);

	std::string GetReturnData(const std::string& strKey) const;
	std::unordered_map<std::string, std::string> GetReturnData() const;
	void SetReturnData(const std::string& strKey, const std::string& strValue);

	void SetData(const _TySubscriptionAck& value);
	void SetData(const _TyQuoteData& value);
	void SetData(const _TyDepthData& value);
	void SetData(const _TyQueryResponse& value);
	void SetData(const _TyStrategyInfo& value);
	void SetData(const _TyStrategyList& value);
	const _TyReqData& GetData() const;

	void SetConnectionId(net::_TyConnectionId id);
	net::_TyConnectionId GetConnectionId() const;

	bool Serialize(std::string* pOutput) const;
	bool Deserialize(const std::string& strData);

	std::optional<std::pair<int, std::string>> GetErrorInfo() const;

  private:
	std::unique_ptr<google::protobuf::Arena> m_arena;
	_TyReqData* m_data{ nullptr };
	net::_TyConnectionId m_connection_id{ -1 };
};

#endif
