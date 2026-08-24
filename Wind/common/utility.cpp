#include "utility.h"

#include <algorithm>
#include <cctype>

namespace utility
{
	std::string lower(std::string strVal)
	{
		std::transform(strVal.begin(), strVal.end(), strVal.begin(), [](unsigned char ch)
			{
				return static_cast<char>(std::tolower(ch));
			});
		return strVal;
	}

	size_t SplitString(const std::string& s, std::vector<std::string>& vc, char delim, bool bEmpty)
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

	std::size_t SplitStringView(const std::string& str, std::vector<stringview>& views, char delim, bool bEmpty)
	{
		std::size_t nLength = str.length();
		if (nLength <= 0)
		{
			views.clear();
			return 0;
		}
		std::size_t sz = views.size();
		std::size_t n = 0;
		std::size_t nStart = 0;
		for (std::size_t i = 0; i < nLength; ++i)
		{
			if (str[i] == delim)
			{
				if ((!bEmpty) && (nStart == i))
				{
					nStart = i + 1;
					continue;
				}
				if (n < sz)
				{
					views.at(n).SetView(nStart, i);
				}
				else
				{
					views.emplace_back(nStart, i);
				}
				++n;
				nStart = i + 1;
			}
		}
		if (n < sz)
		{
			views.at(n).SetView(nStart, nLength);
		}
		else
		{
			views.emplace_back(nStart, nLength);
		}
		++n;
		if (n < sz)
		{
			views.resize(n);
		}
		return n;
	}

} // namespace utility
