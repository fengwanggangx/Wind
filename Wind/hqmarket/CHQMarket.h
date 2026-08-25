#ifndef WIND_HQMARKET_CHQMARKET_H
#define WIND_HQMARKET_CHQMARKET_H

#include "../request/request.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace net
{
	class CTcpClient;
	struct CNetEvent;
}

class CHQMarket final
{
	public:
		using _TyHandler = std::function<void(std::unique_ptr<CRequest>)>;

		explicit CHQMarket(net::CTcpClient* pTcpClient);
		~CHQMarket();
		CHQMarket(const CHQMarket&) = delete;
		CHQMarket& operator=(const CHQMarket&) = delete;

		bool Initialize(const std::string& strToken);
		void Stop();
		// 接管request所有权，调用结束后自动释放。
		bool SendRequest(CRequest* pRequest);
		void RegisterHandler(_TyHandler handler);
		bool IsConnected() const;
		bool IsAuthenticated() const;

	private:
		int OnNetEvent(const net::CNetEvent& ev);
		void SendAuthRequest();
		void HandleRequest(const CRequest& request);
		void Dispatch(std::unique_ptr<CRequest> request);

	private:
		mutable std::mutex m_mtx_state;
		net::CTcpClient* m_pTcpClient{nullptr};
		std::string m_strToken;
		_TyHandler m_handler;
		std::atomic_bool m_bConnected{false};
		std::atomic_bool m_bAuthenticated{false};
		std::atomic_uint64_t m_nRequestId{0};
};

#endif
