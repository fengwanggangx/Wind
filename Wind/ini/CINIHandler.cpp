#include "CINIHandler.h"

#include <array>
#include <filesystem>
#include <utility>

namespace ini
{
	namespace
	{
		using ConfigFile = std::pair<Config, std::filesystem::path>;

		const std::array<ConfigFile, 1> configFiles{
			ConfigFile{ Config::System, std::filesystem::path("ini") / "system.ini" }
		};
	}

	CINIHandler::CINIHandler()
	{
		Load();
	}

	CINIHandler::~CINIHandler() = default;

	bool CINIHandler::Load()
	{
		if (!m_iniFiles.empty())
		{
			return true;
		}

		for (const ConfigFile& configFile : configFiles)
		{
			m_iniFiles.emplace(configFile.first, std::make_unique<CIniFile>(configFile.second.string()));
		}
		return true;
	}
}
