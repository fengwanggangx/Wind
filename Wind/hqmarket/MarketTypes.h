#ifndef WIND_HQMARKET_MARKETTYPES_H
#define WIND_HQMARKET_MARKETTYPES_H

#include <string>

namespace market
{
	// 交易所或市场类型。
	enum class Exchange
	{
		unknown = 0, // 未知市场。
		sse,         // 上海证券交易所。
		szse,        // 深圳证券交易所。
		bse,         // 北京证券交易所。
		hkex,        // 香港交易所。
		cffex,       // 中国金融期货交易所。
		shfe,        // 上海期货交易所。
		dce,         // 大连商品交易所。
		czce,        // 郑州商品交易所。
		ine,         // 上海国际能源交易中心。
		gfex,        // 广州期货交易所。
		nasdaq,      // 纳斯达克证券交易所。
		nyse,        // 纽约证券交易所。
		crypto       // 数字货币市场。
	};

	// 行情数据通道或数据周期。
	enum class Channel
	{
		unknown = 0, // 未知数据类型。
		quote,       // 最新行情快照。
		depth,       // 买卖盘口深度。
		trade,       // 逐笔成交。
		bar_1m,      // 一分钟K线。
		bar_1d,      // 日K线。
		market_status // 市场开盘、休市或收盘等状态。
	};

	struct CSecurity
	{
		std::string m_strCode;
		Exchange m_market{ Exchange::unknown };

		CSecurity() = default;
		CSecurity(const std::string& strCode, market::Exchange mk);
		CSecurity(const CSecurity& arg);
		CSecurity& operator=(const CSecurity& arg);
		bool operator==(const CSecurity& arg) const;

		bool IsValid() const;
		std::string String() const;	
	};

	std::string GetMarketString(Exchange exchange);
	std::string GetChannelString(Channel channel);
	std::string FmtSecurityString(const std::string& strCode, Exchange mk);
}

#endif
