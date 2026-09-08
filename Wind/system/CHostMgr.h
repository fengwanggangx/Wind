#ifndef MARY_CONFIGURATION_CSERVERSETTINGS_H
#define MARY_CONFIGURATION_CSERVERSETTINGS_H

#include "../common/ISingleton.h"

#include <optional>
#include <string>
#include <unordered_map>

struct CHostInfo
{
	std::string m_strKey;
	std::string m_strName;
	std::string m_strHost;
	unsigned int m_nPort{0};

	bool m_bEnabled{false};

	bool operator==(const CHostInfo& arg) const;

	bool Valid() const;

	std::string Serialize() const;
	bool Deserialize(const std::string& key, const std::string& info);
};

class CHostMgr final
{
public:
	CHostMgr();

public:
	std::string Key() const;
	const std::unordered_map<std::string, CHostInfo>& GetHosts() const;
	std::optional<CHostInfo> GetActiveHost() const;

	bool SetConnectFast(bool enabled);
	bool IsConnectFast() const;

private:
	void Initialize();

private:
	bool m_bConnectFast{ false };
	std::unordered_map<std::string, CHostInfo> m_hosts;
};

#endif
