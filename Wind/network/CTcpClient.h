#ifndef WIND_NETWORK_CTCPCLIENT_H
#define WIND_NETWORK_CTCPCLIENT_H
#include "CNet.h"
#include "CNetRouter.h"
#include "common_net.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

template<bool bAsyn, class _Ty, class _TyHandler>
class CDistributor;

namespace net
{
	class CTcpClient final : public CNet, public CNetRouter<CTcpClient>
	{
		friend class CNetRouter<CTcpClient>;

		using _TyHandler = std::function<int(const CNetEvent&)>;
		using _TyDistributor = CDistributor<true, std::vector<CNetEvent>, _TyHandler>;
		public:
			explicit CTcpClient(const std::string& strAddr, int nPort);
			~CTcpClient() override;
			int Initialize();

			bool IsConnected() const;

			bool SendRequest(const CRequest& req);

			void ClearHandlers();
			void RegisterHandler(_TyHandler&& handler);

			void Release();

			net::_TyConnectionId GetId() const;
		
		private:
			bool Send(const void* pData, std::size_t nLength);
			void OnConnected(bufferevent* pEvent) override;
			std::size_t OnRead(bufferevent* pEvent) override;
			void OnEvent(bufferevent* pEvent, short events) override;

		private:
			struct CBufferEventDeleter
			{
				void operator()(bufferevent* pEvent) const;
			};

			std::unique_ptr<bufferevent, CBufferEventDeleter> m_pEvent;
			std::atomic_bool m_bConnected{false};
			std::unique_ptr<_TyDistributor> m_dispatcher;

		private:
			std::string m_strAddr;
			int m_nPort{ -1 };

			net::_TyConnectionId m_id{ 0 };
	};
} // namespace net
#endif
