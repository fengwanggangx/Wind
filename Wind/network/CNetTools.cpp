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
#include "CNetPool.h"
#include "CNetTools.h"
#include "CFrameCodec.h"

namespace net
{

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

	std::size_t RequestFromBuffer(std::vector<std::unique_ptr<CRequest>>& reqs, struct bufferevent* pEvent)
	{
		auto ret = CNetPool::InstancePtr()->GetRecvBuffer(pEvent);
		if (!ret.has_value())
		{
			return 0;
		}
		std::vector<char>& buffer = *ret.value();
		std::vector<char> received;
		std::size_t nReceived = net::BufferEventReader(pEvent, received);
		if (nReceived == 0)
		{
			return 0;
		}
		_TyConnectionId id = bufferevent_getfd(pEvent);
		buffer.insert(buffer.end(), received.begin(), received.end());

		std::size_t nReqCount = 0;
		std::size_t nBufferLength = buffer.size();
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

	bool SendRequest(net::_TyConnectionId id, const CRequest& request)
	{
		std::string strPayload;
		if (!request.Serialize(&strPayload))
		{
			return false;
		}
		std::string frame = net::CFrameCodec::Encode(strPayload);
		bool bRet = net::CNetPool::InstancePtr()->Send(id, frame.data(), frame.size());
		return bRet;
	}

	bool SendRequest(struct bufferevent* pEvent, const CRequest& request)
	{
		_TyConnectionId id = bufferevent_getfd(pEvent);
		return SendRequest(id, request);
	}

	void SetError(CRequest& request, int code, const std::string& message)
	{
		request.SetReturnData("error_code", std::to_string(code));
		request.SetReturnData("error_message", message);
	}

	void SetError(CRequest& response, const CRequest& request, int code, const std::string& message)
	{
		response = request;
		SetError(response, code, message);
	}

	void SendError(net::_TyConnectionId id, const CRequest& request, int code, const std::string& message)
	{
		CRequest response = request;
		SetError(response, code, message);
		SendRequest(id, response);
	}
} // namespace net
