#ifndef HQMARKET_SYSTEM_CBOOTLOADER_H
#define HQMARKET_SYSTEM_CBOOTLOADER_H

#include <memory>
#include <string>
#include <thread>

namespace net
{
	class CHttpServer;
	class CTcpClient;
	class CTcpServer;
}

class CBootLoader final
{
	public:
		CBootLoader();
		~CBootLoader();
		CBootLoader(const CBootLoader&) = delete;
		CBootLoader& operator=(const CBootLoader&) = delete;

		bool Initialize();
		bool Run();
		void Finalize();
		net::CTcpServer& GetTcpServer();
		net::CTcpClient& GetTcpClient();
		net::CHttpServer& GetHttpServer();
		const std::string& GetHQMarketToken() const;
		const std::string& GetLastError() const;
		int GetErrorCode() const;

	private:
		std::string m_strLastError;
		std::string m_strHQMarketToken;
		int m_nErrorCode{ 0 };
		bool m_bInitialized{ false };

	private:
		std::unique_ptr<net::CTcpServer> m_pTcpServer;
		std::unique_ptr<net::CTcpClient> m_pTcpClient;
		std::unique_ptr<net::CHttpServer> m_pHttpServer;
};

#endif
