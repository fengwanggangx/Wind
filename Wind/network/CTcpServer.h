#ifndef __CTCPSERVER_H__
#define __CTCPSERVER_H__

#include <memory>
#include <vector>
#include <functional>
#include "CNet.h"
#include "CNetRouter.h"
template<bool bAsyn, class _Ty, class _TyHandler>
class CDistributor;

class CRequest;

namespace net
{
	class CTcpServer final : public CNet, public CNetRouter<CTcpServer>
	{
		using _TyData = std::unique_ptr<CRequest>;
		using _TyHandler = std::function<int(const _TyData&)>;
		using _TyDistributor = CDistributor<true, std::vector<_TyData>, _TyHandler>;
	public:
		explicit CTcpServer(int nPort);
		~CTcpServer() = default;

	public:
		void OnListenerError(struct evconnlistener* pListener);

		int Initialize();

		void RegisterHandler(_TyHandler&& func);

	public:
		void OnConnAccept(struct evconnlistener* pListener, evutil_socket_t fd, struct sockaddr* pAddr, int nLength) override;
		std::size_t OnRead(struct bufferevent* pEvent) override;
		void OnEvent(struct bufferevent* pEvent, short events) override;

	private:
		int	m_nPort{ -1 };
		std::vector<char> m_buffer_recv;
		std::vector<char> m_buffer_send;
		std::unique_ptr<_TyDistributor> m_dispatcher;
	};
}
#endif
