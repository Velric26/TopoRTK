#include "link_diagnostic.h"
#include "link_diagnostic_core.h"
#include "survey_service.h"
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <freertos/queue.h>

namespace {
linktest::Engine engine;HardwareSerial radio(2);WiFiUDP udp;
bool radio_started=false,udp_started=false,locked=false,persisted=false,persisted_peer=false;
uint32_t probe_start=0;uint8_t probe_phase=0;char probe_text[256]={};size_t probe_size=0;
void start_radio(){if(!radio_started){radio.setRxBufferSize(4096);radio.setTxBufferSize(1024);radio.begin(57600,SERIAL_8N1,18,17);radio_started=true;}}
uint8_t transport=0; // 0 Wi-Fi; 1 SiK
uint8_t rx[sizeof(linktest::Packet)]={};size_t rx_size=0;
QueueHandle_t queue=nullptr;portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
char cached[4096]="{\"state\":\"idle\"}",last_report[3072]="null";
bool cached_busy=false;uint32_t published=0;
struct Request{bool cancel=false,probe=false;linktest::Config config;uint8_t transport=0;};
const char *state_name(){switch(engine.state){case linktest::Armed:return "armed";case linktest::Running:return "running";case linktest::Done:return "done";case linktest::Failed:return "failed";default:return "idle";}}
void result_json(JsonObject d){
  d["run"]=engine.config.run;d["state"]=state_name();d["reason"]=engine.reason;
  d["role"]=engine.node?"ROVER":"BASE";d["transport"]=transport?"sik":"wifi";
  d["seconds"]=engine.config.seconds;d["rate"]=engine.config.rate;d["mode"]=engine.config.mode;
  d["expected_tx"]=engine.config.run?linktest::planned(engine.config,engine.node):0;d["sent"]=engine.sent;
  d["expected_rx"]=engine.config.run?linktest::planned(engine.config,1-engine.node):0;d["received"]=engine.received;
  d["errors"]=engine.errors;d["duplicates"]=engine.duplicates;d["reordered"]=engine.reordered;d["max_gap_ms"]=engine.max_gap_ms;
  d["local_pass"]=engine.local_pass();d["peer_report_received"]=engine.peer_result;d["pair_pass"]=engine.pair_pass();
  if(engine.peer_result){auto peer=d.createNestedObject("peer");peer["sent"]=engine.peer.sent;peer["received"]=engine.peer.received;peer["errors"]=engine.peer.errors;peer["duplicates"]=engine.peer.duplicates;peer["reordered"]=engine.peer.reordered;peer["max_gap_ms"]=engine.peer.max_gap_ms;}
  d["radio_baud"]=57600;d["qualification"]="synthetic transport test; not survey accuracy";
}
void save_result(){
  StaticJsonDocument<3072> d;result_json(d.to<JsonObject>());serializeJson(d,last_report,sizeof(last_report));
  Preferences p;if(p.begin("linkdiag",false)){
    persisted=p.putString("report",last_report)>0&&p.getString("report")==last_report;
    if(persisted)p.putBool("active",false);p.end();
  }
  persisted_peer=engine.peer_result;
}
void publish(uint32_t now){
  DynamicJsonDocument d(4096);result_json(d.to<JsonObject>());d["busy"]=engine.busy();d["persisted"]=persisted;
  d["radio_probe"]=probe_phase?"checking":probe_size?(std::strstr(probe_text,"SiK ")?"UART responds as SiK":"No SiK identity response; check power, baud and crossed TX/RX"):"not checked";
  d["radio_probe_response"]=probe_text;
  if(probe_phase){d["state"]="probing";d["busy"]=true;}
  d["uptime_ms"]=now;d["remaining_seconds"]=engine.state==linktest::Running&&int32_t(now-engine.start_at)>=0?std::max(0,int(engine.config.seconds)-(int(now-engine.start_at)/1000)):0;
  DynamicJsonDocument previous(3072);if(!deserializeJson(previous,static_cast<const char*>(last_report)))d["last_report"]=previous.as<JsonVariant>();
  static char out[4096];size_t n=serializeJson(d,out,sizeof(out));
  if(n<sizeof(out)-1){portENTER_CRITICAL(&guard);std::memcpy(cached,out,n+1);cached_busy=engine.busy()||probe_phase;portEXIT_CRITICAL(&guard);}published=now;
}
}
void diagnostic_begin(){
  queue=xQueueCreate(1,sizeof(Request));Preferences p;
  if(p.begin("linkdiag",false)){
    if(p.getBool("active",false)){std::snprintf(last_report,sizeof(last_report),"{\"state\":\"interrupted\",\"run\":%lu,\"pair_pass\":false,\"reason\":\"instrument_restarted_during_test\"}",static_cast<unsigned long>(p.getUInt("run",0)));p.putString("report",last_report);p.putBool("active",false);}
    else {String saved=p.getString("report","null");if(saved.length()<sizeof(last_report))std::strcpy(last_report,saved.c_str());}p.end();
  }
  publish(millis());
}
bool diagnostic_busy(){portENTER_CRITICAL(&guard);bool value=cached_busy;portEXIT_CRITICAL(&guard);return value;}
bool diagnostic_snapshot(char *out,size_t capacity){portENTER_CRITICAL(&guard);size_t n=std::strlen(cached);bool ok=n<capacity;if(ok)std::memcpy(out,cached,n+1);portEXIT_CRITICAL(&guard);return ok;}
bool diagnostic_request(const char *json){
  StaticJsonDocument<512>d;if(!queue||deserializeJson(d,json))return false;Request r;
  const char *op=d["op"]|"";if(!std::strcmp(op,"probe")){r.probe=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}r.cancel=std::strcmp(op,"cancel")==0;
  if(!r.cancel&&std::strcmp(op,"arm"))return false;
  if(!d["run"].is<uint32_t>())return false;r.config.run=d["run"];
  if(!r.cancel){if(!d["seconds"].is<uint16_t>()||!d["rate"].is<uint16_t>()||!d["mode"].is<uint8_t>())return false;
    r.config.seconds=d["seconds"];r.config.rate=d["rate"];r.config.mode=d["mode"];
    const char *t=d["transport"]|"";if(std::strcmp(t,"wifi")&&std::strcmp(t,"sik"))return false;r.transport=std::strcmp(t,"sik")==0;
    if(!linktest::valid_config(r.config)||d["confirm"]!=true)return false;
  }
  return xQueueSend(queue,&r,0)==pdTRUE;
}
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy){
  Request request;
  if(queue&&xQueueReceive(queue,&request,0)==pdTRUE){
    if(request.probe){if(!engine.busy()&&!probe_phase&&!profile_busy&&survey_diagnostic_acquire()){locked=true;start_radio();while(radio.available())radio.read();probe_size=0;probe_text[0]=0;probe_start=now;probe_phase=1;}}
    else if(request.cancel){if(engine.busy()&&request.config.run==engine.config.run)engine.abort(now,"cancelled");}
    else if(!engine.busy()&&!probe_phase&&request.config.run!=engine.config.run){
      if(profile_busy||!survey_diagnostic_acquire()){engine.reason="finish_survey_or_receiver_operation_first";}
      else {
        locked=true;Preferences p;bool saved=false;
        if(p.begin("linkdiag",false)){saved=p.putUInt("run",request.config.run)==4&&p.putBool("active",true)==1;p.end();}
        if(!saved){survey_diagnostic_release();locked=false;engine.reason="cannot_save_test_start";}
        else {engine.arm(request.config,rover?1:0,now);transport=request.transport;persisted=persisted_peer=false;rx_size=0;
          if(transport){start_radio();while(radio.available())radio.read();}
          if(!transport&&!udp_started)udp_started=udp.begin(22346);
          if(!transport&&!udp_started)engine.abort(now,"wifi_diagnostic_socket_unavailable");
        }
      }
    }
  }
  if(engine.busy()&&engine.node!=(rover?1:0))engine.abort(now,"role_changed");
  if(engine.state==linktest::Idle)engine.node=rover?1:0;
  if(probe_phase){
    for(unsigned budget=0;budget<512&&radio.available();++budget){int c=radio.read();if(probe_size<sizeof(probe_text)-1&&c>=32&&c<127){probe_text[probe_size++]=char(c);probe_text[probe_size]=0;}}
    uint32_t elapsed=now-probe_start;
    if(probe_phase==1&&elapsed>=1200){radio.print("+++");probe_phase=2;}
    if(probe_phase==2&&elapsed>=2700){radio.print("\rATI\r");probe_phase=3;}
    if(probe_phase==3&&elapsed>=4200){radio.print("ATO\r");probe_phase=4;}
    if(probe_phase==4&&elapsed>=4800){probe_phase=0;if(!probe_size){std::strcpy(probe_text,"no response");probe_size=11;}survey_diagnostic_release();locked=false;}
  }
  if(engine.config.run&&!probe_phase){
    if(transport&&radio_started){
      for(unsigned budget=0;budget<2048&&radio.available();++budget){
        rx[rx_size++]=uint8_t(radio.read());
        if(rx_size>=4){uint32_t marker;std::memcpy(&marker,rx,4);if(marker!=linktest::magic){std::memmove(rx,rx+1,--rx_size);if(engine.busy())++engine.errors;}}
        if(rx_size==sizeof(rx)){linktest::Packet p;std::memcpy(&p,rx,sizeof(p));if(linktest::valid(p)){engine.receive(p,now);rx_size=0;}else{engine.receive(p,now);std::memmove(rx,rx+1,--rx_size);}}
      }
    }else if(!transport&&udp_started){
      for(unsigned budget=0;budget<8;++budget){int size=udp.parsePacket();if(!size)break;
        if(size==sizeof(linktest::Packet)&&uint32_t(peer)&&udp.remoteIP()==peer){linktest::Packet p;if(udp.read(reinterpret_cast<uint8_t*>(&p),sizeof(p))==sizeof(p))engine.receive(p,now);}
        else while(udp.available())udp.read();
      }
    }
    linktest::Packet packet;
    if(engine.next(packet,now)){
      bool sent=false;
      if(transport){if(radio.availableForWrite()>=sizeof(packet))sent=radio.write(reinterpret_cast<uint8_t*>(&packet),sizeof(packet))==sizeof(packet);}
      else if(uint32_t(peer)&&udp.beginPacket(peer,22346)){sent=udp.write(reinterpret_cast<uint8_t*>(&packet),sizeof(packet))==sizeof(packet)&&udp.endPacket()==1;}
      if(sent)engine.transmitted(packet,now);
    }
  }
  if(!engine.busy()&&!probe_phase&&locked){save_result();survey_diagnostic_release();locked=false;}
  if(engine.state==linktest::Done&&engine.peer_result&&!persisted_peer)save_result();
  portENTER_CRITICAL(&guard);cached_busy=engine.busy()||probe_phase;portEXIT_CRITICAL(&guard);
  if(now-published>=200)publish(now);
}
