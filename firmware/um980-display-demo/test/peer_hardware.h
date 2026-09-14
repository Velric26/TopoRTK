#pragma once
#include "peer_update.h"
#include <cassert>
#include <cstdio>
#include <vector>
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
class IPAddress { public: explicit operator uint32_t()const{return 0;}bool operator==(const IPAddress&)const{return true;} };
class WiFiUDP {
 public: bool begin(int){return true;}int parsePacket(){return 0;}int available(){return 0;}
  IPAddress remoteIP(){return {};}
  int read(uint8_t*,size_t){return 0;}bool beginPacket(IPAddress,int){return true;}
  size_t write(const uint8_t*,size_t n){return n;}int endPacket(){return 1;}
};
