#include "CBrokerService.h"

#include "../common/utility.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../network/CNetTools.h"
#include "../network/CTcpServer.h"
#include "../network/common_net.h"
#include "../request/request.h"
#include "../request/RequestCenter.h"
#include "../request/request.pb.h"
#include "../request/v1/market.pb.h"
#include "../strategy/CStrategyEngine.h"
#include "CSession.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <mutex>
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
	constexpr int UpstreamUnavailable = 1007;
	constexpr int UpstreamTimeout = 1008;
	constexpr int TooManyPendingQueries = 1009;
	constexpr std::size_t MaxPendingQueries = 256;
	constexpr std::chrono::hours TokenLifetime{ 24 };
	constexpr std::chrono::seconds UpstreamRequestTimeout{ 10 };
} // namespace

bool IsAccountValid(const std::string& strAccount)
{
	if ((MinAccountLength > strAccount.size()) || (MaxAccountLength < strAccount.size()))
	{
		return false;
	}
	for (unsigned char c : strAccount)
	{
		if ((0 == std::isalnum(c)) && ('_' != c) && ('-' != c) && ('.' != c) && ('@' != c))
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

CBrokerService::CBrokerService(net::CTcpServer* pTcpServer, CSession* pSession, CStrategyEngine* pStrategyEngine) : m_pTcpServer(pTcpServer), m_pSession(pSession), m_pStrategyEngine(pStrategyEngine)
{
	m_handler = {
		{ "register", std::bind_front(&CBrokerService::HandleRegisterAuth, this) },
		{ "auth", std::bind_front(&CBrokerService::HandleAuth, this) },
		{ "heartbeat", std::bind_front(&CBrokerService::HandleHeartbeat, this) },
		{ "subscribe", std::bind_front(&CBrokerService::HandleSubscription, this) },
		{ "unsubscribe", std::bind_front(&CBrokerService::HandleSubscription, this) },
		{ "query_quote", std::bind_front(&CBrokerService::HandleMarketQuery, this) },
		{ "query_bars", std::bind_front(&CBrokerService::HandleMarketQuery, this) },
		{ "query_securities", std::bind_front(&CBrokerService::HandleMarketQuery, this) },
		{ "query_sectors", std::bind_front(&CBrokerService::HandleMarketQuery, this) },
		{ "query_sector_constituents", std::bind_front(&CBrokerService::HandleMarketQuery, this) },
		{ "strategy_add", std::bind_front(&CBrokerService::HandleStrategy, this) },
		{ "strategy_modify", std::bind_front(&CBrokerService::HandleStrategy, this) },
		{ "strategy_query", std::bind_front(&CBrokerService::HandleStrategy, this) },
		{ "strategy_delete", std::bind_front(&CBrokerService::HandleStrategy, this) }
	};
}

bool CBrokerService::Initialize()
{
	if ((nullptr == m_pTcpServer) || (nullptr == m_pSession) || (nullptr == m_pStrategyEngine))
	{
		return false;
	}
	m_pTcpServer->RegisterHandler(std::bind_front(&CBrokerService::OnNetEvent, this));
	m_pSession->RegisterHandler(std::bind_front(&CBrokerService::OnHQMarketResponse, this));
	m_pSession->RegisterStateHandler(std::bind_front(&CBrokerService::OnHQMarketState, this));
	return true;
}

CQuoteInfo CBrokerService::GetQuoteInfo(const CRequest& req) const
{
	std::string strSecurity = req.GetExtraData("security");
	std::size_t nDot = strSecurity.rfind('.');
	if ((std::string::npos == nDot) || (0 == nDot) || (strSecurity.size() - 1 == nDot))
	{
		return {};
	}
	return CQuoteInfo(strSecurity.substr(0, nDot), ParseMarket(strSecurity.substr(nDot + 1)), ParseChannel(req.GetExtraData("channel")));
}

void CBrokerService::SendSubscriptionResponse(net::_TyConnectionId id, _TyRequestId requestId, bool bAccepted, const std::string& strReason) const
{
	CRequest response;
	response.SetId(requestId);
	response.SetType(CRequest::Type::HQMARKET);
	response.SetCmd("subscription_ack");
	response.SetReturnData("accepted", bAccepted ? "1" : "0");
	response.SetReturnData("reason", strReason);
	net::SendRequest(id, response);
}

std::string CBrokerService::GetMarketResponseKey(const CRequest& req) const
{
	const _TyReqData& message = req.GetData();
	hqmarket::market::v1::Security security;
	hqmarket::market::v1::Channel channel = hqmarket::market::v1::CHANNEL_UNSPECIFIED;
	if (message.has_quote())
	{
		security = message.quote().security();
		channel = hqmarket::market::v1::CHANNEL_QUOTE;
	}
	else if (message.has_depth())
	{
		security = message.depth().security();
		channel = hqmarket::market::v1::CHANNEL_DEPTH;
	}
	if (hqmarket::market::v1::CHANNEL_UNSPECIFIED != channel)
	{
		Exchange exchange = static_cast<Exchange>(static_cast<int>(security.exchange()));
		Channel marketChannel = static_cast<Channel>(static_cast<int>(channel));
		return CQuoteInfo(security.symbol(), exchange, marketChannel).String();
	}
	if (message.has_subscription_ack() && (0 != message.subscription_ack().results_size()))
	{
		const hqmarket::market::v1::SubscriptionResult& result = message.subscription_ack().results(0);
		Exchange exchange = static_cast<Exchange>(static_cast<int>(result.security().exchange()));
		Channel channel = static_cast<Channel>(static_cast<int>(result.channel()));
		return CQuoteInfo(result.security().symbol(), exchange, channel).String();
	}
	return {};
}

void CBrokerService::OnHQMarketResponse(const CRequest& req)
{
	ExpirePendingSubscriptions();
	ExpirePendingQueries();
	if (RouteQueryRequest(req))
	{
		return;
	}
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
			auto mIter = m_pending_subscriptions.find(strKey);
			if (m_pending_subscriptions.end() == mIter)
			{
				return;
			}
			pending = std::move(mIter->second);
			m_pending_subscriptions.erase(mIter);
		}
		if (!bAccepted)
		{
			m_subscriptions.RemoveSubscription(strKey);
		}
		for (const auto& v : pending)
		{
			SendSubscriptionResponse(v.m_router_id, v.m_requestId, bAccepted, strReason);
		}
		return;
	}

	CRequest push = req;
	push.SetId(0);
	std::vector<net::_TyConnectionId> ids = m_subscriptions.GetSubscriberIds(strKey);
	std::vector<net::_TyConnectionId> disconnected;
	for (net::_TyConnectionId id : ids)
	{
		if (!net::SendRequest(id, push))
		{
			disconnected.emplace_back(id);
		}
	}
	for (net::_TyConnectionId id : disconnected)
	{
		HandleDisconnected(id);
	}
}

