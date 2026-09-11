#pragma once
#include "survey_engine.h"
#include <cstring>
namespace survey {
// Caller serializes access; clock and random values are supplied by the platform.
class Control {
  char pin_[7]={},owner_[33]={},token_[33]={};uint32_t lease_=0,attempt_time_=0;unsigned attempts_=0;
 public:
  void reset(const char *pin){std::strncpy(pin_,pin,6);pin_[6]=0;owner_[0]=token_[0]=0;lease_=attempt_time_=0;attempts_=0;}
  const char *pin()const{return pin_;}
  // Rover: every accepted takeover replaces the bearer, even for the same client.
  int takeover(const char *client,const char *candidate,uint32_t now,char *token,size_t capacity){
    if(!valid_id(client)||!valid_id(candidate)||!token||capacity<33)return 400;
    std::strcpy(owner_,client);std::strcpy(token_,candidate);lease_=now;
    std::strcpy(token,token_);return 200;
  }
  int claim(const char *pin,const char *client,const char *candidate,uint32_t now,char *token,size_t capacity){
    if(!pin||std::strlen(pin)!=6||!valid_id(client)||!valid_id(candidate)||capacity<33)return 400;
    if(now-attempt_time_>=60000){attempts_=0;attempt_time_=now;}
    if(attempts_>=5)return 429;
    if(std::strcmp(pin,pin_)!=0){++attempts_;return 403;}
    if(owner_[0]&&now-lease_<120000&&std::strcmp(owner_,client)!=0)return 409;
    if(std::strcmp(owner_,client)!=0||now-lease_>=120000){std::strcpy(owner_,client);std::strcpy(token_,candidate);}
    lease_=now;std::strcpy(token,token_);return 200;
  }
  bool authorized(const char *token,uint32_t now,bool renew){bool ok=token&&token_[0]&&std::strcmp(token,token_)==0&&now-lease_<120000;if(ok&&renew)lease_=now;return ok;}
  void release(const char *token,uint32_t now){if(authorized(token,now,false)){owner_[0]=token_[0]=0;lease_=0;}}
};
}
