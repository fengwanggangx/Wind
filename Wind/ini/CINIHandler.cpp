#include "CINIHandler.h"
#include <fstream>


namespace ini
{
	CINIHandler::CINIHandler()
	{
		Load();
	}

	CINIHandler::~CINIHandler()
	{
	}

	std::unordered_map<ini::cfgs, std::string> s_files;
	bool CINIHandler::Load()
	{
		if (!s_files.empty())
		{
			return false;
		}
		for (const auto& f : s_files)
		{
			m_ini[f.first] = new CIniFile(f.second);
		}
		return true;
	}
}