void CBrokerService::OnHQMarketState(SessionState state, const std::string& strMessage)
{
	if (SessionState::Ready == state)
	{
		for (const CQuoteInfo& quote : m_subscriptions.GetSubscriptions())
		{
			m_pSession->SendRequest(request::Subscription(quote));
		}
		return;
	}
	if ((SessionState::Disconnected == state) || (SessionState::Stopping == state))
	{
		FailPendingSubscriptions(strMessage.empty() ? "HQMarket connection lost" : strMessage);
		FailPendingQueries(strMessage.empty() ? "HQMarket connection lost" : strMessage);
	}
}

std::string CBrokerService::GetQueryKey(const CRequest& req) const
{
	std::string strCmd = req.GetCmd();
	if ("query_quote" == strCmd)
	{
		return strCmd + ":" + req.GetExtraData("security");
	}
	if ("query_securities" == strCmd)
	{
		return strCmd;
	}
	if ("query_sectors" == strCmd)
	{
		std::string strSectorType = req.GetExtraData("sector_type");
		return strSectorType.empty() ? std::string() : strCmd + ":" + strSectorType;
	}
	if ("query_sector_constituents" == strCmd)
	{
		std::string strSectorType = req.GetExtraData("sector_type");
		std::string strSectorCode = req.GetExtraData("sector_code");
		return strSectorType.empty() || strSectorCode.empty() ? std::string() : strCmd + ":" + strSectorType + ":" + strSectorCode;
	}
	if ("query_bars" == strCmd)
	{
		return strCmd + ":" + req.GetExtraData("security") + ":" + req.GetExtraData("channel") + ":" + req.GetExtraData("begin_time_ms") + ":" + req.GetExtraData("end_time_ms");
	}
	return {};
}

