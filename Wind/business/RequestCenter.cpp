#include "RequestCenter.h"
#include "../request/request.h"
#include <string>

int Query(const std::unique_ptr<CRequest>& req)
{
	std::string s = req->GetCmd();
	std::string s1 = req->GetExtraData("retmsg");
	return 0;
}

int Update(const std::unique_ptr<CRequest>& req)
{
	std::string s = req->GetCmd();
	std::string s1 = req->GetExtraData("retmsg");
	return 0;
}

int Auth(const std::unique_ptr<CRequest>& req)
{
	std::string s = req->GetCmd();
	std::string s1 = req->GetExtraData("retmsg");
	return 0;
}