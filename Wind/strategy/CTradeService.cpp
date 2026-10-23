#include "CTradeService.h"
#include "../counter/CInProcessGateway.h"
#include "../counter/TradeProtocol.h"
#include "../common/container.h"
#include "../common/utility.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include "../request/request.h"
#include "../system/CSession.h"
#include "../request/RequestCenter.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <limits>
namespace
{
	using trading::protocol::WriteOrder;
	using trading::protocol::WritePosition;
	using trading::protocol::WriteTrade;
	bool ReadId(const std::string& text, std::uint64_t& value)
	{
		std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
		return !text.empty() && (std::errc() == result.ec) && (text.data() + text.size() == result.ptr) && (0 != value);
	}
	bool ValidName(const std::string& name)
	{
		if (name.empty() || (128 < name.size()))
		{
			return false;
		}
		bool visible = false;
		for (unsigned char value : name)
		{
			if ((32 > value) || (127 == value))
			{
				return false;
			}
			visible = visible || (32 != value);
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
	db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == database)
	{
		m_strLastError = "Account database is unavailable";
		return false;
	}
	// Runtime migration also handles installations with an existing table_strategy.
	for (const std::string& sql : std::vector<std::string>{
			 "CREATE TABLE IF NOT EXISTS table_trading_account(account_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,name VARCHAR(128) NOT NULL,account_type INT NOT NULL DEFAULT 0,initial_balance BIGINT NOT NULL,is_default TINYINT NOT NULL DEFAULT 0,created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,PRIMARY KEY(account_id),UNIQUE KEY uk_account_name(name)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",
			 "CREATE TABLE IF NOT EXISTS table_account_log(log_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,account_id BIGINT UNSIGNED NOT NULL,actor VARCHAR(128) NOT NULL,action VARCHAR(64) NOT NULL,message TEXT NOT NULL,created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(log_id),KEY idx_account_log(account_id,log_id)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
			 "INSERT IGNORE INTO table_trading_account(account_id,name,initial_balance,is_default) VALUES(1,'默认模拟账户',1000000000,1)" })
	{
		if (0 != database->ExecUpdate(sql))
		{
			m_strLastError = "Account schema migration failed";
			return false;
		}
	}
	if (0 != database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) SELECT 1,'system','account_add','默认模拟账户初始化' WHERE NOT EXISTS(SELECT 1 FROM table_account_log WHERE account_id=1 AND actor='system' AND action='account_add')"))
	{
		m_strLastError = "Default account audit initialization failed";
		return false;
	}
	const db::CQueryTable& columns = database->ExecQuery("SHOW COLUMNS FROM table_strategy LIKE 'account_id'");
	if (columns.m_rows.empty() && (0 != database->ExecUpdate("ALTER TABLE table_strategy ADD COLUMN account_id BIGINT UNSIGNED NOT NULL DEFAULT 1")))
	{
		m_strLastError = "Strategy account migration failed";
		return false;
	}
	for (const auto& column : std::vector<std::pair<std::string, std::string>>{ { "adapter_key", "VARCHAR(64) NOT NULL DEFAULT ''" }, { "broker", "VARCHAR(128) NOT NULL DEFAULT 'wind_simulated'" }, { "site", "VARCHAR(128) NOT NULL DEFAULT 'default'" }, { "external_account", "VARCHAR(128) NOT NULL DEFAULT ''" } })
	{
		const db::CQueryTable& existing = database->ExecQuery("SHOW COLUMNS FROM table_trading_account LIKE '" + column.first + "'");
		if (existing.m_rows.empty() && 0 != database->ExecUpdate("ALTER TABLE table_trading_account ADD COLUMN " + column.first + " " + column.second))
		{
			m_strLastError = "Account routing migration failed";
			return false;
		}
	}
	if (0 != database->ExecUpdate("UPDATE table_trading_account SET adapter_key=IF(account_type=0,'simulated','unconfigured') WHERE adapter_key=''"))
	{
		m_strLastError = "Account routing initialization failed";
		return false;
	}
	const db::CQueryTable& table = database->ExecQuery("SELECT account_id,name,account_type,initial_balance,created_at,updated_at,is_default,adapter_key,broker,site,external_account FROM table_trading_account ORDER BY account_id");
	if (table.m_rows.empty() || (11 != table.m_columns.size()))
	{
		m_strLastError = "Default account is missing";
		return false;
	}
	for (const db::_TyQueryRow& row : table.m_rows)
	{
		if (11 != row.size())
		{
			return false;
		}
		CAccountRuntime runtime;
		std::uint64_t id = 0;
		std::uint64_t balance = 0;
		if (!ReadId(db::QueryValueToString(row[0]), id) || !ReadId(db::QueryValueToString(row[3]), balance) || (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) < balance))
		{
			m_strLastError = "Invalid trading account row";
			return false;
		}
		runtime.m_info.set_account_id(id);
		runtime.m_info.set_name(db::QueryValueToString(row[1]));
		runtime.m_info.set_account_type("0" == db::QueryValueToString(row[2]) ? request::ACCOUNT_SIMULATED : request::ACCOUNT_PRODUCTION);
		runtime.m_info.set_initial_balance(static_cast<std::int64_t>(balance));
		runtime.m_info.set_created_at(db::QueryValueToString(row[4]));
		runtime.m_info.set_updated_at(db::QueryValueToString(row[5]));
		runtime.m_info.set_is_default("1" == db::QueryValueToString(row[6]));
		runtime.m_info.set_number("SIM-" + std::to_string(id));
		runtime.m_strAdapter = db::QueryValueToString(row[7]);
		runtime.m_strBroker = db::QueryValueToString(row[8]);
		runtime.m_strSite = db::QueryValueToString(row[9]);
		runtime.m_strExternalAccount = db::QueryValueToString(row[10]);
		if (runtime.m_strExternalAccount.empty())
		{
			runtime.m_strExternalAccount = runtime.m_info.number();
		}
		if (runtime.m_info.is_default() && (request::ACCOUNT_SIMULATED == runtime.m_info.account_type()))
		{

			runtime.m_info.set_state(request::ACCOUNT_CONNECTED);
			if (0 != database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(id) + ",'system','account_connect','Wind 启动时连接')"))
			{
				m_strLastError = "Default account connection audit failed";
				return false;
			}
		}
		m_accounts.emplace(id, std::move(runtime));
	}
	std::vector<trading::CAccountRoute> routes;
	for (const auto& item : m_accounts)
	{
		routes.push_back({ item.first, item.second.m_info.initial_balance(), item.second.m_strAdapter });
	}
	std::string strRulesPath = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "rules_path", std::string());
	database.reset();
	trading::CResult result = trading::InitializeCounter(routes, strRulesPath, session, m_counter);
	if (!result.Ok())
	{
		m_strLastError = result.m_strMessage;
		return false;
	}
	m_counter->SetEventHandler([this](const trading::CEvent& event)
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
		if (item.second.m_info.is_default())
		{
			trading::CRequest request;
			request.m_operation = trading::Operation::Connect;
			request.m_accountId = item.first;
			m_counter->Request(request, {});
		}
	}
	for (const auto& order : m_counter->Snapshot().m_orders)
	{
		if (trading::IsActive(order.m_status))
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
		item.second.m_info.set_state(request::ACCOUNT_DISCONNECTED);
	}
}
void CTradeService::SetOrderEventHandler(_TyOrderEventHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_orderHandler = std::move(handler);
}
void CTradeService::SetTradeEventHandler(_TyTradeEventHandler handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_tradeHandler = std::move(handler);
}
std::shared_ptr<trading::ITradingGateway> CTradeService::FindService(std::uint64_t accountId) const
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	const auto mIter = m_accounts.find(accountId);
	return ((m_accounts.end() == mIter) || (request::ACCOUNT_CONNECTED != mIter->second.m_info.state())) ? nullptr : mIter->second.m_service;
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
	if ((m_accounts.end() == mIter) || (request::ACCOUNT_CONNECTED != mIter->second.m_info.state()))
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
	std::shared_ptr<trading::ITradingGateway> service = FindService(accountId);
	if (nullptr == service)
	{
		return false;
	}
	for (const auto& order : service->Snapshot(accountId).m_orders)
	{
		if (trading::IsActive(order.m_status))
		{
			return true;
		}
	}
	return false;
}
COrderSubmitResult CTradeService::Submit(const COrderIntent& intent)
{
	std::shared_ptr<trading::ITradingGateway> service = FindService(intent.m_accountId);
	if ((nullptr == service) || !GetAccount(intent.m_accountId).m_bReady)
	{
		return { false, true, 0, 0, "Account not synchronized" };
	}
	if (OrderType::Limit != intent.m_orderType)
	{
		return { false, false, 0, 0, "UNSUPPORTED: only limit orders" };
	}
	static std::atomic_uint64_t nextId{ static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) };
	trading::CRequest request;
	request.m_operation = trading::Operation::Submit;
	request.m_accountId = intent.m_accountId;
	request.m_order.m_clientId = 0 == intent.m_clientOrderId ? nextId.fetch_add(1) : intent.m_clientOrderId;
	request.m_requestId = request.m_order.m_clientId;
	request.m_order.m_strategyId = intent.m_strategyId;
	request.m_order.m_signalId = intent.m_signalId;
	request.m_order.m_strSecurity = intent.m_security.String();
	request.m_order.m_side = OrderSide::Buy == intent.m_side ? trading::Side::Buy : trading::Side::Sell;
	request.m_order.m_nPrice = intent.m_price;
	request.m_order.m_nQuantity = intent.m_quantity;
	request.m_order.m_nPriceScale = intent.m_nPriceScale;
	trading::Id nClientId = request.m_order.m_clientId;
	trading::COrder order = request.m_order;
	order.m_accountId = request.m_accountId;
	trading::CResult result = service->Request(request, [this, order](const trading::CReply& reply)
	{
		if ((trading::Error::Rejected == reply.m_result.m_error) || (trading::Error::Unsupported == reply.m_result.m_error) || (trading::Error::Conflict == reply.m_result.m_error) || (trading::Error::NotFound == reply.m_result.m_error))
		{
			trading::CEvent event;
			event.m_kind = trading::EventKind::Order;
			event.m_accountId = order.m_accountId;
			event.m_order = order;
			event.m_order.m_status = trading::Status::Rejected;
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
		auto iter = m_orderAccounts.find(orderId);
		if (m_orderAccounts.end() == iter)
		{
			return false;
		}
		accountId = iter->second;
	}
	std::shared_ptr<trading::ITradingGateway> service = FindService(accountId);
	if (nullptr == service)
	{
		return false;
	}
	trading::CSnapshot snapshot = service->Snapshot(accountId);
	auto order = std::find_if(snapshot.m_orders.begin(), snapshot.m_orders.end(), [orderId, strategyId](const trading::COrder& row)
	{
		return orderId == row.m_orderId && strategyId == row.m_strategyId;
	});
	if (snapshot.m_orders.end() == order)
	{
		return false;
	}
	trading::CRequest request;
	request.m_operation = trading::Operation::Cancel;
	request.m_accountId = accountId;
	request.m_orderId = orderId;
	return service->Request(request, {}).Ok();
}
CAccountSnapshot CTradeService::GetAccount() const
{
	return GetAccount(1);
}
CAccountSnapshot CTradeService::GetAccount(std::uint64_t accountId) const
{
	std::shared_ptr<trading::ITradingGateway> service = FindService(accountId);
	CAccountSnapshot result;
	if (nullptr == service)
	{
		return result;
	}
	trading::CSnapshot snapshot = service->Snapshot(accountId, false);
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
	std::shared_ptr<trading::ITradingGateway> service = FindService(accountId);
	if (nullptr != service)
	{
		trading::CSnapshot state = service->Snapshot(accountId, false);
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
		std::map<std::shared_ptr<trading::ITradingGateway>, std::vector<std::uint64_t>, std::owner_less<std::shared_ptr<trading::ITradingGateway>>> groups;
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
				auto account = m_accounts.find(id);
				if (m_accounts.end() != account && nullptr != account->second.m_service)
				{
					groups[account->second.m_service].emplace_back(id);
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
			std::function<void(const trading::CReply&)> completion = [batch, ids = group.second, send](const trading::CReply& reply)
			{
				bool bDone = false;
				{
					std::lock_guard<std::mutex> lock(batch->m_mtx_reply);
					for (std::uint64_t id : ids)
					{
						request::TradeAccountInfo* row = batch->m_rows.add_accounts();
						row->set_account_id(id);
						row->set_server_time_ms(reply.m_snapshot.m_nTime);
						auto account = std::find_if(reply.m_snapshot.m_accounts.begin(), reply.m_snapshot.m_accounts.end(), [id](const trading::CAccount& value)
						{
							return id == value.m_accountId;
						});
						if (!reply.m_result.Ok())
						{
							row->set_error_code(static_cast<int>(reply.m_result.m_error));
							row->set_error_message(reply.m_result.m_strMessage);
						}
						else if (reply.m_snapshot.m_accounts.end() == account || !reply.m_snapshot.m_bReady || !account->m_bConnected)
						{
							row->set_error_code(1203);
							row->set_error_message("account not synchronized");
						}
						else
						{
							row->set_available_snapshot(true);
							row->set_balance(account->m_nBalance);
							row->set_frozen(account->m_nFrozen);
							row->set_available(account->m_nBalance - account->m_nFrozen);
						}
					}
					bDone = 0 == --batch->m_nRemaining;
				}
				if (bDone)
				{
					send();
				}
			};
			trading::CRequest query;
			query.m_operation = trading::Operation::Funds;
			query.m_requestId = req.GetId();
			trading::CResult queued = group.first->Request(query, completion);
			if (!queued.Ok())
			{
				trading::CReply reply;
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
		std::shared_ptr<trading::ITradingGateway> service;
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
			net::SendError(req.GetConnectionId(), req, static_cast<int>(trading::Error::Unsupported), "account route or adapter unavailable");
			return false;
		}
		trading::CRequest login;
		login.m_operation = trading::Operation::Login;
		login.m_accountId = accountId;
		login.m_requestId = req.GetId();
		login.m_strBroker = req.GetExtraData("broker");
		login.m_strSite = req.GetExtraData("site");
		login.m_strUser = req.GetExtraData("account");
		login.m_strPassword = req.GetExtraData("password");
		net::_TyConnectionId connection = req.GetConnectionId();
		std::uint64_t nRequestId = req.GetId();
		trading::CResult queued = service->Request(std::move(login), [this, connection, nRequestId, accountId](const trading::CReply& reply)
		{
			CRequest response;
			response.SetType(_TyRequest::TRADE_ACCOUNT_LOGIN);
			response.SetId(nRequestId);
			response.SetErrorInfo(static_cast<int>(reply.m_result.m_error), reply.m_result.m_strMessage);
			if (reply.m_result.Ok())
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				response.SetData(m_accounts.at(accountId).m_info);
			}
			net::SendRequest(connection, response);
		});
		if (!queued.Ok())
		{
			net::SendError(connection, req, 1203, queued.m_strMessage);
		}
		return queued.Ok();
	}
	db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == database)
	{
		net::SendError(req.GetConnectionId(), req, 1201, "账户数据库不可用");
		return false;
	}
	// Persist audit entries on the same borrowed DB connection to avoid exhausting a small pool.
	auto fail = [&](const std::string& message)
	{
		database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(strAction) + "," + utility::Utf8Literal(message) + ")");
		net::SendError(req.GetConnectionId(), req, 1202, message);
		return false;
	};
	auto audit = [&]()
	{
		database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(strAction) + ",'成功')");
	};
	if ((!req.IsTradeAccount()) && (_TyRequest::TRADE_ACCOUNT_QUERY != req.GetType()))
	{
		return fail("账户请求类型无效");
	}
	if (_TyRequest::TRADE_ACCOUNT_QUERY == type)
	{
		std::unordered_map<std::uint64_t, std::uint32_t> counts;
		const db::CQueryTable& rows = database->ExecQuery("SELECT account_id,COUNT(*) FROM table_strategy GROUP BY account_id");
		for (const db::_TyQueryRow& row : rows.m_rows)
		{
			std::uint64_t id = 0;
			std::uint64_t count = 0;
			if ((2 == row.size()) && ReadId(db::QueryValueToString(row[0]), id) && ReadId(db::QueryValueToString(row[1]), count))
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
				info->CopyFrom(item.second.m_info);
				info->set_strategy_count(counts[item.first]);
			}
		}
		response.SetData(list);
	}
	else if (_TyRequest::TRADE_ACCOUNT_ADD == type)
	{
		request::TradeAccountRecord info;
		info.set_name(req.GetExtraData("name"));
		info.set_account_type(request::ACCOUNT_SIMULATED);
		std::int64_t nBalance = 0;
		if (!utility::to_number(req.GetExtraData("initial_balance"), nBalance) || !ValidName(info.name()) || (0 >= nBalance))
		{
			return fail("账户名称或初始资金无效，当前仅支持模拟账户");
		}
		info.set_initial_balance(nBalance);
		if (0 != database->ExecUpdate("INSERT INTO table_trading_account(name,initial_balance) VALUES(" + utility::Utf8Literal(info.name()) + "," + std::to_string(info.initial_balance()) + ")"))
		{
			return fail("新增账户失败，请检查账户名称是否重复");
		}
		const db::CQueryTable& ids = database->ExecQuery("SELECT LAST_INSERT_ID()");
		if (ids.m_rows.empty() || ids.m_rows.front().empty() || !ReadId(db::QueryValueToString(ids.m_rows.front().front()), accountId))
		{
			return fail("无法获取新账户编号");
		}
		CAccountRuntime runtime;
		runtime.m_info.set_name(info.name());
		runtime.m_info.set_account_type(request::ACCOUNT_SIMULATED);
		runtime.m_info.set_initial_balance(info.initial_balance());
		runtime.m_info.set_account_id(accountId);
		runtime.m_info.set_number("SIM-" + std::to_string(accountId));
		runtime.m_info.set_state(request::ACCOUNT_DISCONNECTED);
		runtime.m_info.set_strategy_count(0);
		runtime.m_info.clear_last_error();
		runtime.m_info.set_is_default(false);
		const db::CQueryTable& times = database->ExecQuery("SELECT created_at,updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
		if (!times.m_rows.empty() && (2 == times.m_rows.front().size()))
		{
			runtime.m_info.set_created_at(db::QueryValueToString(times.m_rows.front()[0]));
			runtime.m_info.set_updated_at(db::QueryValueToString(times.m_rows.front()[1]));
		}
		database.reset();
		trading::CResult provision = m_counter->AddAccount(accountId, info.initial_balance());
		if (!provision.Ok())
		{
			net::SendError(req.GetConnectionId(), req, 1203, provision.m_strMessage);
			return false;
		}
		database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
		if (nullptr == database)
		{
			net::SendError(req.GetConnectionId(), req, 1201, "account database unavailable");
			return false;
		}
		runtime.m_service = m_counter;
		runtime.m_strAdapter = "simulated";
		runtime.m_strBroker = "wind_simulated";
		runtime.m_strSite = "default";
		runtime.m_strExternalAccount = runtime.m_info.number();
		response.SetData(runtime.m_info);
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			m_accounts.emplace(accountId, std::move(runtime));
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
			if (!ValidName(req.GetExtraData("name")))
			{
				return fail("账户名称无效");
			}
			if (0 != database->ExecUpdate("UPDATE table_trading_account SET name=" + utility::Utf8Literal(req.GetExtraData("name")) + " WHERE account_id=" + std::to_string(accountId)))
			{
				return fail("修改失败，请检查名称是否重复");
			}
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				m_accounts.at(accountId).m_info.set_name(req.GetExtraData("name"));
			}
			const db::CQueryTable& times = database->ExecQuery("SELECT updated_at FROM table_trading_account WHERE account_id=" + std::to_string(accountId));
			if (!times.m_rows.empty() && !times.m_rows.front().empty())
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				m_accounts.at(accountId).m_info.set_updated_at(db::QueryValueToString(times.m_rows.front().front()));
			}
			audit();
		}
		else if ((_TyRequest::TRADE_ACCOUNT_CONNECT == type) || (_TyRequest::TRADE_ACCOUNT_DISCONNECT == type))
		{
			std::shared_ptr<trading::ITradingGateway> service;
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				CAccountRuntime& runtime = m_accounts.at(accountId);
				service = runtime.m_service;
				if ((_TyRequest::TRADE_ACCOUNT_DISCONNECT == type) && !runtime.m_runningStrategies.empty())
				{
					return fail("账户有运行策略，请先停止");
				}
			}
			if (nullptr == service)
			{
				return fail("UNSUPPORTED: trading adapter is not configured");
			}
			trading::CRequest operation;
			operation.m_operation = _TyRequest::TRADE_ACCOUNT_CONNECT == type ? trading::Operation::Connect : trading::Operation::Disconnect;
			operation.m_requestId = req.GetId();
			operation.m_accountId = accountId;
			database.reset();
			CRequest original = req;
			trading::CResult queued = service->Request(operation, [this, original, accountId](const trading::CReply& reply)
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
					response.SetData(m_accounts.at(accountId).m_info);
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
			const db::CQueryTable& table = database->ExecQuery("SELECT created_at,actor,action,message FROM table_account_log WHERE account_id=" + std::to_string(accountId) + " ORDER BY log_id DESC LIMIT 100");
			if (4 != table.m_columns.size())
			{
				return fail("无法读取账户日志");
			}
			for (const db::_TyQueryRow& row : table.m_rows)
			{
				if (4 == row.size())
				{
					request::TradeAccountLog* log = logs.add_logs();
					log->set_time(db::QueryValueToString(row[0]));
					log->set_actor(db::QueryValueToString(row[1]));
					log->set_action(db::QueryValueToString(row[2]));
					log->set_message(db::QueryValueToString(row[3]));
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
		response.SetData(m_accounts.at(accountId).m_info);
	}
	return net::SendRequest(req.GetConnectionId(), response);
}
void CTradeService::SetPushHandler(std::function<void(const CRequest&)> handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_pushHandler = std::move(handler);
}
void CTradeService::SetAccountReadyHandler(std::function<void(std::uint64_t)> handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_accountReadyHandler = std::move(handler);
}
void CTradeService::OnEvent(const trading::CEvent& event)
{
	_TyOrderEventHandler orderHandler;
	_TyTradeEventHandler tradeHandler;
	std::function<void(const CRequest&)> pushHandler;
	std::function<void(std::uint64_t)> readyHandler;
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
		auto account = m_accounts.find(event.m_accountId);
		if (m_accounts.end() == account)
		{
			return;
		}
		if (trading::EventKind::Session == event.m_kind)
		{
			account->second.m_info.set_state(event.m_account.m_bConnected ? request::ACCOUNT_CONNECTED : request::ACCOUNT_DISCONNECTED);
		}
		if (trading::EventKind::Order == event.m_kind)
		{
			if (0 != event.m_order.m_orderId)
			{
				if (trading::IsActive(event.m_order.m_status))
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
	if (trading::EventKind::Order == event.m_kind && nullptr != orderHandler)
	{
		COrderEvent order = trading::protocol::OrderEvent(event);
		orderHandler(order);
	}
	if (trading::EventKind::Trade == event.m_kind && nullptr != tradeHandler)
	{
		CTradeEvent trade = trading::protocol::TradeEvent(event);
		tradeHandler(trade);
	}
	if (nullptr != pushHandler)
	{
		request::TradeAccountRecord account;
		if (trading::EventKind::Session == event.m_kind)
		{
			std::lock_guard<std::mutex> lock(m_mtx_accounts);
			account = m_accounts.at(event.m_accountId).m_info;
		}
		CRequest push = trading::protocol::Push(event, account);
		pushHandler(push);
	}
	if (trading::EventKind::Session == event.m_kind && event.m_account.m_bConnected && nullptr != readyHandler)
	{
		readyHandler(event.m_accountId);
	}
}
bool CTradeService::HandleTradingRequest(const CRequest& req)
{
	trading::CRequest operation;
	operation.m_requestId = req.GetId();
	if (!ReadId(req.GetExtraData("account_id"), operation.m_accountId))
	{
		net::SendError(req.GetConnectionId(), req, 1202, "account_id required");
		return false;
	}
	std::shared_ptr<trading::ITradingGateway> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		auto account = m_accounts.find(operation.m_accountId);
		if (m_accounts.end() != account)
		{
			service = account->second.m_service;
		}
	}
	if (nullptr == service)
	{
		net::SendError(req.GetConnectionId(), req, static_cast<int>(trading::Error::Unsupported), "account adapter unavailable");
		return false;
	}
	_TyRequest type = req.GetType();
	if (_TyRequest::TRADE_CAPABILITIES_QUERY == type)
	{
		CRequest response;
		response.SetId(req.GetId());
		response.SetType(type);
		std::vector<std::uint64_t> operations;
		for (trading::Operation capability : service->Capabilities())
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
		operation.m_operation = trading::Operation::Synchronize;
		std::string strAfter = req.GetExtraData("after_sequence");
		if (!strAfter.empty() && !utility::to_number(strAfter, operation.m_afterSequence))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "invalid after_sequence");
			return false;
		}
	}
	else if (_TyRequest::TRADE_ORDER_SUBMIT == type)
	{
		operation.m_operation = trading::Operation::Submit;
		operation.m_order.m_requestId = req.GetId();
		operation.m_order.m_accountId = operation.m_accountId;
		std::string strSide = req.GetExtraData("side");
		std::int64_t nScale = 0;
		if (("buy" != strSide && "sell" != strSide) || "limit" != req.GetExtraData("order_type") || !ReadId(req.GetExtraData("client_order_id"), operation.m_order.m_clientId) || !utility::to_number(req.GetExtraData("price"), operation.m_order.m_nPrice) || !utility::to_number(req.GetExtraData("quantity"), operation.m_order.m_nQuantity) || !utility::to_number(req.GetExtraData("price_scale"), nScale) || (0 > nScale) || (6 < nScale))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "invalid limit order parameters");
			return false;
		}
		operation.m_order.m_side = "buy" == strSide ? trading::Side::Buy : trading::Side::Sell;
		operation.m_order.m_nPriceScale = static_cast<int>(nScale);
		operation.m_order.m_strSecurity = req.GetExtraData("security");
	}
	else if (_TyRequest::TRADE_ORDER_CANCEL == type)
	{
		operation.m_operation = trading::Operation::Cancel;
		if (!ReadId(req.GetExtraData("order_id"), operation.m_orderId))
		{
			net::SendError(req.GetConnectionId(), req, 1202, "order_id required");
			return false;
		}
	}
	else if (_TyRequest::TRADE_POSITION_QUERY == type)
	{
		operation.m_operation = trading::Operation::Positions;
	}
	else if (_TyRequest::TRADE_ORDER_QUERY == type)
	{
		operation.m_operation = trading::Operation::Orders;
	}
	else if (_TyRequest::TRADE_TRADE_QUERY == type)
	{
		operation.m_operation = trading::Operation::Trades;
	}
	else
	{
		return false;
	}
	std::string strSecurity = req.GetExtraData("security");
	net::_TyConnectionId connection = req.GetConnectionId();
	std::uint64_t nRequestId = req.GetId();
	trading::CResult queued = service->Request(operation, [type, connection, nRequestId, strSecurity](const trading::CReply& reply)
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
					trading::protocol::WriteFunds(account, reply.m_snapshot.m_bReady, *sync.add_accounts());
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
					trading::protocol::WriteEvent(event, *sync.add_events());
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

std::vector<trading::COrder> CTradeService::GetOrders(std::uint64_t accountId) const
{
	std::shared_ptr<trading::ITradingGateway> service;
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		auto account = m_accounts.find(accountId);
		if (m_accounts.end() != account)
		{
			service = account->second.m_service;
		}
	}
	return nullptr == service ? std::vector<trading::COrder>{} : service->Snapshot(accountId).m_orders;
}
