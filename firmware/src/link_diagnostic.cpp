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
uint8_t node_role=0; // local role of the last service turn; engines are armed with it
bool locked=false,persisted=false,persisted_peer=false;
uint32_t probe_start=0;uint8_t probe_phase=0;char probe_text[256]={};size_t probe_size=0;
QueueHandle_t queue=nullptr;portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
char cached[kDiagnosticCapacity]="{\"state\":\"idle\"}";
// Latest stored report per medium, so a run on one link never hides the other
// link's result: index 0 is Wi-Fi, 1 is SiK, each with its own NVS slot.
char reports[2][4096]={"null","null"};
uint8_t latest=0;                 // medium slot the raw snapshot's last_report shows
char unattributed[256]="null";    // pre-R9 report whose medium was never recorded
char cancel_reason[48]={};
// A designed software restart (esp_restart): admitted like any other disruptive
// local action, then executed on a later service turn so the HTTP response, the
// snapshot and the serial log leave the instrument before the chip resets.
bool restart_pending=false;uint32_t restart_at=0;
char quick_error[96]={};          // why the last quick-test start was refused
bool quick_locked=false;          // a quick run owns its report settle, not the reservation
// Records why a quick-test start was refused, for the snapshot and the operator.
bool refuse_quick(const char *medium,const char *why){
  std::snprintf(quick_error,sizeof(quick_error),"%s:%s",medium,why);
  return false;
}
char self_report[1536]="null",self_error[80]={};
bool any_engine_busy(){return engine.busy()||pair_engine.busy();}
// UART2 ownership is explicit and bounded, never inferred from the engines'
// retained run ids. A run armed on SiK -- the probe, the paired SiK quick test,
// an advanced packet test on SiK -- owns the stream while it is in flight and
// through the terminal result exchange that follows, because the peer's own
// verdict can still arrive on UART2 after this instrument's engine is done.
// kExchangeMs caps that exchange, so a peer that never reports cannot hold the
// stream; the engines keep their run ids and their verdict either way, since
// link_service settles the operation from diagnostic_quick_test_ready(). Wi-Fi
// runs never own UART2.
constexpr uint32_t kExchangeMs=5000;
bool radio_owned=false;   // this turn's ownership: gates the stream, both engine blocks and the published busy state
uint32_t exchange_run=0;  // run whose terminal exchange this layer is serving
uint32_t exchange_at=0;   // millis that run's terminal state was first observed
// Run id of the medium the last start took, 0 when that medium is not SiK.
uint32_t radio_run_id(){return transport?(paired?pair_engine.run:engine.config.run):0;}
bool radio_in_flight(){return probe_phase||(transport&&any_engine_busy());}
// The run's verdict is complete exactly when diagnostic_quick_test_ready() says
// so: the peer's own report arrived, or the run failed without one. That is what
// the link owner settles its operation from, so the stream is handed back as
// soon as it holds or kExchangeMs has passed since the terminal state was seen.
bool exchange_open(uint32_t now){return exchange_at&&now-exchange_at<kExchangeMs&&!diagnostic_quick_test_ready(now);}
// True while this layer owns UART2 for the turn: in flight, or inside the
// bounded terminal result exchange.
bool radio_owner(uint32_t now){return radio_in_flight()||exchange_open(now);}
// Opens, holds and releases the one bounded exchange window. It belongs to one
// run, opens at that run's first terminal turn and closes when the link owner's
// verdict is complete or kExchangeMs has elapsed since; a run whose window has
// closed never opens another, and a run in flight owns the stream outright.
void maintain_radio_ownership(uint32_t now){
  const uint32_t run=radio_run_id();
  if(!run){exchange_run=exchange_at=0;return;}
  if(radio_in_flight()){exchange_at=0;return;}
  if(run!=exchange_run){exchange_run=run;exchange_at=diagnostic_quick_test_ready(now)?0:now;return;}
  if(exchange_at&&!exchange_open(now))exchange_at=0;
}
const char *medium_name(uint8_t medium){return medium?"sik":"wifi";}
// The engines refuse a repeated run id, so the generated id skips the target
// engine's current one and the other engine's, which keeps a stale slot from
// colliding with a later run. One pass, always in range.
uint32_t fresh_run(uint32_t first,uint32_t second){
  uint32_t run=100000u+esp_random()%900000u;
  while(run==first||run==second)run=run<999999u?run+1u:100000u;
  return run;
}
// Start marker: the medium (and the paired profile and shape) of the run in
// flight, so a reboot during the run can report it as interrupted for the link
// it was actually using.
bool save_start_marker(uint32_t run,uint8_t medium,uint8_t node,uint8_t profile,bool was_paired){
  Preferences p;if(!p.begin("linkdiag",false))return false;
  const bool saved=p.putUInt("run",run)==4&&p.putBool("active",true)==1&&p.putUInt("medium",medium)==4&&
    p.putUInt("role",node)==4&&p.putUInt("profile",profile)==4&&p.putBool("paired",was_paired)==1;
  p.end();return saved;
}
void clear_start_marker(){Preferences p;if(p.begin("linkdiag",false)){p.putBool("active",false);p.end();}}
// The raw diagnostic snapshot's last_report: the unattributable pre-R9 report
// while it is all we have, otherwise the newest per-medium report.
const char *latest_report(){return std::strcmp(unattributed,"null")?unattributed:reports[latest];}
// Crash marker of a run whose medium the start marker recorded.
void interrupted_report(uint8_t medium,uint8_t node,uint8_t profile,bool was_paired,uint32_t run,char *out,size_t capacity){
  if(was_paired)std::snprintf(out,capacity,"{\"kind\":\"paired_rtcm_faults\",\"suite_version\":2,\"profile\":\"%s\",\"run\":%lu,\"state\":\"interrupted\",\"reason\":\"instrument_restarted_during_test\",\"role\":\"%s\",\"transport\":\"sik\",\"local_pass\":false,\"pair_pass\":false}",
    profile==correctiontest::Clean?"clean":"injected",static_cast<unsigned long>(run),node?"ROVER":"BASE");
  else std::snprintf(out,capacity,"{\"run\":%lu,\"state\":\"interrupted\",\"reason\":\"instrument_restarted_during_test\",\"role\":\"%s\",\"transport\":\"%s\",\"local_pass\":false,\"pair_pass\":false}",
    static_cast<unsigned long>(run),node?"ROVER":"BASE",medium_name(medium));
}
// A stored body is only trusted when it is a whole report of a known shape. A
// body overwritten mid-write still parses as JSON, so the state and role text are
// what separate a real report from fragments: the report writer only ever stores
// a terminal state, and every report shape carries the role. The pre-R9 crash
// marker is the one body without a role.
bool report_ok(JsonVariantConst report){
  const char *state=report["state"]|"";
  if(std::strcmp(state,"done")&&std::strcmp(state,"failed")&&std::strcmp(state,"interrupted"))return false;
  if(!std::strcmp(state,"interrupted")&&!report.containsKey("role"))return true;
  const char *role=report["role"]|"";
  return !std::strcmp(role,"BASE")||!std::strcmp(role,"ROVER");
}
// Medium a stored pre-R9 report body names, -1 when it names none, -2 when it is
// not a whole report at all.
int stored_medium(const String &value){
  if(value=="null"||value.length()>=sizeof(reports[0]))return -1;
  // The stored body can be a whole multi-kilobyte report: a document smaller
  // than its content would fail to parse it and lose the migrated report.
  DynamicJsonDocument report(4096);
  if(deserializeJson(report,value)||!report_ok(report))return -2;
  const char *name=report["transport"]|"";
  return !std::strcmp(name,"sik")?1:!std::strcmp(name,"wifi")?0:-1;
}
// The raw snapshot shows the newest slot that actually holds a report.
void normalize_latest(){if(!std::strcmp(reports[latest],"null")&&std::strcmp(reports[latest?0:1],"null"))latest=latest?0:1;}
// Loads one stored body into its RAM mirror. False means the body is not a whole
// report: the caller drops its durable slot instead of showing fragments.
bool load_report(uint8_t medium,const String &value){
  if(value.length()>=sizeof(reports[0]))return false;
  DynamicJsonDocument report(4096);
  if(deserializeJson(report,value)||!report_ok(report))return false;
  std::strcpy(reports[medium],value.c_str());
  return true;
}
// Discards a body that is not a whole report from RAM and from its durable slot,
// so neither the snapshot nor last_tests can print fragments of it.
void drop_report(uint8_t medium){
  std::strcpy(reports[medium],"null");normalize_latest();
  Preferences p;if(p.begin("linkdiag",false)){p.remove(medium?"report_sik":"report_wifi");p.end();}
}
bool cached_busy=false;uint32_t published=0;
// Requests this layer still serves itself: a local link selection, the wiring
// probe and the fault self-test. A packet or RTCM run is never armed from here,
// because the pair operation owns that start and both peers must run one shape.
struct Request{bool route=false,probe=false,selftest=false,restart=false;uint8_t transport=0;};
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
  const uint8_t medium=transport?1:0;
  if(d.overflowed()||measureJson(d)>=sizeof(reports[0])){persisted=false;return;}
  char *text=reports[medium];serializeJson(d,text,sizeof(reports[medium]));
  latest=medium;std::strcpy(unattributed,"null");
  Preferences p;if(p.begin("linkdiag",false)){
    const char *key=medium?"report_sik":"report_wifi";
    persisted=p.putString(key,text)>0&&p.getString(key)==text;
    // The run is no longer in flight, and the stored report is this medium's
    // latest whatever the outcome was.
    if(persisted){p.putBool("active",false);p.putUInt("latest",medium);}p.end();
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
  DynamicJsonDocument d(10240);result_json(d.to<JsonObject>());
  // radio_owned is the handoff an acceptance run watches: true while this layer
  // holds UART2, which is exactly the run in flight or its bounded terminal
  // result exchange, and the same value already given to link_service.
  d["busy"]=test_busy()||radio_owned;d["radio_owned"]=radio_owned;d["persisted"]=persisted;
  // A restart that has been admitted but not yet executed: the controller can
  // observe it, and it stays true across the grace window.
  d["restart_pending"]=restart_pending;
  d["radio_probe"]=probe_phase?"checking":probe_size?(std::strstr(probe_text,"SiK ")?"UART responds as SiK":"No SiK identity response; check power, baud and crossed TX/RX"):"not checked";
  d["radio_probe_response"]=probe_text;
  if(probe_phase){d["state"]="probing";d["busy"]=true;}
  d["uptime_ms"]=now;d["remaining_seconds"]=engine.state==linktest::Running&&int32_t(now-engine.start_at)>=0?std::max(0,int(engine.config.seconds)-(int(now-engine.start_at)/1000)):0;
  if(paired)d["remaining_seconds"]=pair_engine.state==correctiontest::Running&&int32_t(now-pair_engine.start_at)>=0?std::max(0,int(pair_engine.seconds)-int((now-pair_engine.start_at)/1000)):0;
  DynamicJsonDocument previous(4096);if(!deserializeJson(previous,static_cast<const char*>(latest_report())))d["last_report"]=previous.as<JsonVariant>();
  DynamicJsonDocument self(2048);if(!deserializeJson(self,static_cast<const char*>(self_report)))d["self_test"]=self.as<JsonVariant>();
  d["self_test_error"]=self_error;d["quick_test_error"]=quick_error;auto corrections=d.createNestedObject("corrections");
  link_service::write_json(corrections,now);
  if(route_error[0])corrections["error"]=route_error;
  static char out[kDiagnosticCapacity];size_t n=serializeJson(d,out,sizeof(out));
  if(!d.overflowed()&&n<sizeof(out)-1){portENTER_CRITICAL(&guard);std::memcpy(cached,out,n+1);cached_busy=test_busy()||radio_owned;portEXIT_CRITICAL(&guard);}published=now;
}
}
void diagnostic_begin(){
  queue=xQueueCreate(1,sizeof(Request));Preferences p;
  if(p.begin("linkdiag",false)){
    // A stored body that is not a whole report is dropped here, not displayed.
    if(p.isKey("report_wifi")){const String value=p.getString("report_wifi","null");if(!load_report(0,value))p.remove("report_wifi");}
    if(p.isKey("report_sik")){const String value=p.getString("report_sik","null");if(!load_report(1,value))p.remove("report_sik");}
    if(p.isKey("latest")){const uint32_t index=p.getUInt("latest",0);latest=index<2?uint8_t(index):uint8_t(0);}
    // Migration: the single pre-R9 slot is read only while neither per-medium
    // slot exists, so a migrated report can never outlive a newer one. The body
    // names its own medium; a body without one cannot be attributed to a medium
    // and stays visible in the raw snapshot only, and a body that is not a whole
    // report is ignored entirely.
    if(!p.isKey("report_wifi")&&!p.isKey("report_sik")){
      const String value=p.getString("report","null");
      const int medium=stored_medium(value);
      if(medium>=0){std::strcpy(reports[medium],value.c_str());if(!p.isKey("latest"))latest=uint8_t(medium);}
      else if(medium==-1&&value!="null"&&value.length()<sizeof(unattributed))std::strcpy(unattributed,value.c_str());
    }
    if(p.getBool("active",false)){
      // The in-flight run is the newest report of its medium, interrupted by the
      // restart: report it there instead of dropping it from last_tests.
      const uint32_t run=p.getUInt("run",0);
      if(p.isKey("medium")){
        const uint8_t medium=p.getUInt("medium",0)?1:0;
        char text[384];
        interrupted_report(medium,p.getUInt("role",0)?1:0,uint8_t(p.getUInt("profile",0)&1),p.getBool("paired",false),run,text,sizeof(text));
        std::strcpy(reports[medium],text);latest=medium;
        p.putUInt("latest",medium);p.putString(medium?"report_sik":"report_wifi",text);
      }else std::snprintf(unattributed,sizeof(unattributed),"{\"state\":\"interrupted\",\"run\":%lu,\"pair_pass\":false,\"reason\":\"instrument_restarted_during_test\"}",static_cast<unsigned long>(run));
      p.putBool("active",false);
    }
    // Never show an empty slot as the latest report while the other medium has
    // one; the durable value is rewritten by the next save.
    normalize_latest();
    p.end();
  }
  Preferences saved;
  if(saved.begin("linkdiag",true)){String value=saved.getString("selftest","null");if(value.length()<sizeof(self_report))std::strcpy(self_report,value.c_str());saved.end();}
  publish(millis());
}
bool diagnostic_busy(){portENTER_CRITICAL(&guard);bool value=cached_busy;portEXIT_CRITICAL(&guard);return value;}
bool diagnostic_snapshot(char *out,size_t capacity){portENTER_CRITICAL(&guard);size_t n=std::strlen(cached);bool ok=n<capacity;if(ok)std::memcpy(out,cached,n+1);portEXIT_CRITICAL(&guard);return ok;}
// The settings snapshot needs the latest report per medium without copying the
// whole multi-kilobyte report into its bounded document: keep a small summary,
// refreshed only when a medium's stored report actually changes.
char tests_summary[640]="{\"wifi\":null,\"sik\":null}";
uint32_t summary_hash[2]={0,0};
bool summary_ready[2]={false,false};
void refresh_tests_summary(){
  uint32_t hash[2];bool stale[2]={false,false};unsigned changed=0;
  for(unsigned medium=0;medium<2;++medium){
    hash[medium]=correction::crc32(reinterpret_cast<const uint8_t*>(reports[medium]),std::strlen(reports[medium]));
    stale[medium]=!summary_ready[medium]||summary_hash[medium]!=hash[medium];
    if(stale[medium])++changed;
  }
  if(!changed)return;
  DynamicJsonDocument summary(1024);
  if(deserializeJson(summary,static_cast<const char*>(tests_summary)))return;
  for(unsigned medium=0;medium<2;++medium){
    if(!stale[medium])continue;
    const char *body=reports[medium];
    if(!std::strcmp(body,"null")){summary[medium_name(uint8_t(medium))]=nullptr;continue;}
    // A full stored report needs more pool than its text: a 1022-byte paired
    // report already overflows a 1024-byte document and would show as null.
    DynamicJsonDocument report(4096);
    if(deserializeJson(report,body)||!report_ok(report)){summary[medium_name(uint8_t(medium))]=nullptr;drop_report(uint8_t(medium));continue;}
    auto entry=summary[medium_name(uint8_t(medium))].to<JsonObject>();
    // The string fields are copied through String(). ArduinoJson links a
    // `const char*` value instead of copying it, so assigning the parsed
    // document's own pointers would leave the summary reading whatever the next
    // medium's parse reuses those bytes for.
    entry["run"]=report["run"]|0u;
    entry["state"]=String(report["state"]|"unknown");
    entry["reason"]=String(report["reason"]|"");
    entry["transport"]=medium_name(uint8_t(medium));
    entry["role"]=String(report["role"]|"");
    entry["sent"]=report["sent"]|0u;
    entry["received"]=report["received"]|0u;
    entry["errors"]=report["errors"]|0u;
    entry["pair_pass"]=report["pair_pass"]|false;
    entry["local_pass"]=report["local_pass"]|false;
  }
  // A summary that does not fit is kept as it was and retried next turn rather
  // than published truncated, which would be unreadable JSON.
  char text[sizeof(tests_summary)];
  const size_t length=serializeJson(summary,text,sizeof(text));
  if(summary.overflowed()||length>=sizeof(text))return;
  std::memcpy(tests_summary,text,length+1);
  for(unsigned medium=0;medium<2;++medium)if(stale[medium]){summary_hash[medium]=hash[medium];summary_ready[medium]=true;}
}
void diagnostic_tests_json(JsonObject out){
  refresh_tests_summary();
  StaticJsonDocument<1024> summary;
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
  if(!std::strcmp(op,"selftest")){r.selftest=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}
  if(!std::strcmp(op,"probe")){r.probe=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}
  // A designed restart of this instrument: the escape hatch for a state that
  // cannot be recovered in place (for example a wedged operation or a reservation
  // held by a fault). Confirm is required, and admission is the same single-slot
  // queue as every other diagnostic action.
  if(!std::strcmp(op,"restart")){r.restart=true;return d["confirm"]==true&&xQueueSend(queue,&r,0)==pdTRUE;}
  // A run is never armed from here: the pair operation starts it on both peers.
  return false;
}
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy){
  node_role=rover?1:0; // a quick test admitted this turn is armed with this role
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
    // Advanced diagnostics and quick tests take the medium they are started on;
    // which medium production is using is not an admission condition.
    else if(request.selftest){
      if(test_busy()||probe_phase||profile_busy||!survey_diagnostic_acquire())std::strcpy(self_error,"Finish the current test, survey or receiver operation first.");
      else {run_transport_selftest();survey_diagnostic_release();}
    }
    else if(request.probe){if(!test_busy()&&!probe_phase&&!profile_busy&&survey_diagnostic_acquire()){locked=true;radio_transport::begin();radio_transport::discard_input();probe_size=0;probe_text[0]=0;probe_start=now;probe_phase=1;}}
    else if(request.restart){
      route_error[0]=0;
      // Same gates as every other disruptive local action: an update owns the
      // instrument (locking or mid-transfer), a running test must finish, and the
      // survey/diagnostic reservation is the proof that nothing else is collecting
      // or writing. Holding it through the grace window stops anything new from
      // starting under a chip that is about to reset.
      if(ota_paused())std::strcpy(route_error,"Firmware update is transferring; restart refused.");
      else if(test_busy()||probe_phase||profile_busy)std::strcpy(route_error,"Finish the current test or operation first.");
      else if(!survey_diagnostic_acquire())std::strcpy(route_error,"Finish the current test, survey or receiver operation first.");
      else {restart_pending=true;restart_at=now+500;}
    }
  }
  // One ownership value per turn: the same predicate gates the stream argument,
  // this layer's own UART2 reads and writes, and the published busy state.
  radio_owned=radio_owner(now);
  link_service::service(now,rover,radio_owned);
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
  if(radio_owned&&paired){
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
    // UART2 is read only while this layer owns it; the diagnostics socket is the
    // Wi-Fi gate and says nothing about UART2.
    if(transport&&radio_owned&&radio_transport::started()){
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
      if(transport){if(radio_owned)sent=radio_transport::send(reinterpret_cast<const uint8_t*>(&packet),sizeof(packet))==int(sizeof(packet));}
      else if(uint32_t(peer))sent=wifi_transport::send(wifi_transport::Channel::Diagnostics,peer,reinterpret_cast<const uint8_t*>(&packet),sizeof(packet));
      if(sent)engine.transmitted(packet,now);
    }
  }
  if(!test_busy()&&!probe_phase&&(locked||quick_locked)){
    save_result();
    // Only the reservation this layer took is released here; a quick test inside a
    // link operation leaves the link owner's reservation to the link owner.
    if(locked){survey_diagnostic_release();locked=false;}
    quick_locked=false;
  }
  // The bounded terminal result exchange, after the result settle: UART2 stays
  // this layer's only until the link owner's verdict is complete (the peer's own
  // report arrived, or the run failed without one) or kExchangeMs has elapsed.
  // The engines and their run ids are left as they are, because the link owner
  // still settles its operation from diagnostic_quick_test_ready().
  maintain_radio_ownership(now);
  if((paired?pair_engine.state==correctiontest::Done:engine.state==linktest::Done)&&peer_received()&&!persisted_peer)save_result();
  portENTER_CRITICAL(&guard);cached_busy=test_busy()||radio_owned;portEXIT_CRITICAL(&guard);
  if(now-published>=200)publish(now);
  // The designed restart runs here, on the service turn after the one that
  // admitted it: the HTTP task has answered 202, the snapshot reports
  // restart_pending and this line reaches the serial log first. esp_restart()
  // resets the chip, which clears RAM state exactly as a power cycle would.
  if(restart_pending&&int32_t(now-restart_at)>=0){
    Serial.println("RESTART: requested by the controller; restarting");
    Serial.flush();
    esp_restart();
  }
}
// The quick test is the existing engine for that medium, armed with the shape
// the operation admitted. /transport/ (0 Wi-Fi, 1 SiK) is also the medium whose
// stored report and start marker the run belongs to.
bool diagnostic_quick_test_start(uint8_t medium,uint8_t profile,uint32_t run,uint16_t seconds,uint16_t rate,uint8_t mode,uint32_t now){
  if(medium>1)return refuse_quick("transport","unsupported");
  // One rule says which shape a medium offers; this layer publishes the branch
  // it names. The packet engine runs any rate and direction, the paired RTCM
  // engine has neither knob, and only that engine injects faults.
  const char *unsupported=link_operation::test_parameter_refusal(
    medium?link_operation::Transport::Radio:link_operation::Transport::WiFi,profile,seconds,rate,mode);
  if(unsupported)return refuse_quick(medium_name(medium),unsupported);
  if(any_engine_busy())return refuse_quick(medium_name(medium),"engine_busy");
  if(probe_phase)return refuse_quick(medium_name(medium),"probe_running");
  if(ota_locked())return refuse_quick(medium_name(medium),"ota_active");
  linktest::Config config;config.run=run;config.seconds=seconds;config.rate=rate;config.mode=mode;
  if(!linktest::valid_config(config))return refuse_quick(medium_name(medium),"run_id_rejected");
  // The tested medium is brought up here: a quick test is the moment to start it,
  // so no prior probe, route selection or medium state is required. UART2 begin()
  // is idempotent and reports started() immediately.
  if(medium){radio_transport::begin();if(!radio_transport::started())return refuse_quick("sik","radio_unavailable");}
  else if(!wifi_transport::start(wifi_transport::Channel::Diagnostics))return refuse_quick("wifi","socket_unavailable");
  // The operation that asked for this test already reserved the instrument for
  // its own staging, and the reservation is one flag: acquiring it again fails by
  // design. Hold it only when this call is the one that took it, so a quick test
  // never releases a reservation the link owner still owns.
  if(!locked&&survey_diagnostic_acquire())locked=true;
  if(!save_start_marker(run,medium,node_role,profile,medium==1)){
    if(locked){survey_diagnostic_release();locked=false;}
    return refuse_quick(medium_name(medium),"cannot_save_test_start");
  }
  if(!(medium?pair_engine.arm(run,config.seconds,node_role,now,profile):engine.arm(config,node_role,now))){
    clear_start_marker();
    if(locked){survey_diagnostic_release();locked=false;}
    return refuse_quick(medium_name(medium),"engine_arm_rejected");
  }
  paired=medium==1;transport=medium;persisted=persisted_peer=false;quick_locked=true;quick_error[0]=0;
  if(medium){radio_transport::discard_input();start_uart_observation(now);}
  return true;
}
bool diagnostic_quick_test_start(uint8_t medium,uint8_t profile,uint32_t run,uint32_t now){
  // The Settings quick tests keep the canonical quick profile.
  return diagnostic_quick_test_start(medium,profile,run,30,1000,0,now);
}
bool diagnostic_quick_test_start(uint8_t medium,uint8_t profile,uint32_t now){
  // A lone instrument generates its own id; a pair operation passes one shared
  // id to the overload above, because a mismatched peer id is never accepted.
  return diagnostic_quick_test_start(medium,profile,fresh_run(medium?pair_engine.run:engine.config.run,medium?engine.config.run:pair_engine.run),now);
}
// The service turn advances both engines and settles a finished run's report, so
// the queries below never advance engine state themselves.
bool diagnostic_quick_test_busy(uint32_t){return any_engine_busy();}
bool diagnostic_quick_test_finished(uint32_t){return engine.state==linktest::Done||engine.state==linktest::Failed||pair_engine.state==correctiontest::Done||pair_engine.state==correctiontest::Failed;}
// The run has ended and its verdict is complete: a terminal state whose peer
// report arrived, or a failure that will never produce one (peer_timeout,
// cancellation, role change). "peer_report_received" in result_json() is
// exactly the peer flag used here, for both engines.
bool diagnostic_quick_test_ready(uint32_t){
  if(paired)return pair_engine.state==correctiontest::Failed||(pair_engine.state==correctiontest::Done&&pair_engine.peer_received);
  return engine.state==linktest::Failed||(engine.state==linktest::Done&&engine.peer_result);
}
bool diagnostic_quick_test_pass(uint32_t){return paired?pair_engine.pair_pass():engine.pair_pass();}
void diagnostic_quick_test_cancel(const char *why,uint32_t now){
  if(!why||!*why)why="cancelled";
  // The engines keep the pointer, so the text lives until the report is saved.
  std::snprintf(cancel_reason,sizeof(cancel_reason),"%s",why);
  if(engine.busy())engine.abort(now,cancel_reason);
  if(pair_engine.busy())pair_engine.abort(now,cancel_reason);
}
