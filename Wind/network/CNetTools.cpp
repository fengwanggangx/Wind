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
#include "CFrameBuffer.h"

namespace net
{
	namespace
	{
		constexpr std::size_t ReadChunkSize = 64 * 1024;
	}

	net::_TyConnectionId GetConnectionId(struct bufferevent* pEvent)
	{
		if (nullptr == pEvent)
		{
			return -1;
		}
		return bufferevent_getfd(pEvent);
	}

	std::size_t BufferEventReader(struct bufferevent* pEvent, std::vector<char>& buffer)
	{
		buffer.clear();
		struct evbuffer* input = bufferevent_get_input(pEvent);
		std::size_t nLength = std::min(evbuffer_get_length(input), ReadChunkSize);
		buffer.resize(nLength);
		std::size_t n = evbuffer_remove(input, buffer.data(), nLength);
		buffer.resize(n);
		return n;
	}

	std::size_t RequestFromBuffer(std::vector<std::unique_ptr<CRequest>>& reqs, struct bufferevent* pEvent)
	{
		if (nullptr == pEvent)
		{
			return 0;
		}
		_TyConnectionId id = GetConnectionId(pEvent);
		std::size_t nReqCount = 0;
		for (;;)
		{
			std::vector<char> received;
			if (net::BufferEventReader(pEvent, received) == 0)
			{
				break;
			}

			auto [result, frames] = CNetPool::InstancePtr()->GetRecvFrames(id, pEvent, received.data(), received.size());
			if (result != RecvFrameResult::Ok)
			{
				if (result == RecvFrameResult::ProtocolError)
				{
					bufferevent_trigger_event(pEvent, BEV_EVENT_ERROR, BEV_TRIG_DEFER_CALLBACKS);
				}
				return nReqCount;
			}

			for (const auto& frame : frames)
			{
				std::unique_ptr<CRequest> req = std::make_unique<CRequest>();
				if (!req->Deserialize(frame))
				{
					bufferevent_trigger_event(pEvent, BEV_EVENT_ERROR, BEV_TRIG_DEFER_CALLBACKS);
					return nReqCount;
				}
				req->SetConnectionId(id);
				reqs.emplace_back(std::move(req));
				++nReqCount;
			}
		}
		return nReqCount;
	}

	bool SendRequest(net::_TyConnectionId id, const CRequest& request)
	{
		bool bRet = net::CNetPool::InstancePtr()->SendRequest(id, request);
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
