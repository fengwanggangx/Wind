#ifndef WIND_HQMARKET_CHQREQUEST_H
#define WIND_HQMARKET_CHQREQUEST_H

#include "../request/request.h"
#include <cstdint>
#include <string>

// HQMarket业务请求组装器。
// 所有函数均在堆上创建CRequest，返回值应直接交给CHQMarket::SendRequest()接管；
// 如果请求未交给CHQMarket，调用方负责delete释放。
class CHQRequest final
{
	public:
		// 组装实时行情订阅请求。
		// strInstrument为“证券代码.交易所”格式，例如“600519.SSE”；
		// strChannel支持“quote”或“depth”；nRequestId为0时由CHQMarket自动生成请求号。
		static CRequest* Subscribe(const std::string& strInstrument, const std::string& strChannel, std::uint64_t nRequestId = 0);

		// 组装实时行情退订请求。
		// 参数含义与Subscribe相同，仅取消指定标的和通道的订阅。
		static CRequest* Unsubscribe(const std::string& strInstrument, const std::string& strChannel, std::uint64_t nRequestId = 0);

		// 组装最新行情快照查询请求。
		// strInstrument为“证券代码.交易所”格式；nRequestId可用于关联异步响应。
		static CRequest* QueryQuote(const std::string& strInstrument, std::uint64_t nRequestId = 0);

		// 组装K线查询请求。
		// strChannel支持“bar_1m”或“bar_1d”；时间参数为Unix毫秒时间戳，
		// 查询范围包含nBeginTime和nEndTime之间的数据。
		static CRequest* QueryBars(const std::string& strInstrument, const std::string& strChannel, std::int64_t nBeginTime, std::int64_t nEndTime, std::uint64_t nRequestId = 0);

		// 组装心跳请求。
		// nClientTime为客户端当前Unix毫秒时间戳，用于检测连接和估算链路延迟。
		static CRequest* Heartbeat(std::int64_t nClientTime, std::uint64_t nRequestId = 0);

	private:
		// 创建公共请求对象并填写HQMARKET类型、命令和可选请求号。
		static CRequest* Create(const std::string& strCommand, std::uint64_t nRequestId);
};

#endif
