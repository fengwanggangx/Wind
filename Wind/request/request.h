#ifndef __REQUEST_H__
#define __REQUEST_H__

#include <string>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include "../network/common_net.h"

class CData final
{
	public:
		CData() = default;
		CData(std::string type, void* pData);
		~CData();
		CData(const CData&) = delete;
		CData& operator=(const CData&) = delete;
		CData(CData&&) = delete;
		CData& operator=(CData&&) = delete;

		const std::string& GetType() const;
		void* GetData();
		const void* GetData() const;

		template<typename _Ty>
		_Ty* GetDataAs()
		{
			return (_Ty::descriptor()->full_name() == m_type) ? static_cast<_Ty*>(m_pData) : nullptr;
		}

		template<typename _Ty>
		const _Ty* GetDataAs() const
		{
			return (_Ty::descriptor()->full_name() == m_type) ? static_cast<const _Ty*>(m_pData) : nullptr;
		}

		bool Serialize(std::string* output) const;
		bool Deserialize(const std::string& type, const std::string& payload);

	private:
		void Reset();

		std::string m_type;
		void* m_pData{ nullptr };
};

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
		CRequest(const CRequest& arg);
		CRequest& operator=(const CRequest& arg);
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

		void SetData(std::unique_ptr<CData> data);
		const CData* GetData() const;

		void SetConnectionId(net::_TyConnectionId id);
		net::_TyConnectionId GetConnectionId() const;

	public:
		bool Serialize(std::string* output) const;
		bool Deserialize(const std::string& data);

	private:
		std::unique_ptr<google::protobuf::Arena> m_arena;
		request::RequestData* m_data{ nullptr };
		std::unique_ptr<CData> m_cdata;
		net::_TyConnectionId m_connection_id{ -1 };
};

#endif
