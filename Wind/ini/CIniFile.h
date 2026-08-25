#ifndef __CINIFILE_H__
#define __CINIFILE_H__

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <SimpleIni/SimpleIni.h>


namespace ini
{
	class CIniFile
	{
	public:
		explicit CIniFile(const std::string& strFile);
		~CIniFile();
	public:
		int GetInt(const std::string& strSection, const std::string& strKey, int nDefault) const;
		bool GetBool(const std::string& strSection, const std::string& strKey, bool bDefault) const;
		double GetDouble(const std::string& strSection, const std::string& strKey, double fDefault) const;
		std::string GetString(const std::string& strSection, const std::string& strKey, const std::string& strDefault) const;

		bool SetInt(const std::string& strSection, const std::string& strKey, int nVal);
		bool SetBool(const std::string& strSection, const std::string& strKey, bool bVal);
		bool SetDouble(const std::string& strSection, const std::string& strKey, double fVal);
		bool SetString(const std::string& strSection, const std::string& strKey, const std::string& strVal);


		int GetValue(const std::string& strSection, const std::string& strKey, int nDefault) const;
		bool GetValue(const std::string& strSection, const std::string& strKey, bool bDefault) const;
		double GetValue(const std::string& strSection, const std::string& strKey, double fDefault) const;
		std::string GetValue(const std::string& strSection, const std::string& strKey, const std::string& strDefault) const;

		bool SetValue(const std::string& strSection, const std::string& strKey, int nVal);
		bool SetValue(const std::string& strSection, const std::string& strKey, bool bVal);
		bool SetValue(const std::string& strSection, const std::string& strKey, double fVal);
		bool SetValue(const std::string& strSection, const std::string& strKey, const std::string& strVal);

	private:
		bool IsSectionExists(const std::string& strSection) const;
		std::vector<std::string> GetSections() const;
	private:
		bool Load(const std::string& strFile);
		bool Save() const;
	private:
		mutable std::shared_mutex m_mtx_parser;
		std::unique_ptr<CSimpleIniA> m_pParser;
		std::string m_strFileName;
		std::atomic_bool m_bUpdated{ false };
	};
}

#endif
