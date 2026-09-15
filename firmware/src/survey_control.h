#pragma once
#include "survey_engine.h"
#include <cstring>
namespace survey {
// Caller serializes access; clock and random values are supplied by the platform.
class Control {
  char owner_[33]={},token_[33]={};uint32_t lease_=0;
 public:
  void reset(){owner_[0]=token_[0]=0;lease_=0;}
  // Both roles: every accepted takeover replaces the bearer, even for the same client.
  int takeover(const char *client,const char *candidate,uint32_t now,char *token,size_t capacity){
    if(!valid_id(client)||!valid_id(candidate)||!token||capacity<33)return 400;
    std::strcpy(owner_,client);std::strcpy(token_,candidate);lease_=now;
    std::strcpy(token,token_);return 200;
  }
  bool authorized(const char *token,uint32_t now,bool renew){bool ok=token&&token_[0]&&std::strcmp(token,token_)==0&&now-lease_<120000;if(ok&&renew)lease_=now;return ok;}
  void release(const char *token,uint32_t now){if(authorized(token,now,false)){owner_[0]=token_[0]=0;lease_=0;}}
};
}
