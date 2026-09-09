#include "CBrokerService.h"

#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../hqmarket/CSession.h"
#include "../hqmarket/v1/market.pb.h"
#include "../network/CNetTools.h"
#include "../network/CTcpServer.h"
#include "../network/common_net.h"
#include "../request/request.h"

#include <array>
#include <cctype>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
} // namespace

CBrokerService::CBrokerService(net::CTcpServer* pTcpServer, CSession* pSession) : m_pTcpServer(pTcpServer), m_pSession(pSession)
{
	m_handler =
	{
		{"register", std::bind_front(&CBrokerService::HandleRegisterAuth, this)},
		{"auth", std::bind_front(&CBrokerService::HandleAuth, this)},
		{"subscribe", std::bind_front(&CBrokerService::HandleSubscription, this)},
		{"unsubscribe", std::bind_front(&CBrokerService::HandleSubscription, this)}
	};
}

bool CBrokerService::Initialize()
{
	if ((nullptr == m_pTcpServer) || (nullptr == m_pSession))
	{
		return false;
	}
	m_pTcpServer->RegisterHandler(std::bind_front(&CBrokerService::OnNetEvent, this));
	m_pSession->RegisterHandler(std::bind_front(&CBrokerService::OnHQMarketResponse, this));
	return true;
}

std::string CBrokerService::ToHex(const std::string& strValue) const
{
	constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	std::string result;
	result.reserve(strValue.size() * 2);
	for (unsigned char character : strValue)
	{
		result.push_back(digits[character >> 4]);
		result.push_back(digits[character & 0x0F]);
	}
	return result;
}

std::string CBrokerService::MakeSaltHex() const
{
	std::random_device randomDevice;
	std::array<unsigned char, 16> salt;
	for (unsigned char& value : salt)
	{
		value = static_cast<unsigned char>(randomDevice());
	}
	return ToHex(std::string(reinterpret_cast<const char*>(salt.data()), salt.size()));
}

market::CQuoteInfo CBrokerService::GetQuoteInfo(const CRequest& req) const
{
	std::string strSecurity = req.GetExtraData("security");
	std::size_t nDot = strSecurity.rfind('.');
	if ((std::string::npos == nDot) || (0 == nDot) || (strSecurity.size() - 1 == nDot))
	{
		return market::CQuoteInfo("", market::Exchange::unknown, market::Channel::unknown);
	}
	return market::CQuoteInfo(strSecurity.substr(0, nDot), market::ParseMarket(strSecurity.substr(nDot + 1)), market::ParseChannel(req.GetExtraData("channel")));
}

void CBrokerService::SendSubscriptionResponse(net::_TyConnectionId id, _TyRequestId requestId, bool bAccepted, const std::string& strReason) const
{
	CRequest response;
	response.SetId(requestId);
	response.SetType(CRequest::Type::HQMARKET);
	response.SetCmd("subscription_ack");
	response.SetReturnData("accepted", bAccepted ? "true" : "false");
	response.SetReturnData("reason", strReason);
	net::SendRequest(id, response);
}

std::string CBrokerService::GetMarketResponseKey(const CRequest& req) const
{
	std::string strPayload = req.GetReturnData("data");
	std::string strType = req.GetReturnData("data_type");
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

void CBrokerService::OnHQMarketResponse(const CRequest& req)
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
		bool bAccepted = "1" == req.GetReturnData("accepted");
		std::string strReason = req.GetReturnData("reason");
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			auto pendingIter = m_pendingSubscriptions.find(strKey);
			if (m_pendingSubscriptions.end() == pendingIter)
			{
				return;
			}
			pending = std::move(pendingIter->second);
			m_pendingSubscriptions.erase(pendingIter);
			if (!bAccepted)
			{
				const auto mIter = m_subscriptionClients.find(strKey);
				if (m_subscriptionClients.end() != mIter)
				{
					for (net::_TyConnectionId id : mIter->second)
					{
						m_clientSubscriptions[id].erase(strKey);
					}
					m_subscriptionClients.erase(mIter);
				}
				m_subscriptionInfo.erase(strKey);
			}
		}
		for (const auto& v : pending)
		{
			SendSubscriptionResponse(v.m_id, v.m_requestId, bAccepted, strReason);
		}
		return;
	}

	std::vector<net::_TyConnectionId> clients;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		const auto mIter = m_subscriptionClients.find(strKey);
		if (m_subscriptionClients.end() != mIter)
		{
			clients.assign(mIter->second.begin(), mIter->second.end());
		}
	}
	for (net::_TyConnectionId id : clients)
	{
		net::SendRequest(id, req);
	}
}

