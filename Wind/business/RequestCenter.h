#ifndef __CREQUESTCENTER_H__
#define __CREQUESTCENTER_H__
#include <memory>

class CRequest;


int Query(const std::unique_ptr<CRequest>& req);
int Update(const std::unique_ptr<CRequest>& req);
int Auth(const std::unique_ptr<CRequest>& req);

#endif