bool CBrokerService::HandleMarketQuery(net::_TyConnectionId id, const CRequest& req)
{
	std::string strKey = GetQueryKey(req);
	if (strKey.empty())
	{
		net::SendError(id, req, InvalidRequest, "invalid market query");
		return false;
	}

	_TyRequestId routerId = 0;
	bool bTooManyPending = false;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		auto mIter = m_pending_queries.find(strKey);
		if (m_pending_queries.end() != mIter)
		{
			mIter->second.m_clients.emplace_back(CPendingQueryClient{ id, req.GetId() });
			return true;
		}
		if (MaxPendingQueries <= m_pending_queries.size())
		{
			bTooManyPending = true;
		}
		else
		{
			routerId = m_router_id.fetch_add(1);
			CPendingQuery pending;
			pending.m_router_id = routerId;
			pending.m_strKey = strKey;
			pending.m_strCmd = req.GetCmd();
			pending.m_deadline = std::chrono::steady_clock::now() + UpstreamRequestTimeout;
			pending.m_clients.emplace_back(CPendingQueryClient{ id, req.GetId() });
			m_pending_queries.emplace(strKey, std::move(pending));
		}
	}
	if (bTooManyPending)
	{
		net::SendError(id, req, TooManyPendingQueries, "too many pending market queries");
		return false;
	}

	CRequest router = req;
	router.SetId(routerId);
	if ((nullptr != m_pSession) && m_pSession->SendRequest(router))
	{
		return true;
	}
	CPendingQuery failed;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		auto mIter = m_pending_queries.find(strKey);
		if ((m_pending_queries.end() != mIter) && (routerId == mIter->second.m_router_id))
		{
			failed = std::move(mIter->second);
			m_pending_queries.erase(mIter);
		}
	}
	for (const auto& client : failed.m_clients)
	{
		SendQueryError(client, failed.m_strCmd, UpstreamUnavailable, "HQMarket is unavailable");
	}
	return false;
}

bool CBrokerService::RouteQueryRequest(const CRequest& req)
{
	CPendingQuery pending;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		_TyRequestId routerId = req.GetId();
		auto mIter = std::find_if(m_pending_queries.begin(), m_pending_queries.end(), [routerId](const auto& item)
								  { return routerId == item.second.m_router_id; });
		if (m_pending_queries.end() == mIter)
		{
			return false;
		}
		pending = std::move(mIter->second);
		m_pending_queries.erase(mIter);
	}
	for (const auto& client : pending.m_clients)
	{
		CRequest s = req;
		s.SetId(client.m_requestId);
		net::SendRequest(client.m_router_id, s);
	}
	return true;
}

void CBrokerService::SendQueryError(const CPendingQueryClient& client, const std::string& strCmd, int nErrorCode, const std::string& strMessage) const
{
	CRequest response;
	response.SetId(client.m_requestId);
	response.SetType(CRequest::Type::HQMARKET);
	response.SetCmd(strCmd);
	net::SetError(response, nErrorCode, strMessage);
	net::SendRequest(client.m_router_id, response);
}

void CBrokerService::ExpirePendingQueries()
{
	std::vector<CPendingQuery> expired;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		for (auto mIter = m_pending_queries.begin(); m_pending_queries.end() != mIter;)
		{
			if (mIter->second.m_deadline <= now)
			{
				expired.emplace_back(std::move(mIter->second));
				mIter = m_pending_queries.erase(mIter);
			}
			else
			{
				++mIter;
			}
		}
	}
	for (const auto& pending : expired)
	{
		for (const auto& client : pending.m_clients)
		{
			SendQueryError(client, pending.m_strCmd, UpstreamTimeout, "HQMarket request timeout");
		}
	}
}

void CBrokerService::FailPendingQueries(const std::string& strReason)
{
	std::vector<CPendingQuery> failed;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		failed.reserve(m_pending_queries.size());
		for (auto& item : m_pending_queries)
		{
			failed.emplace_back(std::move(item.second));
		}
		m_pending_queries.clear();
	}
	for (const auto& pending : failed)
	{
		for (const auto& client : pending.m_clients)
		{
			SendQueryError(client, pending.m_strCmd, UpstreamUnavailable, strReason);
		}
	}
}

void CBrokerService::ExpirePendingSubscriptions()
{
	std::vector<PendingSubscription> expired;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		for (auto mIter = m_pending_subscriptions.begin(); m_pending_subscriptions.end() != mIter;)
		{
			auto& values = mIter->second;
			for (auto vIter = values.begin(); values.end() != vIter;)
			{
				if (vIter->m_deadline <= now)
				{
					expired.emplace_back(*vIter);
					vIter = values.erase(vIter);
				}
				else
				{
					++vIter;
				}
			}
			mIter = values.empty() ? m_pending_subscriptions.erase(mIter) : std::next(mIter);
		}
	}
	for (const auto& pending : expired)
	{
		m_subscriptions.Unsubscribe(pending.m_router_id, { pending.m_quote });
		if (nullptr != m_pSession)
		{
			m_pSession->SendRequest(request::UnSubscription(pending.m_quote));
		}
		SendSubscriptionResponse(pending.m_router_id, pending.m_requestId, false, "HQMarket request timeout");
	}
}