bool CBrokerService::IsAccountValid(const std::string& strAccount) const
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

bool CBrokerService::IsPasswordValid(const std::string& strPassword) const
{
	return (MinPasswordLength <= strPassword.size()) && (MaxPasswordLength >= strPassword.size());
}

std::string CBrokerService::Utf8Literal(const std::string& strValue) const
{
	return "CONVERT(UNHEX('" + ToHex(strValue) + "') USING utf8mb4)";
}

void CBrokerService::SendResponse(const CRequest& req, int nErrorCode, const std::string& strMessage) const
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

bool CBrokerService::Login(const CRequest& req, std::string& strToken)
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

	std::string sql = "SELECT user_id, account FROM table_user WHERE account=" + Utf8Literal(strAccount) + " AND password_hash=UNHEX(SHA2(CONCAT(password_salt,UNHEX('" + ToHex(strPassword) + "')),256)) AND status=1 LIMIT 1";
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

bool CBrokerService::HandleRegisterAuth(net::_TyConnectionId, const CRequest& req)
{
	std::string strAccount = req.GetExtraData("user");
	std::string strPassword = req.GetExtraData("password");
	if (!IsAccountValid(strAccount))
	{
		SendResponse(req, InvalidRequest, "账号需为 3-64 位字母、数字或 _-.@");
		return false;
	}
	if (!IsPasswordValid(strPassword))
	{
		SendResponse(req, InvalidRequest, "密码长度需为 8-128 位");
		return false;
	}

	db::_TyDBPtr db = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == db)
	{
		SendResponse(req, StorageUnavailable, "用户数据库暂不可用");
		return false;
	}

	std::string strAccountLiteral = Utf8Literal(strAccount);
	const db::_TyTableInfo& table = db->ExecQuery("SELECT user_id FROM table_user WHERE account=" + strAccountLiteral + " LIMIT 1");
	if (!table.second.empty())
	{
		SendResponse(req, AccountExists, "账号已存在");
		return false;
	}

	std::string saltHex = MakeSaltHex();
	std::string strSQL = "INSERT INTO table_user(account,password_hash,password_salt,status) VALUES(" + strAccountLiteral + ",UNHEX(SHA2(CONCAT(UNHEX('" + saltHex + "'),UNHEX('" + ToHex(strPassword) + "')),256)),UNHEX('" + saltHex + "'),1)";
	if (0 != db->ExecUpdate(strSQL))
	{
		SendResponse(req, AccountExists, "账号已存在或注册失败");
		return false;
	}

	SendResponse(req, 0, "注册成功");
	return true;
}

bool CBrokerService::HandleSubscription(net::_TyConnectionId, const CRequest& req)
{
	net::_TyConnectionId id = static_cast<net::_TyConnectionId>(req.GetConnectionId());
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (m_authenticatedClients.end() == m_authenticatedClients.find(id))
		{
			net::SendError(id, req, AuthenticationRequired, "authentication required");
			return false;
		}
	}

	std::string strCmd = req.GetCmd();
	if (("subscribe" != strCmd) && ("unsubscribe" != strCmd))
	{
		net::SendError(id, req, InvalidSubscription, "unsupported request");
		return false;
	}
	market::CQuoteInfo quote = GetQuoteInfo(req);
	if (!quote.IsValid())
	{
		net::SendError(id, req, InvalidSubscription, "invalid security or unsupported subscription channel");
		return false;
	}
	std::string strKey = quote.String();

	if ("subscribe" == strCmd)
	{
		bool bSendUpstream = false;
		bool bPending = false;
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			auto& subscriptions = m_clientSubscriptions[id];
			if (!subscriptions.emplace(strKey).second)
			{
				SendSubscriptionResponse(id, req.GetId(), true, "already subscribed");
				return 1;
			}
			auto& clients = m_subscriptionClients[strKey];
			bSendUpstream = clients.empty();
			clients.emplace(id);
			m_subscriptionInfo.insert_or_assign(strKey, quote);
			bPending = m_pendingSubscriptions.end() != m_pendingSubscriptions.find(strKey);
			if (bSendUpstream || bPending)
			{
				m_pendingSubscriptions[strKey].push_back(PendingSubscription{id, req.GetId()});
			}
		}
		if (bSendUpstream)
		{
			if ((nullptr == m_pSession) || !m_pSession->SubscribeQuote(quote))
			{
				{
					std::lock_guard<std::mutex> lock(m_mtx_state);
					m_clientSubscriptions[id].erase(strKey);
					m_subscriptionClients.erase(strKey);
					m_subscriptionInfo.erase(strKey);
					m_pendingSubscriptions.erase(strKey);
				}
				SendSubscriptionResponse(id, req.GetId(), false, "HQMarket is unavailable");
				return false;
			}
			return true;
		}
		if (!bPending)
		{
			SendSubscriptionResponse(id, req.GetId(), true, "ok");
		}
		return true;
	}

	bool bSendUpstream = false;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		auto mIter = m_clientSubscriptions.find(id);
		if ((m_clientSubscriptions.end() == mIter) || (0 == mIter->second.erase(strKey)))
		{
			SendSubscriptionResponse(id, req.GetId(), true, "not subscribed");
			return 1;
		}
		if (mIter->second.empty())
		{
			m_clientSubscriptions.erase(mIter);
		}
		auto mmIter = m_subscriptionClients.find(strKey);
		if (m_subscriptionClients.end() != mmIter)
		{
			mmIter->second.erase(id);
			bSendUpstream = mmIter->second.empty();
			if (bSendUpstream)
			{
				m_subscriptionClients.erase(mmIter);
				m_subscriptionInfo.erase(strKey);
				m_pendingSubscriptions.erase(strKey);
			}
		}
	}
	if (bSendUpstream && ((nullptr == m_pSession) || !m_pSession->UnsubscribeQuote(quote)))
	{
		SendSubscriptionResponse(id, req.GetId(), false, "HQMarket is unavailable");
		return false;
	}
	SendSubscriptionResponse(id, req.GetId(), true, "ok");
	return true;
}

