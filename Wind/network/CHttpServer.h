#ifndef __CHTTPSERVER_H__
#define __CHTTPSERVER_H__

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "CNet.h"

struct evhttp;
struct evhttp_bound_socket;
struct evhttp_request;

namespace net
{
	enum class HttpMethod
	{
		GET, POST, PUT, DELETE, OPTIONS, PATCH, UNKNOWN
	};

	class CHttpRequest final
	{
	public:
		HttpMethod GetMethod() const;
		const std::string& GetPath() const;
		const std::string& GetUri() const;
		const std::string& GetBody() const;
		std::string GetHeader(const std::string& strName) const;
		std::string GetQuery(const std::string& strName) const;

	private:
		friend class CHttpServer;
		CHttpRequest() = default;

	private:
		HttpMethod m_method{ HttpMethod::UNKNOWN };
		std::string m_strPath;
		std::string m_strUri;
		std::string m_strBody;
		std::unordered_map<std::string, std::string> m_headers;
		std::unordered_map<std::string, std::string> m_queries;
	};

	class CHttpStream final
	{
	public:
		~CHttpStream() = default;
		bool Send(const std::string& strData);
		bool Send(const std::string& strEvent, const std::string& strData);
		void Close();
		void SetCloseHandler(std::function<void()>&& func);

	private:
		friend class CHttpResponse;
		friend class CHttpServer;
		struct State;
		explicit CHttpStream(std::shared_ptr<State> state);

	private:
		std::shared_ptr<State> m_state;
	};

	class CHttpResponse final
	{
	public:
		~CHttpResponse() = default;
		void SetStatus(int nStatus);
		void SetHeader(const std::string& strName, const std::string& strValue);
		bool Send(const std::string& strBody);
		bool SendJson(const std::string& strBody);
		std::shared_ptr<CHttpStream> BeginSSE();

	private:
		friend class CHttpServer;
		struct State;
		explicit CHttpResponse(std::shared_ptr<State> state);
		bool IsHandled() const;

	private:
		std::shared_ptr<State> m_state;
	};

	class CHttpServer final : public CNet
	{
		friend class CHttpResponse;
		using _TyHandler = std::function<void(const CHttpRequest&, CHttpResponse&)>;

	public:
		explicit CHttpServer(int nPort);
		~CHttpServer() override;
		int Initialize();
		void RegisterHandler(HttpMethod method, const std::string& strPath, _TyHandler&& func);

	private:
		static void Request_Callback(struct evhttp_request* pRequest, void* pArg);
		void OnRequest(struct evhttp_request* pRequest);
		void Release();
		void RegisterStream(const std::shared_ptr<CHttpStream::State>& state);

	private:
		int m_nPort{ -1 };
		struct evhttp* m_pHttp{ nullptr };
		struct evhttp_bound_socket* m_pListener{ nullptr };
		std::unordered_map<std::string, _TyHandler> m_handlers;
		std::mutex m_mutex_streams;
		std::vector<std::weak_ptr<CHttpStream::State>> m_streams;
	};
}

#endif
