#include "debug_service.h"
#include "web_http.h"
#include "peer_update.h"
#include "ota_service.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace {
portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
debugmode::Session session;debugmode::Log logbook;bool was_enabled=false;
}
bool debug_enabled(){portENTER_CRITICAL(&guard);const bool value=session.active(millis());portEXIT_CRITICAL(&guard);return value;}
void debug_enable_local(bool enabled){
  const uint32_t now=millis();portENTER_CRITICAL(&guard);
  if(enabled){session.enable(now);logbook.clear(now);logbook.push(debugmode::Channel::Event,"Enabled on instrument touchscreen. Passive capture only.",now);}
  else {session.disable();logbook.clear(now);}was_enabled=enabled;portEXIT_CRITICAL(&guard);
}
void debug_service(){
  const uint32_t now=millis();portENTER_CRITICAL(&guard);
  if(was_enabled&&!session.active(now)){session.disable();logbook.clear(now);was_enabled=false;}
  portEXIT_CRITICAL(&guard);
}
bool debug_activity(){portENTER_CRITICAL(&guard);const bool ok=session.touch(millis());portEXIT_CRITICAL(&guard);return ok;}
bool debug_disable(){debug_enable_local(false);return true;}
void debug_observe(debugmode::Channel channel,const char *text){
  const uint32_t now=millis();portENTER_CRITICAL(&guard);
  if(session.active(now))logbook.push(channel,text,now);
  portEXIT_CRITICAL(&guard);
}
void debug_frame(debugmode::Channel channel,const uint8_t *frame,size_t size){
  if(!debug_enabled()||!frame||!size)return;
  char text[96];
  if(size>=8&&frame[0]==0xd3)std::snprintf(text,sizeof(text),"RTCM type %u, %u bytes",unsigned((frame[3]<<4)|(frame[4]>>4)),unsigned(size));
  else if(size>=4)std::snprintf(text,sizeof(text),"Frame %02X %02X %02X %02X, %u bytes",frame[0],frame[1],frame[2],frame[3],unsigned(size));
  else std::snprintf(text,sizeof(text),"%u bytes",unsigned(size));
  debug_observe(channel,text);
}
bool debug_status(char *out,size_t capacity){
  const uint32_t now=millis();uint32_t remaining;char peer[80];peer_update_label(peer,sizeof(peer));
  portENTER_CRITICAL(&guard);remaining=session.remaining(now);portEXIT_CRITICAL(&guard);
  const int n=std::snprintf(out,capacity,"{\"version\":1,\"firmware\":\"%s\",\"boot_id\":%lu,\"uptime_ms\":%lu,\"enabled\":%s,\"remaining_ms\":%lu,\"idle_ms\":900000,\"passive\":true,\"ota_available\":true,\"peer_update_notice_available\":true,\"peer_status\":\"%s\",\"update_paused\":%s,\"update_locked\":%s}",kWebUiVersion,static_cast<unsigned long>(web_boot_id()),static_cast<unsigned long>(now),remaining?"true":"false",static_cast<unsigned long>(remaining),peer,ota_paused()?"true":"false",ota_locked()?"true":"false");
  return n>=0&&size_t(n)<capacity;
}
bool debug_logs(char *out,size_t capacity){
  // HTTP task owns this copy; JSON allocation and serialization never hold the
  // observer lock. A second reader cannot block the UART on heap/network work.
  auto *copy=new(std::nothrow) debugmode::Log;if(!copy)return false;
  portENTER_CRITICAL(&guard);const bool enabled=session.active(millis());if(enabled)*copy=logbook;portEXIT_CRITICAL(&guard);
  if(!enabled){delete copy;return false;}
  DynamicJsonDocument d(12288);d["version"]=1;d["boot_id"]=web_boot_id();d["uptime_ms"]=millis();
  d["throttled"]=copy->throttled;d["overwritten"]=copy->overwritten;d["truncated"]=copy->truncated;
  auto rows=d.createNestedArray("entries");for(unsigned i=0;i<copy->size();++i){const auto &entry=copy->at(i);auto row=rows.createNestedObject();row["sequence"]=entry.sequence;row["at_ms"]=entry.at;row["channel"]=debugmode::channel_name(entry.channel);row["text"]=entry.text;}
  // entry strings point into the copy until serialization completes.
  const bool fits=!d.overflowed()&&measureJson(d)<capacity;if(fits)serializeJson(d,out,capacity);delete copy;return fits;
}