int CBrokerService::HandleDisconnected(net::_TyConnectionId id)
{
	std::vector<market::CQuoteInfo> removed;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_authenticatedClients.erase(id);
		const auto mIter = m_clientSubscriptions.find(id);
		if (m_clientSubscriptions.end() != mIter)
		{
			for (const std::string& strKey : mIter->second)
			{
				auto mmIter = m_subscriptionClients.find(strKey);
				if (m_subscriptionClients.end() == mmIter)
				{
					continue;
				}
				mmIter->second.erase(id);
				if (mmIter->second.empty())
				{
					removed.emplace_back(m_subscriptionInfo.at(strKey));
					m_subscriptionClients.erase(mmIter);
					m_subscriptionInfo.erase(strKey);
					m_pendingSubscriptions.erase(strKey);
				}
			}
			m_clientSubscriptions.erase(mIter);
		}
	}
	if (nullptr != m_pSession)
	{
		for (const market::CQuoteInfo& quote : removed)
		{
			m_pSession->UnsubscribeQuote(quote);
		}
	}
	return 1;
}

bool CBrokerService::HandleReAuth(const CRequest& req)
{
	std::string strToken = req.GetExtraData("token");
	bool bAccepted = false;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		bAccepted = m_loginTokens.end() != m_loginTokens.find(strToken);
		if (bAccepted)
		{
			m_authenticatedClients.emplace(req.GetConnectionId());
		}
	}
	if (bAccepted)
	{
		SendResponse(req, 0, "认证成功");
		return true;
	}
	SendResponse(req, InvalidCredentials, "登录状态已失效");
	return false;
}

bool CBrokerService::HandleAuth(net::_TyConnectionId id, const CRequest& req)
{
	// 断线重连
	std::string strToken = req.GetExtraData("token");
	if (!strToken.empty())
	{
		return HandleReAuth(req);
	}

	// 登录认证
	if ("auth" == req.GetCmd())
	{
		if (Login(req, strToken))
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_authenticatedClients.emplace(req.GetConnectionId());
			m_loginTokens.emplace(strToken);
		}
		return true;
	}
	net::SendError(req.GetConnectionId(), req, InvalidRequest, "unsupported authentication request");
	return false;
}

int CBrokerService::OnNetEvent(const net::CNetEvent& ev)
{
	if ((net::em_event::disconnected == ev.m_event) || (net::em_event::error == ev.m_event) || (net::em_event::timeout == ev.m_event))
	{
		return HandleDisconnected(ev.m_connection_id);
	}

	if (nullptr == ev.m_request)
	{
		return 0;
	}

	std::string strCmd = ev.m_request->GetCmd();
	cosnt auto mIter = m_handler.find(strCmd);
	if (m_handler.end() == mIter)
	{
		net::SendError(ev.m_connection_id, *ev.m_request, InvalidRequest, "unknown command");
		return 0;
	}
	return mIter->second(ev.m_connection_id, *ev.m_request) ? 0 : 1;
}
