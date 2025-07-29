#ifndef __CNETDISTRIBUTOR_H__
#define __CNETDISTRIBUTOR_H__

#include "../common/ISingleton.h"
#include <functional>
namespace net
{
	template<class _Ty>
	class CNetDistributor final : public ISingleton<CNetDistributor<_Ty>>
	{
		using pointer = _Ty*;
		using self = CNetDistributor<_Ty>;
		using handler = std::function<bool(_Ty*)>;
		DECLARE_SINGLE_DFAULT(CNetDistributor)
	public:
		void AddRequest(pointer ptr)
		{
			m_net.emplace_back(ptr);
		}
		void RegisterHandler(handler&& fun)
		{
			m_handler.emplace_back(std::forward<handler>(fun));
		}
	private:
		std::vector<pointer> m_net;
		std::vector<handler> m_handler;
	};

	template<class _Ty>
	CNetDistributor<_Ty>::CNetDistributor()
	{
	}

	template<class _Ty>
	CNetDistributor<_Ty>::~CNetDistributor()
	{

	}
}
#endif
