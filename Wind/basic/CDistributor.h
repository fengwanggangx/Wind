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
	using _TyPointer = std::unique_ptr<_Ty>;
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
				m_data.emplace_back(std::move(ptr));
			}
			ThreadPoolPtr->PushTask(task_priority::em_normal, 0, [this]() { AsyncExecute(); });
		}
		else
		{
			Execute(*ptr);
		}
	}

	void Dispatch(_TyDataContainer&& data)
	{
		if (m_bAsync)
		{
			{
				std::unique_lock<std::shared_mutex> lock(m_mtx_data);
				m_data.reserve(m_data.size() + data.size());
				m_data.insert(
					m_data.end(),
					std::make_move_iterator(data.begin()),
					std::make_move_iterator(data.end())
				);
			}
			ThreadPoolPtr->PushTask(task_priority::em_normal, 0, [this]() { AsyncExecute(); });
		}
		else
		{
			Execute(std::forward<_TyDataContainer>(data));
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
			Execute(*item);
		}
	}

	int Execute(_TyDataContainer&& data)
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_handler);
		int nOK = ((1 << m_handler.size()) - 1);
		
		std::size_t sz = data.size();
		int ret = ((1 << sz) - 1);
		for (std::size_t i = 0; i < sz; ++i)
		{
			if (ExecuteA(*data.at(i)) != nOK)
			{
				ret &= ~(1 << i);
			}
		}
		return ret;
	}

	int Execute(_TyConstRef data)
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx_handler);
		return ExecuteA(data);
	}

	int ExecuteA(_TyConstRef data)
	{
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
