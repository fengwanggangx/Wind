#include "CIniFile.h"
#include <mutex>
#include <fstream>

namespace ini
{
	CIniFile::CIniFile(const std::string& strFile) : m_pParser(std::make_unique<CSimpleIniA>(false, false, false)), m_strFileName(strFile)
	{
		Load(strFile);
	}

	CIniFile::~CIniFile()
	{
		Save();
	}

	bool CIniFile::Load(const std::string& strFile)
	{
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
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
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
		SI_Error ret = m_pParser->SaveFile(m_strFileName.c_str());
		return ret >= 0;
	}

	int CIniFile::GetInt(const std::string& strSection, const std::string& strKey, int nDefault) const
	{
		std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
		return m_pParser->GetLongValue(strSection.c_str(), strKey.c_str(), nDefault);
	}


	bool CIniFile::GetBool(const std::string& strSection, const std::string& strKey, bool bDefault) const
	{
		std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
		return m_pParser->GetBoolValue(strSection.c_str(), strKey.c_str(), bDefault);
	}

	double CIniFile::GetDouble(const std::string& strSection, const std::string& strKey, double fDefault) const
	{
		std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
		return m_pParser->GetDoubleValue(strSection.c_str(), strKey.c_str(), fDefault);
	}

	std::string CIniFile::GetString(const std::string& strSection, const std::string& strKey, const std::string& strDefault) const
	{
		std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
		return m_pParser->GetValue(strSection.c_str(), strKey.c_str(), strDefault.c_str());
	}

	bool CIniFile::SetInt(const std::string& strSection, const std::string& strKey, int nVal)
	{
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
		bool bRet = SI_FAIL != m_pParser->SetLongValue(strSection.c_str(), strKey.c_str(), nVal);
		m_bUpdated = m_bUpdated || bRet;
		return bRet;
	}

	bool CIniFile::SetBool(const std::string& strSection, const std::string& strKey, bool bVal)
	{
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
		bool bRet = SI_FAIL != m_pParser->SetBoolValue(strSection.c_str(), strKey.c_str(), bVal);
		m_bUpdated = m_bUpdated || bRet;
		return bRet;
	}

	bool CIniFile::SetDouble(const std::string& strSection, const std::string& strKey, double fVal)
	{
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
		bool bRet = SI_FAIL != m_pParser->SetDoubleValue(strSection.c_str(), strKey.c_str(), fVal);
		m_bUpdated = m_bUpdated || bRet;
		return bRet;
	}

	bool CIniFile::SetString(const std::string& strSection, const std::string& strKey, const std::string& strVal)
	{
		std::unique_lock<std::shared_mutex> lck(m_mtx_parser);
		bool bRet = SI_FAIL != m_pParser->SetValue(strSection.c_str(), strKey.c_str(), strVal.c_str());
		m_bUpdated = m_bUpdated || bRet;
		return bRet;
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
		std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
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
		std::vector<std::string> ret;
		CSimpleIniA::TNamesDepend sections;

		{
			std::shared_lock<std::shared_mutex> lck(m_mtx_parser);
			m_pParser->GetAllSections(sections);
		}

		for (const auto& s : sections) 
		{
			ret.emplace_back(s.pItem);
		}
		return ret;
	}
}
