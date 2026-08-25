#include "CIniFile.h"
#include <fstream>
#include "./SimpleIni/SimpleIni.h"


namespace ini
{
	CIniFile::CIniFile(const std::string& strFile) : m_pParser(new CSimpleIniA(false, false, false)), m_strFileName(strFile)
	{
		Load(strFile);
	}

	CIniFile::~CIniFile()
	{
		Save();
	}

	bool CIniFile::Load(const std::string& strFile)
	{
		SI_Error ret = m_pParser->LoadFile(strFile.c_str());
		if (ret < 0)
		{
			if (ret == SI_FILE)
			{
				std::ofstream f(strFile);
				if (!f.is_open())
				{
					return false;
				}
				f.close();
			}
		}
		return true;
	}

	bool CIniFile::Save() const 
	{
		SI_Error ret = m_pParser->SaveFile(m_strFileName.c_str());
		return ret >= 0;
	}

	int CIniFile::GetInt(const std::string& strSection, const std::string& strKey, int nDefault) const
	{
		return m_pParser->GetLongValue(strSection.c_str(), strKey.c_str(), nDefault);
	}


	bool CIniFile::GetBool(const std::string& strSection, const std::string& strKey, bool bDefault) const
	{
		return m_pParser->GetBoolValue(strSection.c_str(), strKey.c_str(), bDefault);
	}

	double CIniFile::GetDouble(const std::string& strSection, const std::string& strKey, double fDefault) const
	{
		return m_pParser->GetDoubleValue(strSection.c_str(), strKey.c_str(), fDefault);
	}

	std::string CIniFile::GetString(const std::string& strSection, const std::string& strKey, const std::string& strDefault) const
	{
		return m_pParser->GetValue(strSection.c_str(), strKey.c_str(), strDefault.c_str());
	}

	bool CIniFile::SetInt(const std::string& strSection, const std::string& strKey, int nVal)
	{
		return SI_FAIL != m_pParser->SetLongValue(strSection.c_str(), strKey.c_str(), nVal);
	}

	bool CIniFile::SetBool(const std::string& strSection, const std::string& strKey, bool bVal)
	{
		return SI_FAIL != m_pParser->SetBoolValue(strSection.c_str(), strKey.c_str(), bVal);
	}

	bool CIniFile::SetDouble(const std::string& strSection, const std::string& strKey, double fVal)
	{
		return SI_FAIL != m_pParser->SetDoubleValue(strSection.c_str(), strKey.c_str(), fVal);
	}

	bool CIniFile::SetString(const std::string& strSection, const std::string& strKey, const std::string& strVal)
	{
		return SI_FAIL != m_pParser->SetValue(strSection.c_str(), strKey.c_str(), strVal.c_str());
	}

	int CIniFile::GetValue(const std::string& strSection, const std::string& strKey, int nDefault) const
	{
		return GetInt(strSection, strKey, nDefault);
	}
	bool CIniFile::GetValue(const std::string& strSection, const std::string& strKey, bool bDefault) const
	{
		return GetBool(strSection, strKey, bDefault);
	}
	double CIniFile::GetValue(const std::string& strSection, const std::string& strKey, double fDefault) const
	{
		return GetDouble(strSection, strKey, fDefault);
	}

	std::string CIniFile::GetValue(const std::string& strSection, const std::string& strKey, const std::string& strDefault) const
	{
		return GetString(strSection, strKey, strDefault);
	}

	bool CIniFile::SetValue(const std::string& strSection, const std::string& strKey, int nVal)
	{
		return SetInt(strSection, strKey, nVal);
	}

	bool CIniFile::SetValue(const std::string& strSection, const std::string& strKey, bool bVal)
	{
		return SetBool(strSection, strKey, bVal);
	}

	bool CIniFile::SetValue(const std::string& strSection, const std::string& strKey, double fVal)
	{
		return SetDouble(strSection, strKey, fVal);
	}

	bool CIniFile::SetValue(const std::string& strSection, const std::string& strKey, const std::string& strVal)
	{
		return SetString(strSection, strKey, strVal);
	}

	bool CIniFile::IsSectionExists(const std::string& strSection) const
	{
		CSimpleIniA::TNamesDepend sections;
		m_pParser->GetAllSections(sections);

		for (const auto& s : sections) 
		{
			if (s.pItem == strSection) 
			{
				return true;
			}
		}
		return false;
	}

	std::vector<std::string> CIniFile::GetSections() const
	{
		std::vector<std::string> result;
		CSimpleIniA::TNamesDepend sections;
		m_pParser->GetAllSections(sections);

		for (const auto& s : sections) 
		{
			result.emplace_back(s.pItem);
		}
		return result;
	}
}