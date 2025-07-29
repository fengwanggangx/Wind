#ifndef __CNetServer_H__
#define __CNetServer_H__


#include <vector>
#include "CNet.h"
#include "CNetRouter.h"

namespace net
{
	class CNetServer final : public CNet, public CNetRouter<CNetServer>
	{
	public:
		explicit CNetServer(int nPort);
		~CNetServer() = default;

	public:
		void OnListenerError(struct evconnlistener* pListener);

		int Initialize();

	public:
		void OnConnAccept(struct evconnlistener* pListener, evutil_socket_t fd, struct sockaddr* pAddr, int nLength) override;
		std::size_t OnRead(struct bufferevent* pEvent) override;
		void OnEvent(struct bufferevent* pEvent, short events) override;
	private:
		int	m_nPort{ -1 };
		std::vector<char> m_buffer_recv;
		std::vector<char> m_buffer_send;
	};
}
#endif
