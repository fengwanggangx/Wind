#ifndef __UTILITY_H__
#define __UTILITY_H__
#include <string>
#include <vector>
#include <string.h>

template <typename T>
concept require_number = std::is_arithmetic_v<T>;

namespace utility
{
	size_t stringsplit(const std::string& s, std::vector<std::string>& vc, char delim, bool bEmpty = false)
	{
		vc.clear();
		const char* p = s.c_str();
		size_t n = strlen(p);
		size_t opos = 0;
		for (size_t i = 0; i < n; ++i)
		{
			bool bDelim = (*(p + i) == delim);
			bool bEnd = i >= (n - 1);
			if (bDelim)
			{
				if ((i - opos > 0) || bEmpty)
				{
					vc.emplace_back(s.substr(opos, i - opos));
				}
				opos = bEnd ? i : i + 1;
			}
			if (bEnd)
			{
				if (bDelim)
				{
					if (bEmpty)
					{
						vc.emplace_back(s.substr(opos, 0));
					}
				}
				else
				{
					vc.emplace_back(s.substr(opos, -1));
				}
			}
		}
		return vc.size();
	}

	bool s2n(const std::string& str, require_number auto& val)
	{
		decltype(val) temp = 0;
		const char* first = str.data();
		const char* last = str.data() + str.size();
		auto [p, ex] = std::from_chars(first, last, temp);

		if ((ex == std::errc{}) && (p == last))
		{
			val = temp;
			return true;
		}
		return false;
	}

}

#endif
