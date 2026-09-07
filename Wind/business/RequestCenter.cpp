#include "RequestCenter.h"

#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../network/CNetTools.h"
#include "../network/common_net.h"
#include "../request/request.h"

#include <array>
#include <cctype>
#include <random>
#include <string>

namespace
{
	constexpr int InvalidRequest = 1001;
	constexpr int InvalidCredentials = 1002;
	constexpr int AccountExists = 1003;
	constexpr int StorageUnavailable = 1004;
	constexpr std::size_t MinAccountLength = 3;
	constexpr std::size_t MaxAccountLength = 64;
	constexpr std::size_t MinPasswordLength = 8;
	constexpr std::size_t MaxPasswordLength = 128;

	std::string ToHex(const std::string& strValue)
	{
		constexpr std::array<char, 16> digits{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
		std::string result;
		result.reserve(strValue.size() * 2);
		for (unsigned char character : strValue)
		{
			result.push_back(digits[character >> 4]);
			result.push_back(digits[character & 0x0F]);
		}
		return result;
	}

	std::string MakeSaltHex()
	{
		std::random_device randomDevice;
		std::array<unsigned char, 16> salt;
		for (unsigned char& value : salt)
		{
			value = static_cast<unsigned char>(randomDevice());
		}
		return ToHex(std::string(reinterpret_cast<const char*>(salt.data()), salt.size()));
	}

	bool IsAccountValid(const std::string& strAccount)
	{
		if ((MinAccountLength > strAccount.size()) || (MaxAccountLength < strAccount.size()))
		{
			return false;
		}
		for (unsigned char character : strAccount)
		{
			if ((0 == std::isalnum(character)) && ('_' != character) && ('-' != character) && ('.' != character) && ('@' != character))
			{
				return false;
			}
		}
		return true;
	}

	bool IsPasswordValid(const std::string& strPassword)
	{
		return (MinPasswordLength <= strPassword.size()) && (MaxPasswordLength >= strPassword.size());
	}

	std::string Utf8Literal(const std::string& strValue)
	{
		return "CONVERT(UNHEX('" + ToHex(strValue) + "') USING utf8mb4)";
	}

	void SendResponse(const CRequest& req, int nErrorCode, const std::string& strMessage)
	{
		CRequest response;
		response.SetId(req.GetId());
		response.SetType(req.GetType());
		response.SetCmd(req.GetCmd());
		if (0 != nErrorCode)
		{
			net::SetError(response, nErrorCode, strMessage);
		}
		else
		{
			response.SetReturnData("status", "ok");
			response.SetReturnData("message", strMessage);
		}
		net::SendRequest(req.GetConnectionId(), response);
	}

	int Login(const CRequest& req)
	{
		std::string strAccount = req.GetExtraData("user");
		std::string strPassword = req.GetExtraData("password");
		if (!IsAccountValid(strAccount) || !IsPasswordValid(strPassword))
		{
			SendResponse(req, InvalidCredentials, "账号或密码错误");
			return 0;
		}

		db::_TyDBPtr db = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == db)
		{
			SendResponse(req, StorageUnavailable, "用户数据库暂不可用");
			return 0;
		}

		std::string sql = "SELECT user_id, account FROM table_user WHERE account=" + Utf8Literal(strAccount)
			+ " AND password_hash=UNHEX(SHA2(CONCAT(password_salt,UNHEX('" + ToHex(strPassword)
			+ "')),256)) AND status=1 LIMIT 1";
		const db::_TyTableInfo& table = db->ExecQuery(sql);
		if (table.second.empty())
		{
			SendResponse(req, InvalidCredentials, "账号或密码错误");
			return 0;
		}

		CRequest response;
		response.SetId(req.GetId());
		response.SetType(req.GetType());
		response.SetCmd(req.GetCmd());
		response.SetReturnData("status", "ok");
		response.SetReturnData("user_id", table.second.front().at(0));
		response.SetReturnData("account", table.second.front().at(1));
		net::SendRequest(req.GetConnectionId(), response);
		return 1;
	}

	int Register(const CRequest& req)
	{
		std::string strAccount = req.GetExtraData("user");
		std::string strPassword = req.GetExtraData("password");
		if (!IsAccountValid(strAccount))
		{
			SendResponse(req, InvalidRequest, "账号需为 3-64 位字母、数字或 _-.@");
			return 0;
		}
		if (!IsPasswordValid(strPassword))
		{
			SendResponse(req, InvalidRequest, "密码长度需为 8-128 位");
			return 0;
		}

		db::_TyDBPtr db = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == db)
		{
			SendResponse(req, StorageUnavailable, "用户数据库暂不可用");
			return 0;
		}

		std::string strAccountLiteral = Utf8Literal(strAccount);
		const db::_TyTableInfo& table = db->ExecQuery("SELECT user_id FROM table_user WHERE account=" + strAccountLiteral + " LIMIT 1");
		if (!table.second.empty())
		{
			SendResponse(req, AccountExists, "账号已存在");
			return 0;
		}

		std::string saltHex = MakeSaltHex();
		std::string sql = "INSERT INTO table_user(account,password_hash,password_salt,status) VALUES("
			+ strAccountLiteral + ",UNHEX(SHA2(CONCAT(UNHEX('" + saltHex + "'),UNHEX('" + ToHex(strPassword)
			+ "')),256)),UNHEX('" + saltHex + "'),1)";
		if (0 != db->ExecUpdate(sql))
		{
			SendResponse(req, AccountExists, "账号已存在或注册失败");
			return 0;
		}

		SendResponse(req, 0, "注册成功");
		return 1;
	}
}

bool InitializeUserStorage()
{
	db::_TyDBPtr db = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == db)
	{
		return false;
	}
	return 0 == db->ExecUpdate(
		"CREATE TABLE IF NOT EXISTS table_user("
		"user_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"account VARCHAR(64) NOT NULL,"
		"password_hash BINARY(32) NOT NULL,"
		"password_salt BINARY(16) NOT NULL,"
		"status TINYINT UNSIGNED NOT NULL DEFAULT 1,"
		"created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
		"PRIMARY KEY(user_id),UNIQUE KEY uk_table_user_account(account))"
		" ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin");
}

int HandleClientRequest(const CRequest& req)
{
	return 1;
}

int OnClientNetEvent(const net::CNetEvent& ev)
{
	if ((net::em_event::request != ev.m_event) || (nullptr == ev.m_request))
	{
		return 1;
	}

	const CRequest& req = *ev.m_request;
	CRequest::Type t = req.GetType();
	bool bAuthRequest = (CRequest::Type::QUERY_AUTH == t) || (CRequest::Type::UPDATE_AUTH == t);
	std::string strCmd = req.GetCmd();

	if (bAuthRequest)
	{
		if (("auth" == strCmd))
		{
			return Login(req);
		}
		
		if ("register" == strCmd)
		{
			return Register(req);
		}
	}

	return HandleClientRequest(req);
}
