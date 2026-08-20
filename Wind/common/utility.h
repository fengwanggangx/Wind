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

namespace utility
{
	std::string lower(std::string strVal);

	size_t stringsplit(const std::string& s, std::vector<std::string>& vc, char delim, bool bEmpty = false);
	bool s2n(const std::string& str, IsNumber auto& val)
	{
		using _Ty = decltype(val);
		using _Tyx = std::remove_reference_t<_Ty>;
		_Tyx temp = 0;
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
