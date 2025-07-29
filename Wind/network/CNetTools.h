#ifndef __CNETTOOLS_H__
#define __CNETTOOLS_H__
#include <vector>

struct bufferevent;
class CRequest;
namespace net
{
	namespace utility
	{
		std::size_t BufferEventReader(struct bufferevent* pEvent, std::vector<char>& buffer);
		std::size_t RequestFromBuffer(std::vector<CRequest*>& reqs, struct bufferevent* pEvent, std::vector<char>& buffer);
		bool SendRequest(CRequest* pRequest, struct bufferevent* pEvent, std::vector<char>& buffer);
	}
}
#endif
