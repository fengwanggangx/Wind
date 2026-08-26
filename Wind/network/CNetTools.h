#ifndef __CNETTOOLS_H__
#define __CNETTOOLS_H__
#include <vector>
#include <memory>
#include <event2/util.h>
#include "CNet.h"
#include "common_net.h"

struct bufferevent;
class CRequest;
namespace net
{
	namespace utility
	{
		std::size_t BufferEventReader(struct bufferevent* pEvent, std::vector<char>& buffer);
		std::size_t RequestFromBuffer(std::vector<std::unique_ptr<CRequest>>& reqs, struct bufferevent* pEvent);
		void ReleaseConnectionBuffer(_TyConnectionId id);
		bool SendRequest(CRequest* pRequest, struct bufferevent* pEvent);
	} // namespace utility
} // namespace net
#endif
