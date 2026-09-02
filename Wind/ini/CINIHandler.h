#ifndef HQMARKET_INI_CINIHANDLER_H
#define HQMARKET_INI_CINIHANDLER_H

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../common/ISingleton.h"
#include "CIniFile.h"

namespace ini
{
	enum class Config
	{
		System = 0,
		Ui,
		Sqlite,
		Mysql,
		Oracle
	};

	class CINIHandler : public ISingleton<CINIHandler>
	{
		DECLARE_SINGLE_DFAULT(CINIHandler)

	public:
		template <class Type, std::enable_if_t<!std::is_convertible<Type, std::string>::value, int> = 0>
		Type GetValue(Config config, const std::string& section, const std::string& key, Type&& defaultValue) const
		{
			const auto& iter = m_iniFiles.find(config);
			if (m_iniFiles.end() == iter)
			{
				return std::forward<Type>(defaultValue);
			}
			return iter->second->GetValue(section, key, std::forward<Type>(defaultValue));
		}

		template <class Type, std::enable_if_t<std::is_convertible<Type, std::string>::value, int> = 0>
		std::string GetValue(Config config, const std::string& section, const std::string& key, Type&& defaultValue) const
		{
			const auto& iter = m_iniFiles.find(config);
			if (m_iniFiles.end() == iter)
			{
				return std::forward<Type>(defaultValue);
			}
			return iter->second->GetString(section, key, std::forward<Type>(defaultValue));
		}

		template <class Type>
		bool SetValue(Config config, const std::string& section, const std::string& key, Type&& value)
		{
			const auto& iter = m_iniFiles.find(config);
			if (m_iniFiles.end() == iter)
			{
				return false;
			}
			return iter->second->SetValue(section, key, std::forward<Type>(value));
		}

		bool Load();

	private:
		std::unordered_map<Config, std::unique_ptr<CIniFile>> m_iniFiles;
	};
}

#endif
