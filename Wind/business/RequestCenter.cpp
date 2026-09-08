#include "RequestCenter.h"

#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../hqmarket/CSession.h"
#include "../hqmarket/v1/market.pb.h"
#include "../network/CNetTools.h"
#include "../network/common_net.h"
#include "../request/request.h"

#include <array>
#include <cctype>
#include <chrono>
#include <random>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace request
{
	bool IsAuthRequest(const CRequest& req)
	{
		return "subscribe" != req.GetCmd();
	}

	bool IsRegisterRequest(const CRequest& req)
	{
		return "register" != req.GetCmd();
	}

	bool IsSubcriptionRequest(const CRequest& req)
	{
		return "subscribe" == req.GetCmd();
	}

	bool IsUnSubcriptionRequest(const CRequest& req)
	{
		return "unsubscribe" == req.GetCmd();
	}

	CRequest Auth(const std::string& strToken, const std::string& strPassword)
	{
		CRequest req;
		req.SetType(CRequest::Type::QUERY_AUTH);
		req.SetCmd("auth");
		req.SetExtraData("token", strToken);
		req.SetExtraData("password", strPassword);
		return req;
	}

	CRequest Subscription(const market::CQuoteInfo& quote)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("subscribe");
		req.SetExtraData("security", quote.m_security.String());
		req.SetExtraData("channel", market::GetChannelString(quote.m_channel));
		return req;
	}

	CRequest UnSubscription(const market::CQuoteInfo& quote)
	{
		CRequest req = Subscription(quote);
		req.SetCmd("unsubscribe");
		return req;
	}

	CRequest QueryQuote(const market::CSecurity& security)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("query_quote");
		req.SetExtraData("security", security.String());
		return req;
	}

	CRequest QueryBars(const market::CSecurity& security, market::Channel channel, std::int64_t nBeginTime, std::int64_t nEndTime)
	{
		CRequest req;
		req.SetType(CRequest::Type::HQMARKET);
		req.SetCmd("query_bars");
		req.SetExtraData("security", security.String());
		req.SetExtraData("channel", market::GetChannelString(channel));
		req.SetExtraData("begin_time_ms", std::to_string(nBeginTime));
		req.SetExtraData("end_time_ms", std::to_string(nEndTime));
		return req;
	}

	CRequest HeartBeat()
	{
		CRequest req;
		req.SetType(CRequest::Type::HEARTBEAT);
		req.SetCmd("heartbeat");
		std::int64_t nClientTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		req.SetExtraData("client_time_ms", std::to_string(nClientTime));
		return req;
	}
}

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
	constexpr int AuthenticationRequired = 1005;
	constexpr int InvalidSubscription = 1006;

	struct PendingSubscription
	{
		net::_TyConnectionId m_id{ -1 };
		_TyRequestId m_requestId{ 0 };
	};

	CSession* Session{ nullptr };
	std::mutex StateMutex;
	std::unordered_set<net::_TyConnectionId> AuthenticatedClients;
	std::unordered_set<std::string> LoginTokens;
	std::unordered_map<net::_TyConnectionId, std::unordered_set<std::string>> ClientSubscriptions;
	std::unordered_map<std::string, std::unordered_set<net::_TyConnectionId>> SubscriptionClients;
	std::unordered_map<std::string, market::CQuoteInfo> SubscriptionInfo;
	std::unordered_map<std::string, std::vector<PendingSubscription>> PendingSubscriptions;

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

	market::Exchange ParseExchange(const std::string& strExchange)
	{
		static const std::unordered_map<std::string, market::Exchange> exchanges
		{
			{ "SSE", market::Exchange::sse }, { "SZSE", market::Exchange::szse }, { "BSE", market::Exchange::bse },
			{ "HKEX", market::Exchange::hkex }, { "CFFEX", market::Exchange::cffex }, { "SHFE", market::Exchange::shfe },
			{ "DCE", market::Exchange::dce }, { "CZCE", market::Exchange::czce }, { "INE", market::Exchange::ine },
			{ "GFEX", market::Exchange::gfex }, { "NASDAQ", market::Exchange::nasdaq }, { "NYSE", market::Exchange::nyse },
			{ "CRYPTO", market::Exchange::crypto }
		};
		auto iter = exchanges.find(strExchange);
		return exchanges.end() == iter ? market::Exchange::unknown : iter->second;
	}

	market::Channel ParseChannel(const std::string& strChannel)
	{
		if ("quote" == strChannel)
		{
			return market::Channel::quote;
		}
		if ("depth" == strChannel)
		{
			return market::Channel::depth;
		}
		return market::Channel::unknown;
	}

	market::CQuoteInfo GetQuoteInfo(const CRequest& req)
	{
		std::string strSecurity = req.GetExtraData("security");
		std::size_t nDot = strSecurity.rfind('.');
		if ((std::string::npos == nDot) || (0 == nDot) || (strSecurity.size() - 1 == nDot))
		{
			return market::CQuoteInfo("", market::Exchange::unknown, market::Channel::unknown);
		}
		return market::CQuoteInfo(strSecurity.substr(0, nDot), ParseExchange(strSecurity.substr(nDot + 1)), ParseChannel(req.GetExtraData("channel")));
	}

	void SendSubscriptionResponse(net::_TyConnectionId id, _TyRequestId requestId, bool bAccepted, const std::string& strReason)
	{
		CRequest response;
		response.SetId(requestId);
		response.SetType(CRequest::Type::HQMARKET);
		response.SetCmd("subscription_ack");
		response.SetReturnData("accepted", bAccepted ? "true" : "false");
		response.SetReturnData("reason", strReason);
		net::SendRequest(id, response);
	}

	std::string GetMarketResponseKey(const CRequest& response)
	{
		std::string strPayload = response.GetReturnData("data");
		std::string strType = response.GetReturnData("data_type");
		if (("hqmarket.market.v1.QuoteData" == strType) || ("hqmarket.market.v1.DepthData" == strType))
		{
			hqmarket::market::v1::Instrument instrument;
			hqmarket::market::v1::Channel channel = hqmarket::market::v1::CHANNEL_UNSPECIFIED;
			if ("hqmarket.market.v1.QuoteData" == strType)
			{
				hqmarket::market::v1::QuoteData value;
				if (!value.ParseFromString(strPayload))
				{
					return {};
				}
				instrument = value.instrument();
				channel = hqmarket::market::v1::CHANNEL_QUOTE;
			}
			else
			{
				hqmarket::market::v1::DepthData value;
				if (!value.ParseFromString(strPayload))
				{
					return {};
				}
				instrument = value.instrument();
				channel = hqmarket::market::v1::CHANNEL_DEPTH;
			}
			market::Exchange exchange = static_cast<market::Exchange>(static_cast<int>(instrument.exchange()));
			market::Channel marketChannel = static_cast<market::Channel>(static_cast<int>(channel));
			return market::CQuoteInfo(instrument.symbol(), exchange, marketChannel).String();
		}
		if ("hqmarket.market.v1.SubscriptionAck" == strType)
		{
			hqmarket::market::v1::SubscriptionAck ack;
			if (!ack.ParseFromString(strPayload) || (0 == ack.results_size()))
			{
				return {};
			}
			const hqmarket::market::v1::SubscriptionResult& result = ack.results(0);
			market::Exchange exchange = static_cast<market::Exchange>(static_cast<int>(result.instrument().exchange()));
			market::Channel channel = static_cast<market::Channel>(static_cast<int>(result.channel()));
			return market::CQuoteInfo(result.instrument().symbol(), exchange, channel).String();
		}
		return {};
	}

	void HandleMarketResponse(const CRequest& req)
	{
		std::string strKey = GetMarketResponseKey(req);
		if (strKey.empty())
		{
			return;
		}

		std::string strCmd = req.GetCmd();
		if ("subscription_ack" == strCmd)
		{
			std::vector<PendingSubscription> pending;
			bool bAccepted = ("1" == req.GetReturnData("accepted");
			std::string strReason = req.GetReturnData("reason");
			{
				std::lock_guard<std::mutex> lock(StateMutex);
				auto pendingIter = PendingSubscriptions.find(strKey);
				if (PendingSubscriptions.end() == pendingIter)
				{
					return;
				}
				pending = std::move(pendingIter->second);
				PendingSubscriptions.erase(pendingIter);
				if (!bAccepted)
				{
					auto clientsIter = SubscriptionClients.find(strKey);
					if (SubscriptionClients.end() != clientsIter)
					{
						for (net::_TyConnectionId id : clientsIter->second)
						{
							ClientSubscriptions[id].erase(strKey);
						}
						SubscriptionClients.erase(clientsIter);
					}
					SubscriptionInfo.erase(strKey);
				}
			}
			for (const auto& item : pending)
			{
				SendSubscriptionResponse(item.m_id, item.m_requestId, bAccepted, strReason);
			}
			return;
		}

		std::vector<net::_TyConnectionId> clients;
		{
			std::lock_guard<std::mutex> lock(StateMutex);
			auto iter = SubscriptionClients.find(strKey);
			if (SubscriptionClients.end() != iter)
			{
				clients.assign(iter->second.begin(), iter->second.end());
			}
		}
		for (net::_TyConnectionId id : clients)
		{
			net::SendRequest(id, req);
		}
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

	bool Login(const CRequest& req, std::string& strToken)
	{
		std::string strAccount = req.GetExtraData("user");
		std::string strPassword = req.GetExtraData("password");
		if (!IsAccountValid(strAccount) || !IsPasswordValid(strPassword))
		{
			SendResponse(req, InvalidCredentials, "账号或密码错误");
			return false;
		}

		db::_TyDBPtr db = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == db)
		{
			SendResponse(req, StorageUnavailable, "用户数据库暂不可用");
			return false;
		}

		std::string sql = "SELECT user_id, account FROM table_user WHERE account=" + Utf8Literal(strAccount)
			+ " AND password_hash=UNHEX(SHA2(CONCAT(password_salt,UNHEX('" + ToHex(strPassword)
			+ "')),256)) AND status=1 LIMIT 1";
		const db::_TyTableInfo& table = db->ExecQuery(sql);
		if (table.second.empty())
		{
			SendResponse(req, InvalidCredentials, "账号或密码错误");
			return false;
		}

		CRequest response;
		response.SetId(req.GetId());
		response.SetType(req.GetType());
		response.SetCmd(req.GetCmd());
		response.SetReturnData("status", "ok");
		response.SetReturnData("user_id", table.second.front().at(0));
		response.SetReturnData("account", table.second.front().at(1));
		strToken = MakeSaltHex();
		response.SetReturnData("token", strToken);
		net::SendRequest(req.GetConnectionId(), response);
		return true;
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

void InitializeRequestCenter(CSession* pSession)
{
	Session = pSession;
	if (nullptr != Session)
	{
		Session->RegisterHandler(HandleMarketResponse);
	}
}

static int HandleClientRequest(const CRequest& req)
{
	net::_TyConnectionId id = static_cast<net::_TyConnectionId>(req.GetConnectionId());
	{
		std::lock_guard<std::mutex> lock(StateMutex);
		if (AuthenticatedClients.end() == AuthenticatedClients.find(id))
		{
			net::SendError(id, req, AuthenticationRequired, "authentication required");
			return 0;
		}
	}

	if (!IsSubcriptionRequest(req) && !IsUnSubcriptionRequest(req))
	{
		net::SendError(id, req, InvalidSubscription, "unsupported request");
		return 0;
	}
	market::CQuoteInfo quote = GetQuoteInfo(req);
	if (!quote.IsValid())
	{
		net::SendError(id, req, InvalidSubscription, "invalid security or unsupported subscription channel");
		return 0;
	}
	std::string strKey = quote.String();

	if (IsSubcriptionRequest(req))
	{
		bool bSendUpstream = false;
		bool bPending = false;
		{
			std::lock_guard<std::mutex> lock(StateMutex);
			std::unordered_set<std::string>& subscriptions = ClientSubscriptions[id];
			if (!subscriptions.emplace(strKey).second)
			{
				SendSubscriptionResponse(id, req.GetId(), true, "already subscribed");
				return 1;
			}
			std::unordered_set<net::_TyConnectionId>& clients = SubscriptionClients[strKey];
			bSendUpstream = clients.empty();
			clients.emplace(id);
			SubscriptionInfo.insert_or_assign(strKey, quote);
			bPending = PendingSubscriptions.end() != PendingSubscriptions.find(strKey);
			if (bSendUpstream || bPending)
			{
				PendingSubscriptions[strKey].push_back(PendingSubscription{ id, req.GetId() });
			}
		}
		if (bSendUpstream)
		{
			if ((nullptr == Session) || !Session->SubscribeQuote(quote))
			{
				{
					std::lock_guard<std::mutex> lock(StateMutex);
					ClientSubscriptions[id].erase(strKey);
					SubscriptionClients.erase(strKey);
					SubscriptionInfo.erase(strKey);
					PendingSubscriptions.erase(strKey);
				}
				SendSubscriptionResponse(id, req.GetId(), false, "HQMarket is unavailable");
				return 0;
			}
			return 1;
		}
		if (!bPending)
		{
			SendSubscriptionResponse(id, req.GetId(), true, "ok");
		}
		return 1;
	}

	bool bSendUpstream = false;
	{
		std::lock_guard<std::mutex> lock(StateMutex);
		auto clientIter = ClientSubscriptions.find(id);
		if ((ClientSubscriptions.end() == clientIter) || (0 == clientIter->second.erase(strKey)))
		{
			SendSubscriptionResponse(id, req.GetId(), true, "not subscribed");
			return 1;
		}
		if (clientIter->second.empty())
		{
			ClientSubscriptions.erase(clientIter);
		}
		auto clientsIter = SubscriptionClients.find(strKey);
		if (SubscriptionClients.end() != clientsIter)
		{
			clientsIter->second.erase(id);
			bSendUpstream = clientsIter->second.empty();
			if (bSendUpstream)
			{
				SubscriptionClients.erase(clientsIter);
				SubscriptionInfo.erase(strKey);
				PendingSubscriptions.erase(strKey);
			}
		}
	}
	if (bSendUpstream && ((nullptr == Session) || !Session->UnsubscribeQuote(quote)))
	{
		SendSubscriptionResponse(id, req.GetId(), false, "HQMarket is unavailable");
		return 0;
	}
	SendSubscriptionResponse(id, req.GetId(), true, "ok");
	return 1;
}

static int HandleClientDisconnected(net::_TyConnectionId id)
{
	std::vector<market::CQuoteInfo> removed;
	{
		std::lock_guard<std::mutex> lock(StateMutex);
		AuthenticatedClients.erase(id);
		auto clientIter = ClientSubscriptions.find(id);
		if (ClientSubscriptions.end() != clientIter)
		{
			for (const std::string& strKey : clientIter->second)
			{
				auto clientsIter = SubscriptionClients.find(strKey);
				if (SubscriptionClients.end() == clientsIter)
				{
					continue;
				}
				clientsIter->second.erase(id);
				if (clientsIter->second.empty())
				{
					removed.emplace_back(SubscriptionInfo.at(strKey));
					SubscriptionClients.erase(clientsIter);
					SubscriptionInfo.erase(strKey);
					PendingSubscriptions.erase(strKey);
				}
			}
			ClientSubscriptions.erase(clientIter);
		}
	}
	if (nullptr != Session)
	{
		for (const market::CQuoteInfo& quote : removed)
		{
			Session->UnsubscribeQuote(quote);
		}
	}
	return 1;
}

static int HandleReAuthenticationRequest(const CRequest& req)
{
	std::string strToken = req.GetExtraData("token");
	bool bAccepted = false;
	{
		std::lock_guard<std::mutex> lock(StateMutex);
		bAccepted = LoginTokens.end() != LoginTokens.find(strToken);
		if (bAccepted)
		{
			AuthenticatedClients.emplace(req.GetConnectionId());
		}
	}
	if (bAccepted)
	{
		SendResponse(req, 0, "认证成功");
		return 0;
	}
	SendResponse(req, InvalidCredentials, "登录状态已失效");
	return 0;
}

static int HandleAuthenticationRequest(const CRequest& req)
{
	//断线重连
	std::string strToken = req.GetExtraData("token");
	if (!strToken.empty())
	{
		return HandleReAuthenticationRequest(req);
	}

	//登录认证
	if ("auth" == strCmd)
	{
		if (Login(req, strToken))
		{
			std::lock_guard<std::mutex> lock(StateMutex);
			AuthenticatedClients.emplace(req.GetConnectionId());
			LoginTokens.emplace(strToken);
		}
		return 0;
	}
	net::SendError(req.GetConnectionId(), req, InvalidRequest, "unsupported authentication request");
	return 0;
}

int OnClientNetEvent(const net::CNetEvent& ev)
{
	CRequest::Type t = ev.m_request->GetType();
	if ((net::em_event::disconnected == ev.m_event) || (net::em_event::error == ev.m_event) || (net::em_event::timeout == ev.m_event))
	{
		return HandleClientDisconnected(ev.m_connection_id);
	}

	if (nullptr == ev.m_request)
	{
		return 0;
	}

	if (IsRegisterRequest(*ev.m_request))
	{
		return Register(*ev.m_request);
	}
	if (IsAuthRequest(*ev.m_request))
	{
		return HandleAuthenticationRequest(*ev.m_request);
	}
	return HandleClientRequest(*ev.m_request);
}
