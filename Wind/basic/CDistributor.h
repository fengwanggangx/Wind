#ifndef __CDISTRIBUTOR_H__
#define __CDISTRIBUTOR_H__
#include <shared_mutex>
#include <functional>
#include <memory>
#include <mutex>
#include "../common/defines.h"
#include "../common/utility.h"

template<bool bAsyn, class _Ty, class _TyHandler>
class CDistributor
{
	using _TyData = Typer<_Ty>::type;
	using _TyDataContainer = std::vector<_TyData>;

public:
	CDistributor() = default;
	~CDistributor() = default;

public:

	void Dispatch(_Ty&& data)
	{
		if constexpr (bAsyn)
		{
			if constexpr (IsContainer<_Ty>)
			{
				{
					std::unique_lock<std::shared_mutex> lock(m_mtx_data);
					m_cache.reserve(m_cache.size() + data.size());
					m_cache.insert(
						m_cache.end(),
						std::make_move_iterator(data.begin()),
						std::make_move_iterator(data.end())
					);
				}
			}
			else
			{
				std::unique_lock<std::shared_mutex> lock(m_mtx_data);
				m_cache.emplace_back(std::move(data));
			}
			ThreadPoolPtr->PushTask(task_priority::em_normal, 0, [this]() { AsyncExecute(); });
		}
		else
		{
			Execute(std::forward<_Ty>(data));
		}
	}

	void RegisterHandler(_TyHandler&& fun)
	{
		std::unique_lock<std::shared_mutex> lock(m_mtx_handler);
		m_handler.emplace_back(std::forward<_TyHandler>(fun));
	}

private:
	int AsyncExecute()
	{
		_TyDataContainer data;
		{
			std::unique_lock<std::shared_mutex> lock(m_mtx_data);
			data.swap(m_cache);
		}

		return Execute(std::move(data));
	}

	int Execute(_Ty&& data)
	{
		int ret = 1;
		if constexpr (IsContainer<_Ty>)
		{
			std::shared_lock<std::shared_mutex> lock(m_mtx_handler);
			int nOK = ((1 << m_handler.size()) - 1);

			std::size_t sz = data.size();
			ret = ((1 << sz) - 1);
			for (std::size_t i = 0; i < sz; ++i)
			{
				if (ExecuteA(data.at(i)) != nOK)
				{
					ret &= ~(1 << i);
				}
			}
		}
		else
		{
			ret = ExecuteA(data);
		}
		return ret;
	}


	int ExecuteA(const _TyData& data)
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
	_TyDataContainer m_cache;

	std::shared_mutex m_mtx_handler;
	std::vector<_TyHandler> m_handler;
};

#endif
