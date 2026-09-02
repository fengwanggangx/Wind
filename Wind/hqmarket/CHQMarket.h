#ifndef WIND_HQMARKET_CHQMARKET_H
#define WIND_HQMARKET_CHQMARKET_H

#include "MarketTypes.h"
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
		using _TyHandler = std::function<void(const CRequest&)>;

		explicit CHQMarket(net::CTcpClient* pTcpClient);
		~CHQMarket();
		CHQMarket(const CHQMarket&) = delete;
		CHQMarket& operator=(const CHQMarket&) = delete;

		bool Initialize(const std::string& strToken);
		void Stop();
		// 订阅指定证券和数据通道的实时行情。
		bool SubscribeQuote(const std::string& strCode, market::Exchange mk, market::Channel channel);
		// 接管request所有权，调用结束后自动释放。
		bool SendRequest(const CRequest& req);
		void RegisterHandler(_TyHandler&& handler);
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
		std::vector<_TyHandler> m_handlers;
		std::atomic_bool m_bConnected{false};
		std::atomic_bool m_bAuthenticated{false};
};

#endif
