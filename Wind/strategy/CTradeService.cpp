#include "CTradeService.h"
#include "../common/container.h"
#include "../common/utility.h"
#include "../database/CDBEngine.h"
#include "../database/IDataBase.h"
#include "../ini/CINIHandler.h"
#include "../network/CNetTools.h"
#include "../request/request.h"
#include "CSimulatedTradingService.h"
#include <charconv>
#include <chrono>
#include <limits>
namespace
{
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
bool CTradeService::Initialize()
{
	std::string mode = ini::CINIHandler::InstanceRef().GetValue(ini::Config::System, "Trade", "mode", std::string("simulated"));
	if ("simulated" != mode)
	{
		m_strLastError = "Only simulated trading is supported; production is not implemented";
		return false;
	}
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
	const db::CQueryTable& table = database->ExecQuery("SELECT account_id,name,account_type,initial_balance,created_at,updated_at,is_default FROM table_trading_account ORDER BY account_id");
	if (table.m_rows.empty() || (7 != table.m_columns.size()))
	{
		m_strLastError = "Default account is missing";
		return false;
	}
	for (const db::_TyQueryRow& row : table.m_rows)
	{
		if (7 != row.size())
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
		if (runtime.m_info.is_default() && (request::ACCOUNT_SIMULATED == runtime.m_info.account_type()))
		{
			runtime.m_service = std::make_shared<CSimulatedTradingService>(runtime.m_info.initial_balance(), id);
			runtime.m_info.set_state(request::ACCOUNT_CONNECTED);
			if (0 != database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(id) + ",'system','account_connect','Wind 启动时连接')"))
			{
				m_strLastError = "Default account connection audit failed";
				return false;
			}
		}
		m_accounts.emplace(id, std::move(runtime));
	}
	return true;
}
void CTradeService::Stop()
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	for (auto& item : m_accounts)
	{
		if (nullptr != item.second.m_service)
		{
			item.second.m_service->Stop();
		}
		item.second.m_info.set_state(request::ACCOUNT_DISCONNECTED);
	}
}
void CTradeService::SetOrderEventHandler(_TyOrderEventHandler&& handler)
{
	std::lock_guard<std::mutex> lock(m_mtx_accounts);
	m_orderHandler = std::move(handler);
	for (auto& item : m_accounts)
	{
		if (nullptr != item.second.m_service)
		{
			item.second.m_service->SetOrderEventHandler(_TyOrderEventHandler(m_orderHandler));
		}
	}
}
std::shared_ptr<CSimulatedTradingService> CTradeService::FindService(std::uint64_t accountId) const
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
	return nullptr != FindService(accountId);
}
bool CTradeService::BeginStrategy(std::uint64_t accountId, _TyStrategyId strategyId)
{
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
	std::shared_ptr<CSimulatedTradingService> service = FindService(accountId);
	return (nullptr != service) && service->HasActiveOrders();
}
COrderSubmitResult CTradeService::Submit(const COrderIntent& intent)
{
	std::shared_ptr<CSimulatedTradingService> service = FindService(intent.m_accountId);
	if (nullptr == service)
	{
		return { false, true, 0, 0, "Trading account is disconnected" };
	}
	COrderSubmitResult result = service->Submit(intent);
	if (result.m_bAccepted)
	{
		std::lock_guard<std::mutex> lock(m_mtx_accounts);
		m_orderAccounts.emplace(result.m_orderId, intent.m_accountId);
	}
	return result;
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
	std::shared_ptr<CSimulatedTradingService> service = FindService(accountId);
	return (nullptr != service) && service->Cancel(strategyId, orderId);
}
CAccountSnapshot CTradeService::GetAccount() const
{
	return GetAccount(1);
}
CAccountSnapshot CTradeService::GetAccount(std::uint64_t accountId) const
{
	std::shared_ptr<CSimulatedTradingService> service = FindService(accountId);
	return nullptr == service ? CAccountSnapshot{} : service->GetAccount();
}
CPositionSnapshot CTradeService::GetPosition(const CSecurity& security) const
{
	return GetPosition(1, security);
}
CPositionSnapshot CTradeService::GetPosition(std::uint64_t accountId, const CSecurity& security) const
{
	std::shared_ptr<CSimulatedTradingService> service = FindService(accountId);
	if (nullptr != service)
	{
		return service->GetPosition(security);
	}
	CPositionSnapshot snapshot;
	snapshot.m_security = security;
	return snapshot;
}
TradeServiceMode CTradeService::GetMode() const
{
	return TradeServiceMode::Simulated;
}
const std::string& CTradeService::GetLastError() const
{
	return m_strLastError;
}
bool CTradeService::HandleAccountRequest(const CRequest& req, const std::string& actor)
{
	std::lock_guard<std::mutex> operationLock(m_mtx_operations);
	std::string command = CRequest::Type::TRADE_ACCOUNT_QUERY == req.GetType() ? "account_query" : req.GetCmd();
	std::uint64_t accountId = 0;
	CRequest response;
	response.SetId(req.GetId());
	response.SetType(req.GetType());
	if (CRequest::Type::TRADE_ACCOUNT_QUERY != req.GetType())
	{
		response.SetCmd(command);
		if (CRequest::Type::TRADE_ACCOUNT_LOGIN != req.GetType())
		{
			response.SetReturnData("status", "ok");
		}
	}
	response.SetErrorInfo(0, "");
	db::_TyDBPtr database = CDBEngine::InstanceRef().GetDBPtr(db::em_database::mysql);
	if (nullptr == database)
	{
		net::SendError(req.GetConnectionId(), req, 1201, "账户数据库不可用");
		return false;
	}
	// Persist audit entries on the same borrowed DB connection to avoid exhausting a small pool.
	auto fail = [&](const std::string& message)
	{
		database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(command) + "," + utility::Utf8Literal(message) + ")");
		net::SendError(req.GetConnectionId(), req, 1202, message);
		return false;
	};
	auto audit = [&]()
	{
		database->ExecUpdate("INSERT INTO table_account_log(account_id,actor,action,message) VALUES(" + std::to_string(accountId) + "," + utility::Utf8Literal(actor) + "," + utility::Utf8Literal(command) + ",'成功')");
	};
	if (CRequest::Type::TRADE_ACCOUNT_LOGIN == req.GetType())
	{
		if ((!(req.GetCmd().empty()) && ("trade_account_login" != req.GetCmd())) || req.GetExtraData("broker").empty() || req.GetExtraData("site").empty() || req.GetExtraData("account").empty() || req.GetExtraData("password").empty())
		{
			return fail("券商、站点、账户和密码不能为空，且登录命令必须匹配请求类型");
		}
		if ((128 < req.GetExtraData("broker").size()) || (256 < req.GetExtraData("site").size()) || (128 < req.GetExtraData("account").size()) || (256 < req.GetExtraData("password").size()))
		{
			return fail("账户登录参数过长");
		}
		return fail("券商登录通道尚未接入，无法验证交易账户");
	}
	if ((CRequest::Type::ACCOUNT != req.GetType()) && (CRequest::Type::TRADE_ACCOUNT_QUERY != req.GetType()))
	{
		return fail("账户请求类型无效");
	}
	if ("account_query" == command)
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
	else if ("account_add" == command)
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
		if ("account_modify" == command)
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
		else if (("account_connect" == command) || ("account_disconnect" == command))
		{
			std::string error;
			{
				std::lock_guard<std::mutex> lock(m_mtx_accounts);
				CAccountRuntime& runtime = m_accounts.at(accountId);
				if ("account_connect" == command)
				{
					if (request::ACCOUNT_SIMULATED != runtime.m_info.account_type())
					{
						error = "实盘通道尚未接入";
						runtime.m_info.set_state(request::ACCOUNT_ERROR);
					}
					else
					{
						if (nullptr == runtime.m_service)
						{
							runtime.m_service = std::make_shared<CSimulatedTradingService>(runtime.m_info.initial_balance(), accountId);
							runtime.m_service->SetOrderEventHandler(_TyOrderEventHandler(m_orderHandler));
						}
						runtime.m_info.set_state(request::ACCOUNT_CONNECTED);
					}
				}
				else if (!runtime.m_runningStrategies.empty() || ((nullptr != runtime.m_service) && runtime.m_service->HasActiveOrders()))
				{
					error = "账户存在已启动策略或活跃委托，请先停止策略并处理委托";
				}
				else
				{
					runtime.m_info.set_state(request::ACCOUNT_DISCONNECTED);
				}
				runtime.m_info.set_last_error(error);
			}
			if (!error.empty())
			{
				return fail(error);
			}
			audit();
		}
		else if ("account_snapshot_query" == command)
		{
			request::TradingAccountSnapshot snapshot;
			snapshot.set_account_id(accountId);
			std::shared_ptr<CSimulatedTradingService> service = FindService(accountId);
			snapshot.set_available_snapshot(nullptr != service);
			if (nullptr != service)
			{
				CAccountSnapshot funds = service->GetAccount();
				snapshot.set_balance(funds.m_balance);
				snapshot.set_available(funds.m_available);
				snapshot.set_frozen(funds.m_frozen);
			}
			snapshot.set_server_time_ms(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			response.SetData(snapshot);
		}
		else if ("account_logs_query" == command)
		{
			request::TradingAccountLogs logs;
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
					request::TradingAccountLog* log = logs.add_logs();
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
	return net::SendRequest(req.GetConnectionId(), response);
}
