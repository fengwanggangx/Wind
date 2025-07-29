#include "CNet.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <event2/event.h>

namespace net
{
	CNet::CNet()
	{
		m_pNet = event_base_new();
	}

	CNet::~CNet()
	{
		Release();
	}

	void CNet::Release()
	{
		if (nullptr != m_pNet)
		{
			if (m_bRunning)
			{
				ShutDown();
			}
			event_base_free(m_pNet);
			m_pNet = nullptr;
		}
	}

	struct event_base* CNet::GetNet()
	{
		return m_pNet;
	}

	void CNet::ShutDown()
	{
		if ((nullptr != m_pNet) && m_bRunning)
		{
			event_base_loopbreak(m_pNet);
			m_bRunning.store(false);
		}
	}

	void CNet::Start(bool bRealTime)
	{
		if ((nullptr == m_pNet) || m_bRunning.load())
		{
			return;
		}
		m_bRunning.store(true);
		if (bRealTime)
		{
			int nRet = event_base_dispatch(m_pNet);
			if (-1 == nRet)
			{
				std::cerr << "事件循环错误" << std::endl;
			}
		}
		else
		{
			while (m_bRunning)
			{
				int nRet = event_base_loop(m_pNet, EVLOOP_NONBLOCK);
				if (-1 == nRet)
				{
					std::cerr << "事件循环错误" << std::endl;
					break;
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		m_bRunning.store(false);
	}
}