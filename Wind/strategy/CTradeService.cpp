#include "CTradeService.h"
#include "../common/container.h"
#include "../common/utility.h"
#include "../counter/CInProcessGateway.h"
#include "../counter/TradeProtocol.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include "../request/RequestCenter.h"
#include "../request/request.h"
#include "../system/CSession.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <limits>
#include <map>
namespace
{
	using trade::protocol::WriteOrder;
	using trade::protocol::WritePosition;
	using trade::protocol::WriteTrade;
	bool ReadId(const std::string& strText, std::uint64_t& nValue)
	{
		std::from_chars_result result = std::from_chars(strText.data(), strText.data() + strText.size(), nValue);
		return !strText.empty() && (std::errc() == result.ec) && (strText.data() + strText.size() == result.ptr) && (0 != nValue);
	}

	bool IsNameValid(const std::string& strName)
	{
		if (strName.empty() || (128 < strName.size()))
		{
			return false;
		}
		bool visible = false;
		for (unsigned char character : strName)
		{
			if ((32 > character) || (127 == character))
			{
				return false;
			}
			visible = visible || (32 != character);
		}
		return visible;
	}
} // namespace

CTradeService::~CTradeService()
{
	Stop();
}

bool CTradeService::Initialize(CSession& session)
{
	db::_TyDBPtr pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == pDB)
	{
		m_strLastError = "Account database is unavailable";
		return false;
	}
	const db::CTable& table = pDB->ExecQuery("SELECT account_id,name,account_type,initial_balance,created_at,updated_at,is_default,adapter_key,broker,site,external_account FROM table_trading_account ORDER BY account_id");
	if (11 != table.m_columns.size())
	{
		m_strLastError = "Failed to load trading accounts";
		return false;
	}
	for (const auto& row : table.m_rows)
	{
		if (11 != row.size())
		{
			return false;
		}
		std::uint64_t id = 0;
		std::uint64_t balance = 0;
		if (!ReadId(db::to_string(row[0]), id) || !ReadId(db::to_string(row[3]), balance) || (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) < balance))
		{
			m_strLastError = "Invalid trading account row";
			return false;
		}

		CTradeAccount& account = container::emplace_back(m_accounts, id);
		account.m_info.m_accountId = id;
		account.m_info.m_strName = db::to_string(row[1]);
		account.m_info.m_type = "0" == db::to_string(row[2]) ? TradeAccountType::Simulated : TradeAccountType::Production;
		account.m_info.m_nInitialBalance = static_cast<std::int64_t>(balance);
		account.m_info.m_strCreatedAt = db::to_string(row[4]);
		account.m_info.m_strUpdatedAt = db::to_string(row[5]);
		account.m_info.m_bDefault = "1" == db::to_string(row[6]);
		account.m_info.m_strNumber = "SIM-" + std::to_string(id);
		account.m_strAdapter = db::to_string(row[7]);
		account.m_strBroker = db::to_string(row[8]);
		account.m_strSite = db::to_string(row[9]);
		account.m_strExternalAccount = db::to_string(row[10]);
		if (account.m_strExternalAccount.empty())
		{
			account.m_strExternalAccount = account.m_info.m_strNumber;
		}
	}
	std::string strRulesPath = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "rules_path", std::string());
	pDB.reset();
	trade::CResult result = trade::InitializeCounter(m_accounts, strRulesPath, session, m_counter);
	if (!result.Ok())
	{
		m_strLastError = result.m_strMessage;
		return false;
	}
	m_counter->SetEventHandler([this](const trade::CEvent& event)
	{
		OnEvent(event);
	});
	std::lock_guard<std::mutex> initializeLock(m_mtx_accounts);
	for (auto& item : m_accounts)
	{
		if ("simulated" != item.second.m_strAdapter)
		{
			continue;
		}
		item.second.m_service = m_counter;
		if (item.second.m_info.m_bDefault)
		{
			trade::CRequest request;
			request.m_operation = trade::Operation::Connect;
			request.m_accountId = item.first;
			m_counter->Request(request, { });
		}
	}
	for (const auto& order : m_counter->Snapshot().m_orders)
	{
		if (trade::IsActive(order.m_status))
		{
			m_orderAccounts[order.m_orderId] = order.m_accountId;
		}
	}
	return true;
}
void CTradeService::Stop()
{
	if (nullptr != m_counter)
	{
		m_counter->Stop();
	}
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	for (auto& item : m_accounts)
	{
		item.second.m_info.m_state = TradeAccountState::Disconnected;
	}
}
void CTradeService::SetOrderEventHandler(_TyOrderEventHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_orderHandler = std::move(handler);
}
void CTradeService::SetTradeEventHandler(_TyTradeEventHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_tradeHandler = std::move(handler);
}
std::shared_ptr<trade::ITradingGateway> CTradeService::FindService(std::uint64_t accountId) const
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	const auto mIter = m_accounts.find(accountId);
	return ((m_accounts.end() == mIter) || (TradeAccountState::Connected != mIter->second.m_info.m_state)) ? nullptr : mIter->second.m_service;
}
bool CTradeService::HasAccount(std::uint64_t accountId) const
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	return container::with(m_accounts, accountId);
}
bool CTradeService::IsConnected(std::uint64_t accountId) const
{
	return GetAccount(accountId).m_bReady;
}
bool CTradeService::BeginStrategy(std::uint64_t accountId, _TyStrategyId strategyId)
{
	if (!GetAccount(accountId).m_bReady)
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	const auto mIter = m_accounts.find(accountId);
	if ((m_accounts.end() == mIter) || (TradeAccountState::Connected != mIter->second.m_info.m_state))
	{
		return false;
	}
	mIter->second.m_runningStrategies.emplace(strategyId);
	return true;
}
void CTradeService::EndStrategy(std::uint64_t accountId, _TyStrategyId strategyId)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	const auto mIter = m_accounts.find(accountId);
	if (m_accounts.end() != mIter)
	{
		mIter->second.m_runningStrategies.erase(strategyId);
	}
}
bool CTradeService::HasActiveOrders(std::uint64_t accountId) const
{
	std::shared_ptr<trade::ITradingGateway> service = FindService(accountId);
	if (nullptr == service)
	{
		return false;
	}
	for (const auto& order : service->Snapshot(accountId).m_orders)
	{
		if (trade::IsActive(order.m_status))
		{
			return true;
		}
	}
	return false;
}
COrderSubmitResult CTradeService::Submit(const COrderIntent& intent)
{
	std::shared_ptr<trade::ITradingGateway> service = FindService(intent.m_accountId);
	if ((nullptr == service) || !GetAccount(intent.m_accountId).m_bReady)
	{
		return { false, true, 0, 0, "Account not synchronized" };
	}
	if (OrderType::Limit != intent.m_orderType)
	{
		return { false, false, 0, 0, "UNSUPPORTED: only limit orders" };
	}
	static std::atomic_uint64_t nextId{ static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) };
	trade::CRequest request;
	request.m_operation = trade::Operation::Submit;
	request.m_accountId = intent.m_accountId;
	request.m_order.m_clientId = 0 == intent.m_clientOrderId ? nextId.fetch_add(1) : intent.m_clientOrderId;
	request.m_requestId = request.m_order.m_clientId;
	request.m_order.m_strategyId = intent.m_strategyId;
	request.m_order.m_signalId = intent.m_signalId;
	request.m_order.m_strSecurity = intent.m_security.String();
	request.m_order.m_side = OrderSide::Buy == intent.m_side ? trade::Side::Buy : trade::Side::Sell;
	request.m_order.m_nPrice = intent.m_price;
	request.m_order.m_nQuantity = intent.m_quantity;
	request.m_order.m_nPriceScale = intent.m_nPriceScale;
	trade::Id nClientId = request.m_order.m_clientId;
	trade::COrder order = request.m_order;
	order.m_accountId = request.m_accountId;
	trade::CResult result = service->Request(request, [this, order](const trade::CReply& reply)
	{
		if ((trade::Error::Rejected == reply.m_result.m_error) || (trade::Error::Unsupported == reply.m_result.m_error) || (trade::Error::Conflict == reply.m_result.m_error) || (trade::Error::NotFound == reply.m_result.m_error))
		{
			trade::CEvent event;
			event.m_kind = trade::EventKind::Order;
			event.m_accountId = order.m_accountId;
			event.m_order = order;
			event.m_order.m_status = trade::Status::Rejected;
			OnEvent(event);
		}
	});
	return { result.Ok(), !result.Ok(), nClientId, 0, result.Ok() ? "Request queued; await order event" : result.m_strMessage };
}
bool CTradeService::Cancel(_TyStrategyId strategyId, _TyOrderId orderId)
{
	std::uint64_t accountId = 0;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		const auto mIter = m_orderAccounts.find(orderId);
		if (m_orderAccounts.end() == mIter)
		{
			return false;
		}
		accountId = mIter->second;
	}
	std::shared_ptr<trade::ITradingGateway> service = FindService(accountId);
	if (nullptr == service)
	{
		return false;
	}
	trade::CSnapshot snapshot = service->Snapshot(accountId);
	if (!container::with(snapshot.m_orders, orderId, [strategyId](const trade::COrder& row, _TyOrderId id)
	{
		return id == row.m_orderId && strategyId == row.m_strategyId;
	}))
	{
		return false;
	}
	trade::CRequest request;
	request.m_operation = trade::Operation::Cancel;
	request.m_accountId = accountId;
	request.m_orderId = orderId;
	return service->Request(request, { }).Ok();
}
CAccountSnapshot CTradeService::GetAccount() const
{
	return GetAccount(1);
}
CAccountSnapshot CTradeService::GetAccount(std::uint64_t accountId) const
{
	std::shared_ptr<trade::ITradingGateway> service = FindService(accountId);
	CAccountSnapshot result;
	if (nullptr == service)
	{
		return result;
	}
	trade::CSnapshot snapshot = service->Snapshot(accountId, false);
	result.m_bReady = snapshot.m_bReady && (5000 >= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - snapshot.m_nTime);
	result.m_version = snapshot.m_version;
	result.m_nTime = snapshot.m_nTime;
	if (snapshot.m_accounts.empty() || !snapshot.m_accounts[0].m_bConnected)
	{
		result.m_bReady = false;
		return result;
	}
	result.m_balance = snapshot.m_accounts[0].m_nBalance;
	result.m_frozen = snapshot.m_accounts[0].m_nFrozen;
	result.m_available = result.m_balance - result.m_frozen;
	return result;
}
CPositionSnapshot CTradeService::GetPosition(const CSecurity& security) const
{
	return GetPosition(1, security);
}
CPositionSnapshot CTradeService::GetPosition(std::uint64_t accountId, const CSecurity& security) const
{
	std::shared_ptr<trade::ITradingGateway> service = FindService(accountId);
	if (nullptr != service)
	{
		trade::CSnapshot state = service->Snapshot(accountId, false);
		CPositionSnapshot snapshot;
		snapshot.m_security = security;
		snapshot.m_bReady = GetAccount(accountId).m_bReady;
		snapshot.m_version = state.m_version;
		snapshot.m_nTime = state.m_nTime;
		for (const auto& position : state.m_positions)
		{
			if (security.String() == position.m_strSecurity)
			{
				snapshot.m_totalQuantity = position.m_nTotal;
				snapshot.m_availableQuantity = position.m_nAvailable;
				snapshot.m_frozenQuantity = position.m_nFrozen;
				snapshot.m_averageCost = 0 == position.m_nTotal ? 0 : position.m_nCost / position.m_nTotal;
			}
		}
		return snapshot;
	}
	CPositionSnapshot snapshot;
	snapshot.m_security = security;
	return snapshot;
}
const std::string& CTradeService::GetLastError() const
{
	return m_strLastError;
}
bool CTradeService::HandleAccountRequest(const CRequest& req, const std::string& actor)
{
	std::lock_guard<std::mutex> operationLock(m_mtx_operations);
	_TyRequest type = req.GetType();
	std::string strAction = req.GetTypeName();
	std::uint64_t accountId = 0;
	CRequest response;
	response.SetId(req.GetId());
	response.SetType(req.GetType());

	response.SetErrorInfo(0, "");
	if (_TyRequest::TRADE_ACCOUNT_INFO_QUERY == req.GetType())
	{
		std::vector<std::uint64_t> ids;
		if (!req.GetExtraData("account_ids", ids))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "account_ids must be a JSON array of positive IDs");
			return false;
		}
		struct CBatch
		{
			std::mutex m_mtx_reply;
			request::TradeAccountInfoList m_rows;
			std::size_t m_nRemaining{ 0 };
		};
		std::shared_ptr<CBatch> batch = std::make_shared<CBatch>();
		std::map<std::shared_ptr<trade::ITradingGateway>, std::vector<std::uint64_t>, std::owner_less<std::shared_ptr<trade::ITradingGateway>>> groups;
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			if (ids.empty())
			{
				for (const auto& item : m_accounts)
				{
					ids.emplace_back(item.first);
				}
			}
			std::sort(ids.begin(), ids.end());
			ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
			for (std::uint64_t id : ids)
			{
				const auto mIter = m_accounts.find(id);
				if (m_accounts.end() != mIter && nullptr != mIter->second.m_service)
				{
					groups[mIter->second.m_service].emplace_back(id);
				}
				else
				{
					request::TradeAccountInfo* row = batch->m_rows.add_accounts();
					row->set_account_id(id);
					row->set_error_code(1202);
					row->set_error_message("account route unavailable");
				}
			}
		}
		batch->m_nRemaining = groups.size();
		net::_TyConnectionId connection = req.GetConnectionId();
		std::uint64_t nRequest = req.GetId();
		auto send = [batch, connection, nRequest]()
		{
			CRequest response;
			response.SetId(nRequest);
			response.SetType(_TyRequest::TRADE_ACCOUNT_INFO_QUERY);
			response.SetData(batch->m_rows);
			net::SendRequest(connection, response);
		};
		if (groups.empty())
		{
			send();
			return true;
		}
		for (const auto& group : groups)
		{
			std::function<void(const trade::CReply&)> completion = [batch, ids = group.second, send](const trade::CReply& reply)
			{
				bool bDone = false;
				{
					std::lock_guard<std::mutex> lock(batch->m_mtx_reply);
					for (std::uint64_t id : ids)
					{
						request::TradeAccountInfo* row = batch->m_rows.add_accounts();
						row->set_account_id(id);
						row->set_server_time_ms(reply.m_snapshot.m_nTime);
						const auto vIter = std::find_if(reply.m_snapshot.m_accounts.begin(), reply.m_snapshot.m_accounts.end(), [id](const trade::CAccount& v)
						{
							return id == v.m_accountId;
						});
						if (!reply.m_result.Ok())
						{
							row->set_error_code(static_cast<int>(reply.m_result.m_error));
							row->set_error_message(reply.m_result.m_strMessage);
						}
						else if (reply.m_snapshot.m_accounts.end() == vIter || !reply.m_snapshot.m_bReady || !vIter->m_bConnected)
						{
							row->set_error_code(1203);
							row->set_error_message("account not synchronized");
						}
						else
						{
							row->set_available_snapshot(true);
							row->set_balance(vIter->m_nBalance);
							row->set_frozen(vIter->m_nFrozen);
							row->set_available(vIter->m_nBalance - vIter->m_nFrozen);
						}
					}
					bDone = 0 == --batch->m_nRemaining;
				}
				if (bDone)
				{
					send();
				}
			};
			trade::CRequest query;
			query.m_operation = trade::Operation::Funds;
			query.m_requestId = req.GetId();
			trade::CResult queued = group.first->Request(query, completion);
			if (!queued.Ok())
			{
				trade::CReply reply;
				reply.m_result = queued;
				completion(reply);
			}
		}
		return true;
	}
	if (_TyRequest::TRADE_ACCOUNT_LOGIN == req.GetType())
	{
		if (req.GetExtraData("broker").empty() || req.GetExtraData("site").empty() || req.GetExtraData("account").empty() || req.GetExtraData("password").empty())
		{
			net::SendError(req.GetConnectionId(), req, 1202, "broker/site/account/password required");
			return false;
		}
		std::shared_ptr<trade::ITradingGateway> service;
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			for (const auto& item : m_accounts)
			{
				if (item.second.m_strBroker == req.GetExtraData("broker") && item.second.m_strSite == req.GetExtraData("site") && item.second.m_strExternalAccount == req.GetExtraData("account"))
				{
					accountId = item.first;
					service = item.second.m_service;
					break;
				}
			}
		}
		if (nullptr == service)
		{
			net::SendError(req.GetConnectionId(), req, static_cast<int>(trade::Error::Unsupported), "account route or adapter unavailable");
			return false;
		}
		trade::CRequest login;
		login.m_operation = trade::Operation::Login;
		login.m_accountId = accountId;
		login.m_requestId = req.GetId();
		login.m_strBroker = req.GetExtraData("broker");
		login.m_strSite = req.GetExtraData("site");
		login.m_strUser = req.GetExtraData("account");
		login.m_strPassword = req.GetExtraData("password");
		net::_TyConnectionId connection = req.GetConnectionId();
		std::uint64_t nRequestId = req.GetId();
		trade::CResult queued = service->Request(std::move(login), [this, connection, nRequestId, accountId](const trade::CReply& reply)
		{
			CRequest response;
			response.SetType(_TyRequest::TRADE_ACCOUNT_LOGIN);
			response.SetId(nRequestId);
			response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
			if (reply.m_result.Ok())
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				response.SetData(trade::protocol::AccountPayload(m_accounts.at(accountId).m_info));
			}
			net::SendRequest(connection, response);
		});
		if (!queued.Ok())
		{
			net::SendError(connection, req, 1203, queued.m_strMessage);
		}
		return queued.Ok();
	}
	db::_TyDBPtr pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == pDB)
	{
		net::SendError(req.GetConnectionId(), req, 1201, "账户数据库不可用");
		return false;
	}
	// Persist audit entries on the same borrowed DB connection to avoid exhausting a small pool.
	auto fail = [&](const std::string& message)
	{
		pDB->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(strAction) + "," + utility::Utf8Literal(message) + ")");
		net::SendError(req.GetConnectionId(), req, 1202, message);
		return false;
	};
	auto audit = [&]()
	{
		pDB->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(strAction) + ",'成功')");
	};
	if ((!req.IsTradeAccount()) && (_TyRequest::TRADE_ACCOUNT_QUERY != req.GetType()))
	{
		return fail("账户请求类型无效");
	}
	if (_TyRequest::TRADE_ACCOUNT_QUERY == type)
	{
		std::unordered_map<std::uint64_t, std::uint32_t> counts;
		const db::CTable& rows = pDB->ExecQuery("SELECT account_id,COUNT(*) FROM table_strategy GROUP BY account_id");
		for (const db::_TyRow& row : rows.m_rows)
		{
			std::uint64_t id = 0;
			std::uint64_t count = 0;
			if ((2 == row.size()) && ReadId(db::to_string(row[0]), id) && ReadId(db::to_string(row[1]), count))
			{
				counts.emplace(id, static_cast<std::uint32_t>(count));
			}
		}
		if (2 != rows.m_columns.size())
		{
			return fail("无法查询账户关联策略");
		}
		request::TradeAccountList list;
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			for (const auto& item : m_accounts)
			{
				request::TradeAccountRecord* info = list.add_accounts();
				*info = trade::protocol::AccountPayload(item.second.m_info);
				info->set_strategy_count(counts[item.first]);
			}
		}
		response.SetData(list);
	}
	else if (_TyRequest::TRADE_ACCOUNT_ADD == type)
	{
		CTradeAccountInfo info;
		info.m_strName = req.GetExtraData("name");
		info.m_type = TradeAccountType::Simulated;
		std::int64_t nBalance = 0;
		if (!utility::to_number(req.GetExtraData("initial_balance"), nBalance) || !IsNameValid(info.m_strName) || (0 >= nBalance))
		{
			return fail("账户名称或初始资金无效，当前仅支持模拟账户");
		}
		info.m_nInitialBalance = nBalance;
		if (0 != pDB->ExecUpdate("INSERT INTO table_trading_account(name,initial_balance) VALUES(" + utility::Utf8Literal(info.m_strName) + "," + std::to_string(info.m_nInitialBalance) + ")"))
		{
			return fail("新增账户失败，请检查账户名称是否重复");
		}
		const db::CTable& ids = pDB->ExecQuery("SELECT LAST_INSERT_ID()");
		if (ids.m_rows.empty() || ids.m_rows.front().empty() || !ReadId(db::to_string(ids.m_rows.front().front()), accountId))
		{
			return fail("无法获取新账户编号");
		}
		CTradeAccount account;
		account.m_info.m_strName = info.m_strName;
		account.m_info.m_type = TradeAccountType::Simulated;
		account.m_info.m_nInitialBalance = info.m_nInitialBalance;
		account.m_info.m_accountId = accountId;
		account.m_info.m_strNumber = "SIM-" + std::to_string(accountId);
		account.m_info.m_state = TradeAccountState::Disconnected;
		account.m_info.m_nStrategyCount = 0;
		account.m_info.m_strLastError.clear();
		account.m_info.m_bDefault = false;
		const db::CTable& times = pDB->ExecQuery("SELECT created_at,updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
		if (!times.m_rows.empty() && (2 == times.m_rows.front().size()))
		{
			account.m_info.m_strCreatedAt = db::to_string(times.m_rows.front()[0]);
			account.m_info.m_strUpdatedAt = db::to_string(times.m_rows.front()[1]);
		}
		pDB.reset();
		trade::CResult provision = m_counter->AddAccount(accountId, info.m_nInitialBalance);
		if (!provision.Ok())
		{
			net::SendError(req.GetConnectionId(), req, 1203, provision.m_strMessage);
			return false;
		}
		pDB = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == pDB)
		{
			net::SendError(req.GetConnectionId(), req, 1201, "account database unavailable");
			return false;
		}
		account.m_service = m_counter;
		account.m_strAdapter = "simulated";
		account.m_strBroker = "wind_simulated";
		account.m_strSite = "default";
		account.m_strExternalAccount = account.m_info.m_strNumber;
		response.SetData(trade::protocol::AccountPayload(account.m_info));
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			m_accounts.emplace(accountId, std::move(account));
		}
		audit();
	}

	else
	{
		if (!ReadId(req.GetExtraData("account_id"), accountId) || !HasAccount(accountId))
		{
			return fail("账户不存在");
		}
		if (_TyRequest::TRADE_ACCOUNT_MODIFY == type)
		{
			if (!IsNameValid(req.GetExtraData("name")))
			{
				return fail("账户名称无效");
			}
			if (0 != pDB->ExecUpdate("UPDATE table_trading_account SET name=" + utility::Utf8Literal(req.GetExtraData("name")) + " WHERE account_id=" + std::to_string(accountId)))
			{
				return fail("修改失败，请检查名称是否重复");
			}
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				m_accounts.at(accountId).m_info.m_strName = req.GetExtraData("name");
			}
			const db::CTable& times = pDB->ExecQuery("SELECT updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
			if (!times.m_rows.empty() && !times.m_rows.front().empty())
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				m_accounts.at(accountId).m_info.m_strUpdatedAt = db::to_string(times.m_rows.front().front());
			}
			audit();
		}
		else if ((_TyRequest::TRADE_ACCOUNT_CONNECT == type) || (_TyRequest::TRADE_ACCOUNT_DISCONNECT == type))
		{
			std::shared_ptr<trade::ITradingGateway> service;
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				CTradeAccount& account = m_accounts.at(accountId);
				service = account.m_service;
				if ((_TyRequest::TRADE_ACCOUNT_DISCONNECT == type) && !account.m_runningStrategies.empty())
				{
					return fail("账户有运行策略，请先停止");
				}
			}
			if (nullptr == service)
			{
				return fail("UNSUPPORTED: trading adapter is not configured");
			}
			trade::CRequest operation;
			operation.m_operation = _TyRequest::TRADE_ACCOUNT_CONNECT == type ? trade::Operation::Connect : trade::Operation::Disconnect;
			operation.m_requestId = req.GetId();
			operation.m_accountId = accountId;
			pDB.reset();
			CRequest original = req;
			trade::CResult queued = service->Request(operation, [this, original, accountId](const trade::CReply& reply)
			{
				if (!reply.m_result.Ok())
				{
					net::SendError(original.GetConnectionId(), original, 1203, reply.m_result.m_strMessage);
					return;
				}
				CRequest response;
				response.SetId(original.GetId());
				response.SetType(original.GetType());
				{
					std::lock_guard<std::mutex> lock(m_mtx_accounts);
					response.SetData(trade::protocol::AccountPayload(m_accounts.at(accountId).m_info));
				}
				net::SendRequest(original.GetConnectionId(), response);
			});
			if (!queued.Ok())
			{
				net::SendError(req.GetConnectionId(), req, 1203, queued.m_strMessage);
			}
			return queued.Ok();
		}
		else if (_TyRequest::TRADE_ACCOUNT_LOGS_QUERY == type)
		{
			request::TradeAccountLogList logs;
			logs.set_account_id(accountId);
			const db::CTable& table = pDB->ExecQuery("SELECT created_at,actor,action,message FROM table_account_log WHERE account_id=" + std::to_string(accountId) + " ORDER BY log_id DESC LIMIT 100");
			if (4 != table.m_columns.size())
			{
				return fail("无法读取账户日志");
			}
			for (const db::_TyRow& row : table.m_rows)
			{
				if (4 == row.size())
				{
					request::TradeAccountLog* log = logs.add_logs();
					log->set_time(db::to_string(row[0]));
					log->set_actor(db::to_string(row[1]));
					log->set_action(db::to_string(row[2]));
					log->set_message(db::to_string(row[3]));
				}
			}
			response.SetData(logs);
		}
		else
		{
			return fail("不支持的账户操作");
		}
	}
	if ((_TyRequest::TRADE_ACCOUNT_MODIFY == type) || (_TyRequest::TRADE_ACCOUNT_CONNECT == type) || (_TyRequest::TRADE_ACCOUNT_DISCONNECT == type))
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		response.SetData(trade::protocol::AccountPayload(m_accounts.at(accountId).m_info));
	}
	return net::SendRequest(req.GetConnectionId(), response);
}
void CTradeService::SetPushHandler(_TyPushHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_pushHandler = std::move(handler);
}
void CTradeService::SetAccountReadyHandler(_TyAccountReadyHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_accountReadyHandler = std::move(handler);
}
void CTradeService::OnEvent(const trade::CEvent& event)
{
	_TyOrderEventHandler orderHandler;
	_TyTradeEventHandler tradeHandler;
	_TyPushHandler pushHandler;
	_TyAccountReadyHandler readyHandler;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		if (0 != event.m_sequence)
		{
			std::uint64_t& nLast = m_lastEventSequence[event.m_accountId];
			if (event.m_sequence <= nLast)
			{
				return;
			}
			nLast = event.m_sequence;
		}
		orderHandler = m_orderHandler;
		tradeHandler = m_tradeHandler;
		pushHandler = m_pushHandler;
		readyHandler = m_accountReadyHandler;
		const auto mIter = m_accounts.find(event.m_accountId);
		if (m_accounts.end() == mIter)
		{
			return;
		}
		if (trade::EventKind::Session == event.m_kind)
		{
			mIter->second.m_info.m_state = event.m_account.m_bConnected ? TradeAccountState::Connected : TradeAccountState::Disconnected;
		}
		if (trade::EventKind::Order == event.m_kind)
		{
			if (0 != event.m_order.m_orderId)
			{
				if (trade::IsActive(event.m_order.m_status))
				{
					m_orderAccounts[event.m_order.m_orderId] = event.m_accountId;
				}
				else
				{
					m_orderAccounts.erase(event.m_order.m_orderId);
				}
			}
		}
	}
	if (trade::EventKind::Order == event.m_kind && nullptr != orderHandler)
	{
		COrderEvent order = trade::protocol::OrderEvent(event);
		orderHandler(order);
	}
	if (trade::EventKind::Trade == event.m_kind && nullptr != tradeHandler)
	{
		CTradeEvent trade = trade::protocol::TradeEvent(event);
		tradeHandler(trade);
	}
	if (nullptr != pushHandler)
	{
		CTradeAccountInfo account;
		if (trade::EventKind::Session == event.m_kind)
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			account = m_accounts.at(event.m_accountId).m_info;
		}
		CRequest push = trade::protocol::Push(event, account);
		pushHandler(push);
	}
	if (trade::EventKind::Session == event.m_kind && event.m_account.m_bConnected && nullptr != readyHandler)
	{
		readyHandler(event.m_accountId);
	}
}
bool CTradeService::HandleTradingRequest(const CRequest& req)
{
	trade::CRequest operation;
	operation.m_requestId = req.GetId();
	if (!ReadId(req.GetExtraData("account_id"), operation.m_accountId))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "account_id required");
		return false;
	}
	std::shared_ptr<trade::ITradingGateway> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		const auto mIter = m_accounts.find(operation.m_accountId);
		if (m_accounts.end() != mIter)
		{
			service = mIter->second.m_service;
		}
	}
	if (nullptr == service)
	{
		net::SendError(req.GetConnectionId(), req, static_cast<int>(trade::Error::Unsupported), "account adapter unavailable");
		return false;
	}
	_TyRequest type = req.GetType();
	if (_TyRequest::TRADE_CAPABILITIES_QUERY == type)
	{
		CRequest response;
		response.SetId(req.GetId());
		response.SetType(type);
		std::vector<std::uint64_t> operations;
		for (trade::Operation capability : service->Capabilities())
		{
			operations.emplace_back(static_cast<std::uint64_t>(capability) + 1);
		}
		response.SetExtraData("operations", operations);
		response.SetExtraData("limit_orders", "1");
		net::SendRequest(req.GetConnectionId(), response);
		return true;
	}
	if (_TyRequest::TRADE_SYNC_QUERY == type)
	{
		operation.m_operation = trade::Operation::Synchronize;
		std::string strAfter = req.GetExtraData("after_sequence");
		if (!strAfter.empty() && !utility::to_number(strAfter, operation.m_afterSequence))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "invalid after_sequence");
			return false;
		}
	}
	else if (_TyRequest::TRADE_ORDER_SUBMIT == type)
	{
		operation.m_operation = trade::Operation::Submit;
		operation.m_order.m_requestId = req.GetId();
		operation.m_order.m_accountId = operation.m_accountId;
		std::string strSide = req.GetExtraData("side");
		std::int64_t nScale = 0;
		if (("buy" != strSide && "sell" != strSide) || "limit" != req.GetExtraData("order_type") || !ReadId(req.GetExtraData("client_order_id"), operation.m_order.m_clientId) || !utility::to_number(req.GetExtraData("price"), operation.m_order.m_nPrice) || !utility::to_number(req.GetExtraData("quantity"), operation.m_order.m_nQuantity) || !utility::to_number(req.GetExtraData("price_scale"), nScale) || (0 > nScale) || (6 < nScale))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "invalid limit order parameters");
			return false;
		}
		operation.m_order.m_side = "buy" == strSide ? trade::Side::Buy : trade::Side::Sell;
		operation.m_order.m_nPriceScale = static_cast<int>(nScale);
		operation.m_order.m_strSecurity = req.GetExtraData("security");
	}
	else if (_TyRequest::TRADE_ORDER_CANCEL == type)
	{
		operation.m_operation = trade::Operation::Cancel;
		if (!ReadId(req.GetExtraData("order_id"), operation.m_orderId))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "order_id required");
			return false;
		}
	}
	else if (_TyRequest::TRADE_POSITION_QUERY == type)
	{
		operation.m_operation = trade::Operation::Positions;
	}
	else if (_TyRequest::TRADE_ORDER_QUERY == type)
	{
		operation.m_operation = trade::Operation::Orders;
	}
	else if (_TyRequest::TRADE_TRADE_QUERY == type)
	{
		operation.m_operation = trade::Operation::Trades;
	}
	else
	{
		return false;
	}
	std::string strSecurity = req.GetExtraData("security");
	net::_TyConnectionId connection = req.GetConnectionId();
	std::uint64_t nRequestId = req.GetId();
	trade::CResult queued = service->Request(operation, [type, connection, nRequestId, strSecurity](const trade::CReply& reply)
	{
		CRequest response;
		response.SetId(nRequestId);
		response.SetType(type);
		response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
		response.SetExtraData("version", std::to_string(reply.m_snapshot.m_version));
		response.SetExtraData("server_time_ms", std::to_string(reply.m_snapshot.m_nTime));
		if (reply.m_result.Ok())
		{
			if (_TyRequest::TRADE_SYNC_QUERY == type)
			{
				request::TradeSyncResult sync;
				sync.set_watermark(reply.m_snapshot.m_version);
				sync.set_ready(reply.m_snapshot.m_bReady);
				sync.set_server_time_ms(reply.m_snapshot.m_nTime);
				for (const auto& account : reply.m_snapshot.m_accounts)
				{
					trade::protocol::WriteFunds(account, reply.m_snapshot.m_bReady, *sync.add_accounts());
				}
				for (const auto& row : reply.m_snapshot.m_orders)
				{
					WriteOrder(row, *sync.add_orders());
				}
				for (const auto& row : reply.m_snapshot.m_trades)
				{
					WriteTrade(row, *sync.add_trades());
				}
				for (const auto& row : reply.m_snapshot.m_positions)
				{
					WritePosition(row, *sync.add_positions());
				}
				for (const auto& event : reply.m_events)
				{
					trade::protocol::WriteEvent(event, *sync.add_events());
				}
				response.SetData(sync);
			}
			else if ((_TyRequest::TRADE_ORDER_QUERY == type) || (_TyRequest::TRADE_ORDER_SUBMIT == type) || (_TyRequest::TRADE_ORDER_CANCEL == type))
			{
				request::TradeOrderList rows;
				for (const auto& row : reply.m_snapshot.m_orders)
				{
					if ((strSecurity.empty() || strSecurity == row.m_strSecurity) && ((_TyRequest::TRADE_ORDER_QUERY == type) || reply.m_orderId == row.m_orderId))
					{
						WriteOrder(row, *rows.add_orders());
					}
				}
				response.SetData(rows);
			}
			else if (_TyRequest::TRADE_TRADE_QUERY == type)
			{
				request::TradeList rows;
				for (const auto& row : reply.m_snapshot.m_trades)
				{
					if (strSecurity.empty() || strSecurity == row.m_strSecurity)
					{
						WriteTrade(row, *rows.add_trades());
					}
				}
				response.SetData(rows);
			}
			else
			{
				request::TradePositionList rows;
				for (const auto& row : reply.m_snapshot.m_positions)
				{
					if (strSecurity.empty() || strSecurity == row.m_strSecurity)
					{
						WritePosition(row, *rows.add_positions());
					}
				}
				response.SetData(rows);
			}
		}
		net::SendRequest(connection, response);
	});
	if (!queued.Ok())
	{
		net::SendError(connection, req, static_cast<int>(queued.m_error), queued.m_strMessage);
	}
	return queued.Ok();
}

std::vector<trade::COrder> CTradeService::GetOrders(std::uint64_t accountId) const
{
	std::shared_ptr<trade::ITradingGateway> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		const auto mIter = m_accounts.find(accountId);
		if (m_accounts.end() != mIter)
		{
			service = mIter->second.m_service;
		}
	}
	return nullptr == service ? std::vector<trade::COrder>{ } : service->Snapshot(accountId).m_orders;
}
