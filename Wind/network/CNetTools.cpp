#include "CNetTools.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "../request/request.h"
#include <string.h>
#include <mutex>
#include <unordered_map>

namespace net
{
	namespace utility
	{
		namespace
		{
			std::mutex& ConnectionBuffersMutex()
			{
				static std::mutex mtxBuffers;
				return mtxBuffers;
			}

			std::unordered_map<_TyConnectionId, std::vector<char>>& ConnectionBuffers()
			{
				static std::unordered_map<_TyConnectionId, std::vector<char>> connectionBuffers;
				return connectionBuffers;
			}
		}

		std::size_t BufferEventReader(struct bufferevent* pEvent, std::vector<char>& buffer)
		{
			buffer.clear();
			struct evbuffer* input = bufferevent_get_input(pEvent);
			std::size_t nLength = evbuffer_get_length(input);
			buffer.resize(nLength);
			std::size_t n = evbuffer_remove(input, buffer.data(), nLength);
			buffer.resize(n);
			return n;
		}

		std::size_t RequestFromBuffer(std::vector<std::unique_ptr<CRequest>>& reqs, struct bufferevent* pEvent, std::vector<char>& buffer)
		{
			std::vector<char> received;
			std::size_t nReceived = net::utility::BufferEventReader(pEvent, received);
			if (nReceived == 0)
			{
				return 0;
			}
			_TyConnectionId id = bufferevent_getfd(pEvent);
			std::lock_guard<std::mutex> lock(ConnectionBuffersMutex());
			std::unordered_map<_TyConnectionId, std::vector<char>>& connectionBuffers = ConnectionBuffers();
			std::vector<char>& connectionBuffer = connectionBuffers[id];
			connectionBuffer.insert(connectionBuffer.end(), received.begin(), received.end());
			buffer.clear();

			std::size_t nReqCount = 0;
			std::size_t nBufferLength = connectionBuffer.size();
			while (nBufferLength >= sizeof(uint32_t))
			{
				constexpr std::size_t nHeaderLength = sizeof(uint32_t);
				uint32_t nDataLength = 0;
				memcpy(&nDataLength, connectionBuffer.data(), nHeaderLength);
				nDataLength = ntohl(nDataLength);

				if (nBufferLength < (nHeaderLength + nDataLength))
				{
					break;
				}

				const char* pszData = connectionBuffer.data() + nHeaderLength;
				std::string strData(pszData, nDataLength);

				std::unique_ptr<CRequest> req = std::make_unique<CRequest>();
				if (req->Deserialize(strData))
				{
					req->SetConnectionId(id);
					reqs.emplace_back(std::move(req));
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
					memmove(connectionBuffer.data(), connectionBuffer.data() + nDone, nBufferLength - nDone);
					connectionBuffer.resize(nBufferLength - nDone);
					nBufferLength = connectionBuffer.size();
				}
				else
				{
					connectionBuffer.clear();
					break;
				}
			}
			if (connectionBuffer.empty())
			{
				connectionBuffers.erase(id);
			}
			return nReqCount;
		}

		void ReleaseConnectionBuffer(_TyConnectionId id)
		{
			std::lock_guard<std::mutex> lock(ConnectionBuffersMutex());
			ConnectionBuffers().erase(id);
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
	} // namespace utility
} // namespace net
