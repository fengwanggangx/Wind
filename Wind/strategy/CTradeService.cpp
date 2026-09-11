#include "CTradeService.h"

#include "../ini/CINIHandler.h"
#include "CSimulatedTradingService.h"

#include <utility>

CTradeService::~CTradeService()
{
	Stop();
}

bool CTradeService::Initialize()
{
	if (nullptr != m_service)
	{
		return true;
	}
	m_strLastError.clear();
	ini::CINIHandler& iniHandler = ini::CINIHandler::InstanceRef();
	std::string strMode = iniHandler.GetValue(ini::Config::System, "Trade", "mode", std::string("simulated"));
	if ("simulated" == strMode)
	{
		m_mode = TradeServiceMode::Simulated;
		m_service = std::make_unique<CSimulatedTradingService>();
		return true;
	}
	if ("production" == strMode)
	{
		m_mode = TradeServiceMode::Production;
		m_strLastError = "Production trade service is not implemented";
		return false;
	}
	m_strLastError = "Unknown trade service mode: " + strMode;
	return false;
}

void CTradeService::Stop()
{
	if (nullptr != m_service)
	{
		m_service->Stop();
	}
}

void CTradeService::SetOrderEventHandler(_TyOrderEventHandler&& handler)
{
	if (nullptr != m_service)
	{
		m_service->SetOrderEventHandler(std::move(handler));
	}
}

COrderSubmitResult CTradeService::Submit(const COrderIntent& intent)
{
	if (nullptr == m_service)
	{
		return { false, false, 0, 0, "Trade service is unavailable" };
	}
	return m_service->Submit(intent);
}

bool CTradeService::Cancel(_TyStrategyId strategyId, _TyOrderId orderId)
{
	return (nullptr != m_service) && m_service->Cancel(strategyId, orderId);
}

CPositionSnapshot CTradeService::GetPosition(const market::CSecurity& security) const
{
	if (nullptr == m_service)
	{
		CPositionSnapshot snapshot;
		snapshot.m_security = security;
		return snapshot;
	}
	return m_service->GetPosition(security);
}

CAccountSnapshot CTradeService::GetAccount() const
{
	return nullptr == m_service ? CAccountSnapshot{ } : m_service->GetAccount();
}

TradeServiceMode CTradeService::GetMode() const
{
	return m_mode;
}

const std::string& CTradeService::GetLastError() const
{
	return m_strLastError;
}
