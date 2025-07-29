#ifndef __REQUEST_H__
#define __REQUEST_H__

#include <string>
#include <unordered_map>
#include <memory> 

namespace request {
	class RequestData;
}

namespace google {
	namespace protobuf {
		class Arena;
	}
}


class CRequest
{
public:
	CRequest();
	~CRequest();

	enum class Type {
		UNKNOWN = 0,
		QUERY_AUTH = 1,
		QUERY_USERINFO = 2,
		UPDATE_AUTH = 3,
		UPDAT_PRODUCT = 4
	};


public:
	void SetType(CRequest::Type type);
	void SetCmd(const std::string& strCmd);
	void SetExtraData(const std::string& strKey, const std::string& strVal);
	void SetReturnData(const std::string& strKey, const std::string& strVal);

	CRequest::Type GetType() const;
	std::string GetCmd() const;
	std::string GetExtraData(const std::string& strKey) const;
	std::string GetReturnData(const std::string& strKey) const;
	std::unordered_map<std::string, std::string> GetExtraData() const;
	std::unordered_map<std::string, std::string> GetReturnData() const;

public:
	bool Serialize(std::string* output) const;
	bool Deserialize(const std::string& data);

private:
	request::RequestData* m_data{ nullptr };
	static google::protobuf::Arena* m_arena;
};

#endif