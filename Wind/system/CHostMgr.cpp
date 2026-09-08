#include "CHostMgr.h"

#include "../common/utility.h"
#include "../ini/CINIHandler.h"

#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr unsigned int portLowLimit = 0;
	constexpr unsigned int portUpLimit = 65535;
}

CHostMgr::CHostMgr()
{
	Initialize();
}

bool CHostInfo::Valid() const
{
	return !m_strName.empty() && !m_strHost.empty() && utility::between(m_nPort, portLowLimit, portUpLimit);
}

bool CHostInfo::Deserialize(const std::string& key, const std::string& info)
{
	if (key.empty())
	{
		return false;
	}

	std::vector<std::string_view> values;
	if (4 != utility::split(info, values, '_', false))
	{
		return false;
	}

	unsigned int port = 0;
	if (!utility::to_number(std::string(values.at(2)), port) || !utility::between(port, portLowLimit, portUpLimit))
	{
		return false;
	}

	m_strKey = key;
	m_strName = values.at(0);
	m_strHost = values.at(1);
	m_nPort = port;
	m_bEnabled = "1" == values.at(3);
	return true;
}

bool CHostInfo::operator==(const CHostInfo& arg) const
{
	return (m_strHost == arg.m_strHost) && (m_nPort == arg.m_nPort);
}

std::string CHostInfo::Serialize() const
{
	if (!Valid())
	{
		return {};
	}
	return m_strName + "_" + m_strHost + "_" + std::to_string(m_nPort) + "_" + (m_bEnabled ? "1" : "0");
}

void CHostMgr::Initialize()
{
	auto entries = ini::CINIHandler::InstanceRef().GetSection(ini::Config::System, "Server");
	m_hosts.clear();
	m_hosts.reserve(entries.size());
	for (const auto& [key, value] : entries)
	{
		if ("connect_fast" == key)
		{
			m_bConnectFast = "1" == value;
		}
		else if (0 == key.find("host"))
		{
			CHostInfo host;
			if (host.Deserialize(key, value))
			{
				m_hosts.emplace(key, std::move(host));
			}
		}
	}
}

const std::unordered_map<std::string, CHostInfo>& CHostMgr::GetHosts() const
{
	return m_hosts;
}

std::optional<CHostInfo> CHostMgr::GetActiveHost() const
{
	for (const auto& value : m_hosts)
	{
		const CHostInfo& info = value.second;
		if (info.m_bEnabled && info.Valid())
		{
			return info;
		}
	}
	return std::nullopt;
}

bool CHostMgr::SetConnectFast(bool enabled)
{
	bool previous = m_bConnectFast;
	m_bConnectFast = enabled;
	return previous;
}

bool CHostMgr::IsConnectFast() const
{
	return m_bConnectFast;
}
