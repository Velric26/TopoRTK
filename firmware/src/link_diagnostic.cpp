#include "link_diagnostic.h"
#include "radio_transport.h"
#include "wifi_transport.h"
#include "link_service.h"
#include "debug_service.h"
#include "peer_update.h"
#include "ota_service.h"
#include "link_diagnostic_core.h"
#include "correction_transport_selftest.h"
#include "correction_pair_test.h"
#include "correction_bridge.h"
#include <esp_system.h>
#include "survey_service.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/queue.h>

namespace {
linktest::Engine engine;
correctiontest::Engine pair_engine;bool paired=false;
char route_error[96]={};
bool test_busy(){return paired?pair_engine.busy():engine.busy();}
bool peer_received(){return paired?pair_engine.peer_received:engine.peer_result;}
void start_uart_observation(uint32_t now){radio_transport::observe_start(now);}
void stop_uart_observation(){radio_transport::observe_stop();}
void uart_json(JsonObject d){
  const auto w=radio_transport::observation();
  d["observed"]=w.observed;d["fifo_overflow"]=w.fifo;d["buffer_full"]=w.buffer;d["frame_errors"]=w.frame;d["parity_errors"]=w.parity;d["breaks"]=w.brk;
  d["rx_bytes"]=w.rx_bytes;d["tx_bytes"]=w.tx_bytes;d["rx_backlog_peak"]=w.rx_peak;d["max_service_gap_ms"]=w.service_gap;
  d["tx_wait_polls"]=w.tx_wait;d["short_writes"]=w.short_writes;
}
uint8_t transport=0; // 0 Wi-Fi; 1 SiK
bool locked=false,persisted=false,persisted_peer=false;
uint32_t probe_start=0;uint8_t probe_phase=0;char probe_text[256]={};size_t probe_size=0;
QueueHandle_t queue=nullptr;portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
char cached[kDiagnosticCapacity]="{\"state\":\"idle\"}",last_report[4096]="null";
char self_report[1536]="null",self_error[80]={};
bool cached_busy=false;uint32_t published=0;
struct Request{bool route=false;bool cancel=false,probe=false,selftest=false,paired=false;linktest::Config config;uint8_t transport=0,profile=correctiontest::Injected;};
const char *state_name(){switch(engine.state){case linktest::Armed:return "armed";case linktest::Running:return "running";case linktest::Done:return "done";case linktest::Failed:return "failed";default:return "idle";}}
void result_json(JsonObject d){
  if(paired){
    const auto &e=pair_engine;const auto &s=e.stream.receiver.stats;
    d["kind"]="paired_rtcm_faults";d["suite_version"]=2;d["profile"]=e.profile==correctiontest::Clean?"clean":"injected";d["run"]=e.run;d["state"]=e.state==correctiontest::Armed?"armed":e.state==correctiontest::Running?"running":e.state==correctiontest::Done?"done":"failed";
    d["reason"]=e.reason;d["role"]=e.node?"ROVER":"BASE";d["transport"]="sik";d["seconds"]=e.seconds;d["mode"]=0;
    d["expected_tx"]=e.node?0:e.planned();d["sent"]=e.sent;d["expected_rx"]=e.node?e.expected():0;d["received"]=e.accepted;
    d["errors"]=s.wire_errors;d["duplicates"]=s.duplicates;d["integrity_violations"]=e.invalid;d["tx_expired"]=e.tx_expired;
    d["wire_packets_sent"]=e.wire_sent;d["injected_drops"]=e.dropped;d["injected_corruptions"]=e.corrupted;d["injected_duplicates"]=e.duplicated;
    d["assembly_expired"]=s.expired;d["preempted"]=s.preempted;d["rtcm_errors"]=s.rtcm_errors;d["control_errors"]=e.control_errors;
    d["first_missing_sequence"]=e.first_missing();uart_json(d.createNestedObject("uart"));
    d["local_pass"]=e.local_pass();d["pair_pass"]=e.pair_pass();d["peer_report_received"]=e.peer_received;
    if(e.peer_received){auto p=d.createNestedObject("peer");p["sent"]=e.peer_sent;p["received"]=e.peer_accepted;p["integrity_violations"]=e.peer_invalid;p["tx_expired"]=e.peer_tx_expired;}
    d["workspace_bytes"]=sizeof(pair_engine);d["qualification"]="Synthetic framing only; no GNSS output. Delivery pass is separate from UART health; zero reported UART errors does not prove a clean physical link.";
    return;
  }
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
  DynamicJsonDocument d(4096);result_json(d.to<JsonObject>());
  if(d.overflowed()||measureJson(d)>=sizeof(last_report)){persisted=false;return;}
  serializeJson(d,last_report,sizeof(last_report));
  Preferences p;if(p.begin("linkdiag",false)){
    persisted=p.putString("report",last_report)>0&&p.getString("report")==last_report;
    if(persisted)p.putBool("active",false);p.end();
  }
  persisted_peer=peer_received();
}
void run_transport_selftest(){
  static correction::TestWorkspace workspace;
  self_error[0]=0;Preferences p;
  if(!p.begin("linkdiag",false)){std::strcpy(self_error,"Cannot open report storage; self-test not started.");return;}
  const uint32_t run=esp_random()|1u;
  DynamicJsonDocument result(2048);result["kind"]="local_transport_fault_suite";result["suite_version"]=correction::suite_version;
  result["run"]=run;result["state"]="interrupted";result["passed"]=false;
  serializeJson(result,self_report,sizeof(self_report));
  if(!p.putString("selftest",self_report)||p.getString("selftest")!=self_report){p.end();std::strcpy(self_error,"Cannot save restart marker; self-test not started.");return;}
  const uint32_t started=micros();const auto report=correction::run_selftest(workspace);
  result["duration_us"]=micros()-started;result["state"]=report.passed()?"passed":"failed";result["passed"]=report.passed();
  result["checks"]=report.checks;result["failed_mask"]=report.failed_mask;result["workspace_bytes"]=sizeof(workspace);
  result["sender_bytes"]=sizeof(correction::Sender);result["stream_bytes"]=sizeof(correction::Stream);
  result["qualification"]="Local algorithm test only; no radio or GNSS traffic";result["saved"]=true;
  serializeJson(result,self_report,sizeof(self_report));
  if(!p.putString("selftest",self_report)||p.getString("selftest")!=self_report){result["saved"]=false;serializeJson(result,self_report,sizeof(self_report));std::strcpy(self_error,"Self-test finished, but its report was not confirmed saved.");}
  p.end();
}
void publish(uint32_t now){
  DynamicJsonDocument d(10240);result_json(d.to<JsonObject>());d["busy"]=test_busy();d["persisted"]=persisted;
  d["radio_probe"]=probe_phase?"checking":probe_size?(std::strstr(probe_text,"SiK ")?"UART responds as SiK":"No SiK identity response; check power, baud and crossed TX/RX"):"not checked";
  d["radio_probe_response"]=probe_text;
  if(probe_phase){d["state"]="probing";d["busy"]=true;}
  d["uptime_ms"]=now;d["remaining_seconds"]=engine.state==linktest::Running&&int32_t(now-engine.start_at)>=0?std::max(0,int(engine.config.seconds)-(int(now-engine.start_at)/1000)):0;
  if(paired)d["remaining_seconds"]=pair_engine.state==correctiontest::Running&&int32_t(now-pair_engine.start_at)>=0?std::max(0,int(pair_engine.seconds)-int((now-pair_engine.start_at)/1000)):0;
  DynamicJsonDocument previous(4096);if(!deserializeJson(previous,static_cast<const char*>(last_report)))d["last_report"]=previous.as<JsonVariant>();
  DynamicJsonDocument self(2048);if(!deserializeJson(self,static_cast<const char*>(self_report)))d["self_test"]=self.as<JsonVariant>();
  d["self_test_error"]=self_error;auto corrections=d.createNestedObject("corrections");
  link_service::write_json(corrections,now);
  if(route_error[0])corrections["error"]=route_error;
  static char out[kDiagnosticCapacity];size_t n=serializeJson(d,out,sizeof(out));
  if(!d.overflowed()&&n<sizeof(out)-1){portENTER_CRITICAL(&guard);std::memcpy(cached,out,n+1);cached_busy=test_busy()||probe_phase;portEXIT_CRITICAL(&guard);}published=now;
}
}
void diagnostic_begin(){
  queue=xQueueCreate(1,sizeof(Request));Preferences p;
  if(p.begin("linkdiag",false)){
    if(p.getBool("active",false)){std::snprintf(last_report,sizeof(last_report),"{\"state\":\"interrupted\",\"run\":%lu,\"pair_pass\":false,\"reason\":\"instrument_restarted_during_test\"}",static_cast<unsigned long>(p.getUInt("run",0)));p.putString("report",last_report);p.putBool("active",false);}
    else {String saved=p.getString("report","null");if(saved.length()<sizeof(last_report))std::strcpy(last_report,saved.c_str());}p.end();
  }
  Preferences saved;
  if(saved.begin("linkdiag",true)){String value=saved.getString("selftest","null");if(value.length()<sizeof(self_report))std::strcpy(self_report,value.c_str());saved.end();}
  publish(millis());
}
bool diagnostic_busy(){portENTER_CRITICAL(&guard);bool value=cached_busy;portEXIT_CRITICAL(&guard);return value;}
bool diagnostic_snapshot(char *out,size_t capacity){portENTER_CRITICAL(&guard);size_t n=std::strlen(cached);bool ok=n<capacity;if(ok)std::memcpy(out,cached,n+1);portEXIT_CRITICAL(&guard);return ok;}
// The settings snapshot needs the latest report per medium without copying the
// whole multi-kilobyte report into its bounded document: keep a small summary,
// refreshed only when the stored report actually changes.
char tests_summary[384]="{\"wifi\":null,\"sik\":null}";
uint32_t summary_hash=0;
bool summary_ready=false;
void refresh_tests_summary(){
  const uint32_t hash=correction::crc32(reinterpret_cast<const uint8_t*>(last_report),std::strlen(last_report));
  if(summary_ready&&hash==summary_hash)return;
  summary_hash=hash;summary_ready=true;
  DynamicJsonDocument report(1024);
  if(deserializeJson(report,static_cast<const char*>(last_report)))return;
  const char *transport=report["transport"]|"";
  if(std::strcmp(transport,"sik")&&std::strcmp(transport,"wifi"))return;
  const bool sik=!std::strcmp(transport,"sik");
  DynamicJsonDocument summary(384);
  if(deserializeJson(summary,static_cast<const char*>(tests_summary)))return;
  auto entry=summary[sik?"sik":"wifi"].to<JsonObject>();
  entry["run"]=report["run"]|0u;
  entry["state"]=report["state"]|"unknown";
  entry["reason"]=report["reason"]|"";
  entry["transport"]=transport;
  entry["role"]=report["role"]|"";
  entry["sent"]=report["sent"]|0u;
  entry["received"]=report["received"]|0u;
  entry["errors"]=report["errors"]|0u;
  entry["pair_pass"]=report["pair_pass"]|false;
  entry["local_pass"]=report["local_pass"]|false;
  serializeJson(summary,tests_summary,sizeof(tests_summary));
}
void diagnostic_tests_json(JsonObject out){
  refresh_tests_summary();
  StaticJsonDocument<384> summary;
  if(deserializeJson(summary,static_cast<const char*>(tests_summary))){out["wifi"]=nullptr;out["sik"]=nullptr;return;}
  out["wifi"]=summary["wifi"];
  out["sik"]=summary["sik"];
}
bool diagnostic_request(const char *json){
  StaticJsonDocument<512>d;if(!queue||deserializeJson(d,json))return false;Request r;
  const char *op=d["op"]|"";
  if(!std::strcmp(op,"corrections")){
    r.route=true;const char *t=d["transport"]|"";
    if(d["confirm"]!=true||(std::strcmp(t,"sik")&&std::strcmp(t,"wifi")))return false;
    r.transport=!std::strcmp(t,"sik");
    if(d.containsKey("session"))return false; // Sessions are negotiated, never supplied by a caller.
    return xQueueSend(queue,&r,0)==pdTRUE;
  }
  if(!std::strcmp(op,"selftest")){r.selftest=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}if(!std::strcmp(op,"probe")){r.probe=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}r.cancel=std::strcmp(op,"cancel")==0;
  r.paired=!std::strcmp(op,"pairtest");if(!r.cancel&&!r.paired&&std::strcmp(op,"arm"))return false;
  if(!d["run"].is<uint32_t>())return false;r.config.run=d["run"];
  if(r.paired){if(!d["seconds"].is<uint16_t>())return false;r.config.seconds=d["seconds"];r.config.rate=1000;r.config.mode=0;r.transport=1;
    if(d.containsKey("profile")){if(!d["profile"].is<const char*>())return false;const char *profile=d["profile"];if(std::strcmp(profile,"clean")&&std::strcmp(profile,"injected"))return false;r.profile=!std::strcmp(profile,"clean")?correctiontest::Clean:correctiontest::Injected;}
    if(!linktest::valid_config(r.config)||d["confirm"]!=true)return false;}
  else if(!r.cancel){if(!d["seconds"].is<uint16_t>()||!d["rate"].is<uint16_t>()||!d["mode"].is<uint8_t>())return false;
    r.config.seconds=d["seconds"];r.config.rate=d["rate"];r.config.mode=d["mode"];
    const char *t=d["transport"]|"";if(std::strcmp(t,"wifi")&&std::strcmp(t,"sik"))return false;r.transport=std::strcmp(t,"sik")==0;
    if(!linktest::valid_config(r.config)||d["confirm"]!=true)return false;
  }
  return xQueueSend(queue,&r,0)==pdTRUE;
}
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy){
  Request request;
  if(queue&&xQueueReceive(queue,&request,0)==pdTRUE){
    if(ota_locked()){std::strcpy(route_error,"Firmware update owns the instrument; diagnostic request rejected.");}
    else if(request.route){
      route_error[0]=0;
      if(test_busy()||probe_phase||profile_busy||!survey_diagnostic_acquire())std::strcpy(route_error,"Finish the current test, survey or receiver operation first.");
      else {
        // End completed advanced diagnostic repetitions before giving UART2
        // back to automatic production bootstrap. No old session is reused.
        paired=false;engine=linktest::Engine{};pair_engine.state=correctiontest::Idle;pair_engine.run=0;engine.node=rover?1:0;
        if(!link_service::select(request.transport?pair_session::Transport::Radio:pair_session::Transport::WiFi,rover,now))
          std::strcpy(route_error,"Link preference was not verified saved; local recovery required.");
        survey_diagnostic_release();
      }
    }
    else if(link_service::radio_active()){std::strcpy(route_error,"Select Wi-Fi locally before running advanced diagnostics.");}
    else if(request.selftest){
      if(test_busy()||probe_phase||profile_busy||!survey_diagnostic_acquire())std::strcpy(self_error,"Finish the current test, survey or receiver operation first.");
      else {run_transport_selftest();survey_diagnostic_release();}
    }
    else if(request.probe){if(!test_busy()&&!probe_phase&&!profile_busy&&survey_diagnostic_acquire()){locked=true;radio_transport::begin();radio_transport::discard_input();probe_size=0;probe_text[0]=0;probe_start=now;probe_phase=1;}}
    else if(request.cancel){if(paired){if(pair_engine.busy()&&request.config.run==pair_engine.run)pair_engine.abort(now,"cancelled");}else if(engine.busy()&&request.config.run==engine.config.run)engine.abort(now,"cancelled");}
    else if(!test_busy()&&!probe_phase&&request.config.run!=engine.config.run&&request.config.run!=pair_engine.run){
      if(profile_busy||!survey_diagnostic_acquire()){engine.reason="finish_survey_or_receiver_operation_first";}
      else {
        locked=true;Preferences p;bool saved=false;
        if(p.begin("linkdiag",false)){saved=p.putUInt("run",request.config.run)==4&&p.putBool("active",true)==1;p.end();}
        if(!saved){survey_diagnostic_release();locked=false;engine.reason="cannot_save_test_start";}
        else {paired=request.paired;if(paired)pair_engine.arm(request.config.run,request.config.seconds,rover?1:0,now,request.profile);else engine.arm(request.config,rover?1:0,now);transport=request.transport;persisted=persisted_peer=false;
          if(transport){radio_transport::begin();radio_transport::discard_input();}
          if(paired)start_uart_observation(now);
          if(!transport&&!wifi_transport::start(wifi_transport::Channel::Diagnostics))engine.abort(now,"wifi_diagnostic_socket_unavailable");
        }
      }
    }
  }
  const bool radio_reserved=probe_phase||(paired&&pair_engine.run)||(!paired&&transport&&engine.config.run);
  link_service::service(now,rover,radio_reserved);
  if(paired&&pair_engine.busy()&&pair_engine.node!=(rover?1:0))pair_engine.abort(now,"role_changed");
  if(!paired&&engine.busy()&&engine.node!=(rover?1:0))engine.abort(now,"role_changed");
  if(engine.state==linktest::Idle)engine.node=rover?1:0;
  if(probe_phase){
    for(unsigned budget=0;budget<512;++budget){int c=radio_transport::read_probe_byte();if(c<0)break;if(probe_size<sizeof(probe_text)-1&&c>=32&&c<127){probe_text[probe_size++]=char(c);probe_text[probe_size]=0;}}
    uint32_t elapsed=now-probe_start;
    if(probe_phase==1&&elapsed>=1200){radio_transport::send(reinterpret_cast<const uint8_t*>("+++"),3);probe_phase=2;}
    if(probe_phase==2&&elapsed>=2700){radio_transport::send(reinterpret_cast<const uint8_t*>("\rATI\r"),5);probe_phase=3;}
    if(probe_phase==3&&elapsed>=4200){radio_transport::send(reinterpret_cast<const uint8_t*>("ATO\r"),4);probe_phase=4;}
    if(probe_phase==4&&elapsed>=4800){probe_phase=0;if(!probe_size){std::strcpy(probe_text,"no response");probe_size=11;}survey_diagnostic_release();locked=false;}
  }
  if(paired&&!probe_phase){
    const bool observing=pair_engine.busy();
    if(observing)radio_transport::observe_tick(now);
    pair_engine.tick(now);
    radio_transport::Frame frame;
    size_t budget=2048;
    while(radio_transport::receive(frame,budget)){
      // The paired engines consume a byte stream; each validated envelope is
      // delivered whole, so replay its full 256 bytes in wire order.
      for(size_t i=0;i<sizeof(frame.packet.bytes);++i)pair_engine.byte(frame.packet.bytes[i],now);
    }
    correction::Packet packet;if(pair_engine.next(packet,now)){
      const int written=radio_transport::send(packet.bytes,sizeof(packet));
      if(written<0){/* tx_wait counted by owner */}
      else if(written==int(sizeof(packet)))pair_engine.committed(packet,now);
    }
    if(!pair_engine.busy())stop_uart_observation();
  }
  if(!paired&&engine.config.run&&!probe_phase){
    if(transport&&radio_transport::started()){
      const uint32_t before=radio_transport::decode_stats().diagnostic_errors+radio_transport::decode_stats().discarded_bytes;
      radio_transport::Frame frame;
      size_t budget=2048;
      while(radio_transport::receive(frame,budget)){
        if(frame.kind!=radio_transport::FrameKind::Diagnostic)continue;
        linktest::Packet p;std::memcpy(&p,frame.packet.bytes,sizeof(p));
        engine.receive(p,now); // receive() itself rejects invalid bodies.
      }
      // Raw-byte framing surfaced corruption as engine errors; the framer now
      // consumes bad bytes internally, so keep the report semantics via its
      // per-family counters and the discarded-byte count.
      if(engine.busy())engine.errors+=radio_transport::decode_stats().diagnostic_errors+radio_transport::decode_stats().discarded_bytes-before;
    }else if(!transport&&wifi_transport::started(wifi_transport::Channel::Diagnostics)){
      for(unsigned budget=0;budget<8;++budget){
        IPAddress sender;uint8_t bytes[sizeof(linktest::Packet)];
        const int size=wifi_transport::receive(wifi_transport::Channel::Diagnostics,bytes,sizeof(bytes),sender);
        if(size<=0)break;
        if(size==int(sizeof(linktest::Packet))&&uint32_t(peer)&&sender==peer){linktest::Packet p;std::memcpy(&p,bytes,sizeof(p));engine.receive(p,now);}
      }
    }
    linktest::Packet packet;
    if(engine.next(packet,now)){
      bool sent=false;
      if(transport){sent=radio_transport::send(reinterpret_cast<const uint8_t*>(&packet),sizeof(packet))==int(sizeof(packet));}
      else if(uint32_t(peer))sent=wifi_transport::send(wifi_transport::Channel::Diagnostics,peer,reinterpret_cast<const uint8_t*>(&packet),sizeof(packet));
      if(sent)engine.transmitted(packet,now);
    }
  }
  if(!test_busy()&&!probe_phase&&locked){save_result();survey_diagnostic_release();locked=false;}
  if((paired?pair_engine.state==correctiontest::Done:engine.state==linktest::Done)&&peer_received()&&!persisted_peer)save_result();
  portENTER_CRITICAL(&guard);cached_busy=test_busy()||probe_phase;portEXIT_CRITICAL(&guard);
  if(now-published>=200)publish(now);
}
