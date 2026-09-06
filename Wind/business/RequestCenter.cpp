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

	std::string ToHex(const std::string& value)
	{
		constexpr std::array<char, 16> digits{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
		std::string result;
		result.reserve(value.size() * 2);
		for (unsigned char character : value)
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

	bool IsAccountValid(const std::string& account)
	{
		if ((MinAccountLength > account.size()) || (MaxAccountLength < account.size()))
		{
			return false;
		}
		for (unsigned char character : account)
		{
			if ((0 == std::isalnum(character)) && ('_' != character) && ('-' != character) && ('.' != character) && ('@' != character))
			{
				return false;
			}
		}
		return true;
	}

	bool IsPasswordValid(const std::string& password)
	{
		return (MinPasswordLength <= password.size()) && (MaxPasswordLength >= password.size());
	}

	std::string Utf8Literal(const std::string& value)
	{
		return "CONVERT(UNHEX('" + ToHex(value) + "') USING utf8mb4)";
	}

	void SendResponse(const CRequest& request, int errorCode, const std::string& message)
	{
		CRequest response;
		response.SetId(request.GetId());
		response.SetType(request.GetType());
		response.SetCmd(request.GetCmd());
		if (0 != errorCode)
		{
			net::SetError(response, errorCode, message);
		}
		else
		{
			response.SetReturnData("status", "ok");
			response.SetReturnData("message", message);
		}
		net::SendRequest(request.GetConnectionId(), response);
	}

	int Login(const CRequest& request)
	{
		std::string account = request.GetExtraData("user");
		std::string password = request.GetExtraData("password");
		if (!IsAccountValid(account) || !IsPasswordValid(password))
		{
			SendResponse(request, InvalidCredentials, "账号或密码错误");
			return 0;
		}

		db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == database)
		{
			SendResponse(request, StorageUnavailable, "用户数据库暂不可用");
			return 0;
		}

		std::string sql = "SELECT user_id, account FROM table_user WHERE account=" + Utf8Literal(account)
			+ " AND password_hash=UNHEX(SHA2(CONCAT(password_salt,UNHEX('" + ToHex(password)
			+ "')),256)) AND status=1 LIMIT 1";
		const db::_TyTableInfo& table = database->ExecQuery(sql);
		if (table.second.empty())
		{
			SendResponse(request, InvalidCredentials, "账号或密码错误");
			return 0;
		}

		CRequest response;
		response.SetId(request.GetId());
		response.SetType(request.GetType());
		response.SetCmd(request.GetCmd());
		response.SetReturnData("status", "ok");
		response.SetReturnData("user_id", table.second.front().at(0));
		response.SetReturnData("account", table.second.front().at(1));
		net::SendRequest(request.GetConnectionId(), response);
		return 1;
	}

	int Register(const CRequest& request)
	{
		std::string account = request.GetExtraData("user");
		std::string password = request.GetExtraData("password");
		if (!IsAccountValid(account))
		{
			SendResponse(request, InvalidRequest, "账号需为 3-64 位字母、数字或 _-.@");
			return 0;
		}
		if (!IsPasswordValid(password))
		{
			SendResponse(request, InvalidRequest, "密码长度需为 8-128 位");
			return 0;
		}

		db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == database)
		{
			SendResponse(request, StorageUnavailable, "用户数据库暂不可用");
			return 0;
		}

		std::string accountLiteral = Utf8Literal(account);
		const db::_TyTableInfo& existing = database->ExecQuery("SELECT user_id FROM table_user WHERE account=" + accountLiteral + " LIMIT 1");
		if (!existing.second.empty())
		{
			SendResponse(request, AccountExists, "账号已存在");
			return 0;
		}

		std::string saltHex = MakeSaltHex();
		std::string sql = "INSERT INTO table_user(account,password_hash,password_salt,status) VALUES("
			+ accountLiteral + ",UNHEX(SHA2(CONCAT(UNHEX('" + saltHex + "'),UNHEX('" + ToHex(password)
			+ "')),256)),UNHEX('" + saltHex + "'),1)";
		if (0 != database->ExecUpdate(sql))
		{
			SendResponse(request, AccountExists, "账号已存在或注册失败");
			return 0;
		}

		SendResponse(request, 0, "注册成功");
		return 1;
	}
}

bool InitializeUserStorage()
{
	db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == database)
	{
		return false;
	}
	return 0 == database->ExecUpdate(
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

int HandleUserRequest(const net::CNetEvent& event)
{
	if ((net::em_event::request != event.m_event) || (nullptr == event.m_request))
	{
		return 1;
	}

	const CRequest& request = *event.m_request;
	if ((CRequest::Type::QUERY_AUTH == request.GetType()) && ("auth" == request.GetCmd()))
	{
		return Login(request);
	}
	if ((CRequest::Type::UPDATE_AUTH == request.GetType()) && ("register" == request.GetCmd()))
	{
		return Register(request);
	}
	return 1;
}
