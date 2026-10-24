#include "defines.h"

CThreadPool* GetThreadPool()
{
	static CThreadPool* p = new CThreadPool(pool_type::em_more_calc);
	return p;
}