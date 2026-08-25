#ifndef __REQUEST_H__
#define __REQUEST_H__

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include "../network/common_net.h"

namespace request
{
	class RequestData;
}

namespace google
{
	namespace protobuf
	{
		class Arena;
	}
} // namespace google

class CRequest
{
	public:
		CRequest();
		~CRequest();
		CRequest(const CRequest&) = delete;
		CRequest& operator=(const CRequest&) = delete;
		CRequest(CRequest&&) = delete;
		CRequest& operator=(CRequest&&) = delete;

		enum class Type
		{
			UNKNOWN = 0,
			QUERY_AUTH = 1,
			QUERY_USERINFO = 2,
			UPDATE_AUTH = 3,
			UPDAT_PRODUCT = 4,
			HQMARKET = 5
		};

	public:
		std::uint64_t GetId() const;
		void SetId(std::uint64_t nId);

		CRequest::Type GetType() const;
		void SetType(CRequest::Type type);

		std::string GetCmd() const;
		void SetCmd(const std::string& strCmd);

		std::string GetExtraData(const std::string& strKey) const;
		std::unordered_map<std::string, std::string> GetExtraData() const;
		void SetExtraData(const std::string& strKey, const std::string& strVal);

		std::string GetReturnData(const std::string& strKey) const;
		std::unordered_map<std::string, std::string> GetReturnData() const;
		void SetReturnData(const std::string& strKey, const std::string& strVal);

		net::_TyConnectionId GetConnectionId() const;
		void SetConnectionId(net::_TyConnectionId id);

	public:
		bool Serialize(std::string* output) const;
		bool Deserialize(const std::string& data);

	private:
		std::unique_ptr<google::protobuf::Arena> m_arena;
		request::RequestData* m_data{nullptr};
		net::_TyConnectionId m_connection_id{-1};
};

#endif
