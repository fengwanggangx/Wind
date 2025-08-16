#ifndef __CREQUESTCENTER_H__
#define __CREQUESTCENTER_H__

class CRequest;


int Query(const CRequest& req);
int Update(const CRequest& req);
int Auth(const CRequest& req);

#endif
