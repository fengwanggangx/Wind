#include "RequestCenter.h"
#include "../request/request.h"
#include <string>

int Query(const CRequest& req)
{
	std::string s = req.GetCmd();
	std::string s1 = req.GetExtraData("retmsg");
	return 0;
}

int Update(const CRequest& req)
{
	std::string s = req.GetCmd();
	std::string s1 = req.GetExtraData("retmsg");
	return 0;
}

int Auth(const CRequest& req)
{
	std::string s = req.GetCmd();
	std::string s1 = req.GetExtraData("retmsg");
	return 0;
}