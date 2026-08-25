#ifndef __CINIHANDLER_H__
#define __CINIHANDLER_H__

#include <string>
#include <unordered_map>

#include "./common/ISingleton.h"
#include "CIniFile.h"
#include <type_traits>

namespace ini
{
	enum class cfgs
	{
		system = 0,
		ui,
		sqlite,
		mysql,
		oracle
	};

	class CIniFile;
	class CINIHandler : public ISingleton<CINIHandler>
	{
		DECLARE_SINGLE_DFAULT(CINIHandler)

	public:
		template <class _Ty, std::enable_if_t<!std::is_convertible<_Ty, std::string>::value, int> = 0>
		_Ty GetValue(ini::cfgs cfg, const std::string& strSection, const std::string& strKey, _Ty&& def) const
		{
			const auto& mIter = m_ini.find(cfg);
			if ((m_ini.end() == mIter) || (nullptr == mIter->second))
			{
				return def;
			}
			return mIter->second->GetValue(strSection, strKey, std::forward<_Ty>(def));
		}

		template <class _Ty, std::enable_if_t<std::is_convertible<_Ty, std::string>::value, int> = 0>
		std::string GetValue(ini::cfgs cfg, const std::string& strSection, const std::string& strKey, _Ty&& def) const
		{
			const auto& mIter = m_ini.find(cfg);
			if ((m_ini.end() == mIter) || (nullptr == mIter->second))
			{
				return def;
			}
			return	mIter->second->GetString(strSection, strKey, std::forward<_Ty>(def));
		}

		template <class _Ty>
		bool SetValue(ini::cfgs cfg, const std::string& strSection, const std::string& strKey, _Ty&& val)
		{
			const auto& mIter = m_ini.find(cfg);
			if ((m_ini.end() == mIter) || (nullptr == mIter->second))
			{
				return false;
			}
			return mIter->second->SetValue(strSection, strKey, val);
		}
	
		bool Load();
	private:
		std::unordered_map<ini::cfgs, CIniFile*> m_ini;
	};
}

#endif
