#ifndef __CDISTRIBUTOR_H__
#define __CDISTRIBUTOR_H__
#include <shared_mutex>
#include <functional>
#include <memory>
#include <mutex>
#include "../common/defines.h"

template<class _Ty>
class CDistributor final
{
	using _TyPointer = _Ty*;
	using _TyConstRef = const _Ty&;
	using _TyHandler = std::function<int(_TyConstRef)>;
	using _TyDataContainer = std::vector<std::unique_ptr<_Ty>>;
public:
	CDistributor(bool bAsync) : m_bAsync(bAsync)
	{

	}
	~CDistributor()
	{

	}

public:
	void Dispatch(_TyPointer ptr)
	{
		if (m_bAsync)
		{
			{
				std::unique_lock<std::shared_mutex> lock(m_mtx_data);
				m_data.emplace_back(ptr);
			}
			ThreadPoolPtr->PushTask([this]() { AsyncExecute(); });
		}
		else
		{
			Execute(ptr);
		}
	}

	void RegisterHandler(_TyHandler&& fun)
	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_handler);
		m_handler.emplace_back(std::forward<_TyHandler>(fun));
	}

private:
	void AsyncExecute()
	{
		_TyDataContainer data;
		{
			std::unique_lock<std::shared_mutex> lock(m_mtx_data);
			data.swap(m_data);
		}

		for (const auto& item : data)
		{
			Execute(item);
		}
	}

	int Execute(_TyConstRef data)
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_handler);
		std::size_t sz = m_handler.size();
		int ret = ((1 << sz) - 1);
		for (std::size_t i = 0; i < sz; ++i)
		{
			const auto& fun = m_handler.at(i);
			if ((nullptr == fun) || !fun(data))
			{
				ret &= ~(1 << i);
			}
		}
		return ret;
	}
private:

	std::shared_mutex m_mtx_data;
	_TyDataContainer m_data;

	std::shared_mutex m_mtx_handler;
	std::vector<_TyHandler> m_handler;
	bool m_bAsync{ false };
};
#endif
