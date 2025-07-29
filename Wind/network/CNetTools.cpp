#include "CNetTools.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "../request/request.h"
#include <string.h>

namespace net
{
	namespace utility
	{

		std::size_t BufferEventReader(struct bufferevent* pEvent, std::vector<char>& buffer)
		{
			buffer.clear();
			struct evbuffer* input = bufferevent_get_input(pEvent);
			std::size_t nLength = evbuffer_get_length(input);
			if (nLength > buffer.capacity())
			{
				buffer.resize(static_cast<std::size_t>(std::ceil(static_cast<double>(nLength) * 1.5))); // 增加50%的余量
			}
			std::size_t n = evbuffer_remove(input, buffer.data(), nLength);
			return n;
		}

		std::size_t RequestFromBuffer(std::vector<CRequest*>& reqs, struct bufferevent* pEvent, std::vector<char>& buffer)
		{
			std::size_t nReqCount = 0;
			std::size_t nBufferLength = net::utility::BufferEventReader(pEvent, buffer);
			if (nBufferLength <= 0)
			{
				return 0;
			}
			while (nBufferLength >= sizeof(uint32_t))
			{
				constexpr std::size_t nHeaderLength = sizeof(uint32_t);
				uint32_t nDataLength = 0;
				memcpy(&nDataLength, buffer.data(), nHeaderLength);
				nDataLength = ntohl(nDataLength);

				if (nBufferLength < (nHeaderLength + nDataLength))
				{
					break;
				}

				const char* pszData = buffer.data() + nHeaderLength;
				std::string strData(pszData, nDataLength);

				CRequest* pRequest = new CRequest;
				if (pRequest->Deserialize(strData))
				{
					reqs.emplace_back(pRequest);
					++nReqCount;
				}
				else
				{
					break;
				}

				// 移除已处理的数据
				std::size_t nDone = nHeaderLength + nDataLength;
				if (nBufferLength > nDone)
				{
					memmove(buffer.data(), buffer.data() + nDone, nBufferLength - nDone);
					buffer.resize(nBufferLength - nDone);
					nBufferLength = buffer.size();
				}
				else
				{
					buffer.clear();
					break;
				}
			}
			return nReqCount;
		}

		bool SendRequest(CRequest* pRequest, struct bufferevent* pEvent, std::vector<char>& buffer)
		{
			if (nullptr == pEvent)
			{
				return false;
			}
			
			if (nullptr == pRequest)
			{
				return false;
			}

			std::string data;
			if (pRequest->Serialize(&data))
			{
				constexpr std::size_t nHeadLength = sizeof(uint32_t);
				std::size_t nLength = nHeadLength + data.size();
				if (nLength > buffer.capacity())
				{
					buffer.resize(static_cast<std::size_t>(std::ceil(static_cast<double>(nLength) * 1.5))); // 增加50%的余量
				}
				buffer.resize(nLength);
				uint32_t nDataLength = htonl(static_cast<uint32_t>(data.size()));
				memcpy(buffer.data(), &nDataLength, nHeadLength);
				memcpy(buffer.data() + nHeadLength, data.data(), data.size());
				return (0 == bufferevent_write(pEvent, buffer.data(), nLength));
			}
			return false;
		}
	}
}