void CBrokerService::FailPendingSubscriptions(const std::string& strReason)
{
	std::vector<PendingSubscription> failed;
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		for (const auto& [k, v] : m_pending_subscriptions)
		{
			failed.insert(failed.end(), v.begin(), v.end());
		}
		m_pending_subscriptions.clear();
	}
	for (const auto& pending : failed)
	{
		m_subscriptions.Unsubscribe(pending.m_router_id, { pending.m_quote });
		if (nullptr != m_pSession)
		{
			m_pSession->SendRequest(request::UnSubscription(pending.m_quote));
		}
		SendSubscriptionResponse(pending.m_router_id, pending.m_requestId, false, strReason);
	}
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

	std::string strSql = "SELECT user_id, account FROM table_user WHERE account=" + utility::Utf8Literal(strAccount) + " AND password_hash=UNHEX(SHA2(CONCAT(password_salt,UNHEX('" + utility::ToHex(strPassword) + "')),256)) AND status=1 LIMIT 1";
	const db::CQueryTable& table = db->ExecQuery(strSql);
	if (table.m_rows.empty())
	{
		SendResponse(req, InvalidCredentials, "账号或密码错误");
		return false;
	}

	CRequest response;
	response.SetId(req.GetId());
	response.SetType(req.GetType());
	response.SetCmd(req.GetCmd());
	response.SetReturnData("status", "ok");
	response.SetReturnData("user_id", db::QueryValueToString(table.m_rows.front().at(0)));
	response.SetReturnData("account", db::QueryValueToString(table.m_rows.front().at(1)));
	strToken = utility::MakeSaltHex();
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

	std::string strAccountLiteral = utility::Utf8Literal(strAccount);
	const db::CQueryTable& table = db->ExecQuery("SELECT user_id FROM table_user WHERE account=" + strAccountLiteral + " LIMIT 1");
	if (!table.m_rows.empty())
	{
		SendResponse(req, AccountExists, "账号已存在");
		return false;
	}

	std::string saltHex = utility::MakeSaltHex();
	std::string strSQL = "INSERT INTO table_user(account,password_hash,password_salt,status) VALUES(" + strAccountLiteral + ",UNHEX(SHA2(CONCAT(UNHEX('" + saltHex + "'),UNHEX('" + utility::ToHex(strPassword) + "')),256)),UNHEX('" + saltHex + "'),1)";
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
		if (m_auth_clients.end() == m_auth_clients.find(id))
		{
			net::SendError(id, req, AuthenticationRequired, "authentication required");
			return false;
		}
	}

	bool bSubscribe = request::IsSubscriptionRequest(req);
	bool bUnsubscribe = request::IsSubscriptionRequest(req);

	if (!bSubscribe && !bUnsubscribe)
	{
		net::SendError(id, req, InvalidSubscription, "unsupported request");
		return false;
	}
	CQuoteInfo quote = GetQuoteInfo(req);
	if (!quote.IsValid())
	{
		net::SendError(id, req, InvalidSubscription, "invalid security or unsupported subscription channel");
		return false;
	}
	std::string strKey = quote.String();

	if (bSubscribe)
	{
		if (m_subscriptions.IsSubscribed(id, quote))
		{
			SendSubscriptionResponse(id, req.GetId(), true, "already subscribed");
			return true;
		}
		std::vector<CQuoteInfo> subscriptions{ quote };
		bool bSendUpstream = !m_subscriptions.Subscribe(id, subscriptions).empty();
		bool bPending = false;
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			bPending = m_pending_subscriptions.end() != m_pending_subscriptions.find(strKey);
			if (bSendUpstream || bPending)
			{
				m_pending_subscriptions[strKey].push_back(PendingSubscription{ id, req.GetId(), quote, std::chrono::steady_clock::now() + UpstreamRequestTimeout });
			}
		}
		if (bSendUpstream)
		{
			if ((nullptr == m_pSession) || !m_pSession->SendRequest(request::Subscription(quote)))
			{
				m_subscriptions.Unsubscribe(id, subscriptions);
				{
					std::lock_guard<std::mutex> lock(m_mtx_state);
					m_pending_subscriptions.erase(strKey);
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

	if (!m_subscriptions.IsSubscribed(id, quote))
	{
		SendSubscriptionResponse(id, req.GetId(), true, "not subscribed");
		return true;
	}
	std::vector<CQuoteInfo> subscriptions{ quote };
	bool bSendUpstream = !m_subscriptions.Unsubscribe(id, subscriptions).empty();
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		if (bSendUpstream)
		{
			m_pending_subscriptions.erase(strKey);
		}
	}
	if (bSendUpstream && ((nullptr == m_pSession) || !m_pSession->SendRequest(request::UnSubscription(quote))))
	{
		SendSubscriptionResponse(id, req.GetId(), false, "HQMarket is unavailable");
		return false;
	}
	SendSubscriptionResponse(id, req.GetId(), true, "ok");
	return true;
}

int CBrokerService::HandleDisconnected(net::_TyConnectionId id)
{
	std::vector<CQuoteInfo> removed = m_subscriptions.RemoveClient(id);
	{
		std::lock_guard<std::mutex> lock(m_mtx_state);
		m_auth_clients.erase(id);
		for (const auto& quote : removed)
		{
			std::string strKey = quote.String();
			m_pending_subscriptions.erase(strKey);
		}
		for (auto mIter = m_pending_queries.begin(); m_pending_queries.end() != mIter;)
		{
			std::vector<CPendingQueryClient>& clients = mIter->second.m_clients;
			clients.erase(std::remove_if(clients.begin(), clients.end(), [id](const CPendingQueryClient& client)
										 { return id == client.m_router_id; }),
						  clients.end());
			if (clients.empty())
			{
				mIter = m_pending_queries.erase(mIter);
			}
			else
			{
				++mIter;
			}
		}
	}
	if (nullptr != m_pSession)
	{
		for (const auto& quote : removed)
		{
			m_pSession->SendRequest(request::UnSubscription(quote));
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
		auto mIter = m_client_tokens.find(strToken);
		bAccepted = (m_client_tokens.end() != mIter) && (std::chrono::steady_clock::now() < mIter->second);
		if ((m_client_tokens.end() != mIter) && !bAccepted)
		{
			m_client_tokens.erase(mIter);
		}
		if (bAccepted)
		{
			m_auth_clients.emplace(req.GetConnectionId());
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

bool CBrokerService::IsAuthenticated(net::_TyConnectionId id) const
{
	std::lock_guard<std::mutex> lck(m_mtx_state);
	return (m_auth_clients.end() != m_auth_clients.find(id));
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
	if (request::IsAuthRequest(req))
	{
		if (Login(req, strToken))
		{
			std::lock_guard<std::mutex> lock(m_mtx_state);
			m_auth_clients.emplace(req.GetConnectionId());
			m_client_tokens.insert_or_assign(strToken, std::chrono::steady_clock::now() + TokenLifetime);
		}
		return true;
	}
	net::SendError(req.GetConnectionId(), req, InvalidRequest, "unsupported authentication request");
	return false;
}

void CBrokerService::OnClientRequest(net::_TyConnectionId id, const CRequest& req)
{
	ExpirePendingQueries();
	std::string strCmd = req.GetCmd();
	const auto mIter = m_handler.find(strCmd);
	if (m_handler.end() == mIter)
	{
		net::SendError(id, req, 1006, "unknown command");
		return;
	}

	if (request::IsAuthRequest(req) || request::IsRegisterAccountRequest(req))
	{
		mIter->second(id, req);
		return;
	}

	if (!IsAuthenticated(id))
	{
		net::SendError(id, req, 1002, "authentication required");
		return;
	}

	mIter->second(id, req);
}

bool CBrokerService::HandleHeartbeat(net::_TyConnectionId id, const CRequest& req)
{
	CRequest response;
	response.SetId(req.GetId());
	response.SetType(CRequest::Type::HEARTBEAT);
	response.SetCmd("heartbeat");
	response.SetReturnData("client_time_ms", req.GetExtraData("client_time_ms"));
	response.SetReturnData("status", "ok");
	return net::SendRequest(id, response);
}

bool CBrokerService::HandleStrategy(net::_TyConnectionId id, const CRequest& req)
{
	if ((CRequest::Type::STRATEGY != req.GetType()) || (nullptr == m_pStrategyEngine))
	{
		net::SendError(id, req, InvalidRequest, "invalid strategy request");
		return false;
	}
	return m_pStrategyEngine->HandleStrategyRequest(req);
}

int CBrokerService::OnNetEvent(const net::CNetEvent& ev)
{
	if (net::em_event::request == ev.m_event)
	{
		OnClientRequest(ev.m_request->GetConnectionId(), *ev.m_request);
	}
	else if ((net::em_event::disconnected == ev.m_event) || (net::em_event::error == ev.m_event) || (net::em_event::timeout == ev.m_event))
	{
		HandleDisconnected(ev.m_connection_id);
	}
	return 1;
}
