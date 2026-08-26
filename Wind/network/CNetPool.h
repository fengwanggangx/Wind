#ifndef __CNETPOOL_H__
#define __CNETPOOL_H__

#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <optional>
#include <vector>
#include "../common/ISingleton.h"
#include "CNet.h"
#include "event2/bufferevent.h"
#include "common_net.h"

namespace net
{
	struct CNetInfo;
	class CNetPool final : public ISingleton<CNetPool>
	{
			DECLARE_SINGLE_DFAULT(CNetPool)

		public:
			std::optional<std::vector<char>*> GetRecvBuffer(_TyConnectionId id);
			std::optional<std::vector<char>*> GetSendBuffer(_TyConnectionId id);
			std::optional<std::vector<char>*> GetRecvBuffer(struct bufferevent* pEvent);
			std::optional<std::vector<char>*> GetSendBuffer(struct bufferevent* pEvent);

			net::_TyConnectionId CloseAConnection(_TyConnectionId id);
			net::_TyConnectionId CloseAConnection(struct bufferevent* pEvent);

			bool SendData2Client(_TyConnectionId id, const char* data, size_t nLength);
			std::size_t Count() const;
			struct bufferevent* RegisterConnect(_TyConnectionId id, struct event_base* pNet, struct sockaddr* pAddr, int nLength, bufferevent_data_cb readcb, bufferevent_data_cb writecb, bufferevent_event_cb eventcb, void* cbarg);
			struct bufferevent* RegisterAConnection(_TyConnectionId id, struct bufferevent* pEvent, struct sockaddr_storage* pAddr);

		private:
			bool CloseAConnection(CNetInfo& info);
			bool RegisterAConnection(_TyConnectionId id, struct bufferevent* pEvent, struct sockaddr* pAddr);

		private:
			mutable std::shared_mutex m_shared_mtx_pool;
			std::unordered_map<_TyConnectionId, std::unique_ptr<CNetInfo>> m_pool;
	};
} // namespace net
#endif
