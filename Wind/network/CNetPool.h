#ifndef __CNETPOOL_H__
#define __CNETPOOL_H__


#include <unordered_map>
#include "../common/ISingleton.h"
#include "event2/bufferevent.h"
namespace net
{	
	struct CNetInfo;
	class CNetPool final : public ISingleton<CNetPool>
	{
		DECLARE_SINGLE_DFAULT(CNetPool)

	public:
		bool CloseAConnection(evutil_socket_t fd);
		struct bufferevent* RegisterConnect(evutil_socket_t fd, struct event_base* pNet, struct sockaddr* pAddr, int nLength, bufferevent_data_cb readcb, bufferevent_data_cb writecb, bufferevent_event_cb eventcb, void* cbarg);
		struct bufferevent* RegisterAConnection(evutil_socket_t fd, struct bufferevent* pEvent, struct sockaddr_storage* pAddr);
	private:
		bool CloseAConnection(CNetInfo& info);
		bool RegisterAConnection(evutil_socket_t fd, struct bufferevent* pEvent, struct sockaddr* pAddr);

		bool SendData2Client(evutil_socket_t fd, const char* data, size_t nLength);
	private:
		std::unordered_map<evutil_socket_t, CNetInfo*> m_pool;
	};
}
#endif
