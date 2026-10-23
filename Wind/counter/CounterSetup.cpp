#include "CounterSetup.h"
#include "CInProcessGateway.h"
#include "CStorager.h"
#include "../system/CSession.h"
#include "../common/utility.h"
#include "../request/request.pb.h"
#include "../request/RequestCenter.h"
#include <rapidjson/document.h>
#include <fstream>
#include <iterator>
#include <chrono>
#include <set>
namespace trading
{
	namespace
	{
		std::int64_t Now()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}
		bool Integer(const rapidjson::Value& value, const char* pName, std::int64_t& nValue)
		{
			if (!value.HasMember(pName) || !value[pName].IsInt64())
			{
				return false;
			}
			nValue = value[pName].GetInt64();
			return true;
		}
		bool Text(const rapidjson::Value& value, const char* pName, std::string& strValue)
		{
			if (!value.HasMember(pName) || !value[pName].IsString())
			{
				return false;
			}
			strValue.assign(value[pName].GetString(), value[pName].GetStringLength());
			return true;
		}
		CResult ReadRules(const std::string& strPath, std::vector<CRule>& rules, std::vector<CCalendar>& calendar)
		{
			if (strPath.empty())
			{
				return {};
			} // Safe default: account queries work, all orders lack rules and are rejected.
			std::ifstream stream(strPath);
			if (!stream)
			{
				return { Error::Rejected, "cannot open Trade.rules_path" };
			}
			std::string strJson((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			rapidjson::Document document;
			document.Parse(strJson.data(), strJson.size());
			if (document.HasParseError() || !document.IsObject() || !document.HasMember("rules") || !document["rules"].IsArray() || !document.HasMember("calendar") || !document["calendar"].IsArray())
			{
				return { Error::Rejected, "invalid counter rules JSON" };
			}
			for (const auto& value : document["calendar"].GetArray())
			{
				CCalendar day;
				std::int64_t nDay = 0;
				if (!value.IsObject() || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "open_ms", day.m_nOpen) || !Integer(value, "break_ms", day.m_nBreak) || !Integer(value, "resume_ms", day.m_nResume) || !Integer(value, "close_ms", day.m_nClose))
				{
					return { Error::Rejected, "calendar requires explicit day and epoch-millisecond sessions" };
				}
				day.m_nDay = static_cast<int>(nDay);
				calendar.emplace_back(day);
			}
			for (const auto& value : document["rules"].GetArray())
			{
				CRule rule;
				std::int64_t nDay = 0, nScale = 0;
				if (!value.IsObject() || !Text(value, "version", rule.m_strVersion) || !Text(value, "security", rule.m_strSecurity) || !Text(value, "board", rule.m_strBoard) || !Integer(value, "day", nDay) || (0 >= nDay) || (99991231 < nDay) || !Integer(value, "price_scale", nScale) || (0 > nScale) || (6 < nScale) || !Integer(value, "tick", rule.m_nTick) || !Integer(value, "buy_minimum", rule.m_nBuyMinimum) || !Integer(value, "buy_step", rule.m_nBuyStep) || !Integer(value, "sell_minimum", rule.m_nSellMinimum) || !Integer(value, "sell_step", rule.m_nSellStep) || !Integer(value, "lower", rule.m_nLower) || !Integer(value, "upper", rule.m_nUpper) || !Integer(value, "commission_ppm", rule.m_nCommissionPpm) || !Integer(value, "minimum_fee_cents", rule.m_nMinimumFee) || !Integer(value, "sell_tax_ppm", rule.m_nSellTaxPpm) || !Integer(value, "transfer_ppm", rule.m_nTransferPpm) || !value.HasMember("corporate_action") || !value["corporate_action"].IsBool())
				{
					return { Error::Rejected, "security rule fields must be explicit" };
				}
				rule.m_nDay = static_cast<int>(nDay);
				rule.m_nPriceScale = static_cast<int>(nScale);
				rule.m_bCorporateAction = value["corporate_action"].GetBool();
				rules.emplace_back(std::move(rule));
			}
			return {};
		}
	} // namespace
	CResult InitializeCounter(const std::vector<CAccountRoute>& routes, const std::string& strRulesPath, CSession& session, std::shared_ptr<CInProcessGateway>& counter)
	{
		std::vector<CRule> rules;
		std::vector<CCalendar> calendar;
		CResult result = ReadRules(strRulesPath, rules, calendar);
		if (!result.Ok())
		{
			return result;
		}
		std::unique_ptr<ICounterStore> store = std::make_unique<CStorager>(utility::MakeSaltHex());
		std::shared_ptr<CInProcessGateway> gateway = std::make_shared<CInProcessGateway>(std::make_unique<CSimulatedCounter>(std::move(store)), Now);
		std::vector<CAccount> accounts;
		for (const auto& route : routes)
		{
			if ("simulated" == route.m_strAdapter)
			{
				accounts.push_back({ route.m_accountId, route.m_nInitialBalance, 0, false, true });
			}
		}
		result = gateway->Start(accounts, rules, calendar);
		if (!result.Ok())
		{
			return result;
		}
		std::weak_ptr<CInProcessGateway> weak = gateway;
		session.RegisterHandler([weak](const ::CRequest& request)
		{
			std::shared_ptr<CInProcessGateway> target = weak.lock();
			if ((nullptr == target) || !request.GetData().has_depth())
			{
				return;
			}
			const hqmarket::market::v1::DepthData& wire = request.GetData().depth();
			if (wire.stale() || (0 == wire.bids_size() + wire.asks_size()))
			{
				return;
			}
			CDepth depth;
			depth.m_strSecurity = wire.security().symbol() + "." + GetMarketString(static_cast<Exchange>(wire.security().exchange()));
			std::string strSequence = request.GetExtraData("sequence");
			if (!utility::to_number(strSequence, depth.m_sequence) || (0 == depth.m_sequence))
			{
				return;
			}
			depth.m_nTime = wire.exchange_time_ms();
			depth.m_nPriceScale = 0 < wire.bids_size() ? wire.bids(0).price_scale() : wire.asks(0).price_scale();
			for (const auto& level : wire.bids())
			{
				if (level.price_scale() != depth.m_nPriceScale)
				{
					return;
				}
				depth.m_bids.push_back({ level.price(), level.volume() });
			}
			for (const auto& level : wire.asks())
			{
				if (level.price_scale() != depth.m_nPriceScale)
				{
					return;
				}
				depth.m_asks.push_back({ level.price(), level.volume() });
			}
			target->OnDepth(std::move(depth));
		});
		std::vector<CSecurity> depthSecurities;
		std::set<std::string> seen;
		for (const auto& rule : rules)
		{
			if (!seen.emplace(rule.m_strSecurity).second)
			{
				continue;
			}
			std::size_t nDot = rule.m_strSecurity.rfind('.');
			depthSecurities.emplace_back(rule.m_strSecurity.substr(0, nDot), ParseMarket(rule.m_strSecurity.substr(nDot + 1)));
		}
		auto subscribe = [weak, &session, depthSecurities](SessionState state, const std::string&)
		{
			std::shared_ptr<CInProcessGateway> target = weak.lock();
			if (nullptr == target)
			{
				return;
			}
			target->OnMarketState(SessionState::Ready == state);
			if (SessionState::Ready == state)
			{
				for (const auto& security : depthSecurities)
				{
					CQuoteInfo quote(security.m_strCode, security.m_market, Channel::depth);
					session.SendRequest(request::Subscription(quote));
				}
			}
		};
		session.RegisterStateHandler(subscribe);
		subscribe(session.IsAuthenticated() ? SessionState::Ready : SessionState::Disconnected, "");
		gateway->OnMarketState(session.IsAuthenticated());
		counter = std::move(gateway);
		return {};
	}
} // namespace trading
