#ifndef __UTILITY_H__
#define __UTILITY_H__
#include <string>
#include <vector>
#include <string.h>
#include <charconv>

template <typename _Ty>
concept IsNumber = std::is_arithmetic_v<_Ty>;

template <typename _Ty>
concept IsContainer = requires(_Ty v) {
	typename _Ty::value_type;
	{ v.begin() } -> std::input_or_output_iterator;
	{ v.end() } -> std::input_or_output_iterator;
	{ v.size() } -> std::convertible_to<size_t>;
};

template <typename _Ty, typename = void>
struct Typer
{
	using type = _Ty;
};

template <typename _Ty>
struct Typer<_Ty, std::enable_if_t<IsContainer<_Ty>>>
{
	using type = typename _Ty::value_type;
};

struct stringview
{
	stringview() = default;
	stringview(int nStart, int nEnd) : m_data{ nStart, nEnd }
	{
	}
	const char* GetPtr(const std::string& org) const
	{
		return org.c_str() + m_data.first;
	}

	int GetLength() const
	{
		return m_data.second - m_data.first;
	}

	const std::string& GetString(const std::string& org) const
	{
		thread_local std::string s_val;
		s_val.assign(GetPtr(org), GetLength());
		return s_val;
	}

	bool Valid() const
	{
		return (m_data.first >= 0) && (m_data.second - m_data.first >= 0);
	}

	void SetView(int nStart, int nEnd)
	{
		m_data.first = nStart;
		m_data.second = nEnd;
	}
	std::pair<int, int> m_data{ -1, -1 }; //[nStart, nEnd)
};

namespace utility
{
	template <typename _Ty>
	bool to_number(const std::string& value, _Ty& result)
	{
		const char* pBegin = value.data();
		const char* pEnd = pBegin + value.size();
		auto parsed = std::from_chars(pBegin, pEnd, result);
		return (std::errc{} == parsed.ec) && (pEnd == parsed.ptr);
	}

	std::string lower(std::string strVal);

	size_t SplitString(const std::string& s, std::vector<std::string>& vc, char delim, bool bEmpty = false);

	std::size_t SplitStringView(const std::string& str, std::vector<stringview>& views, char delim, bool bEmpty = false);

} // namespace utility

#endif
