#include "link_diagnostic.h"
#include "link_diagnostic_core.h"
#include "correction_transport_selftest.h"
#include "correction_pair_test.h"
#include "correction_bridge.h"
#include <esp_system.h>
#include "survey_service.h"
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <freertos/queue.h>

namespace {
linktest::Engine engine;HardwareSerial radio(2);WiFiUDP udp;
correctiontest::Engine pair_engine;bool paired=false;
correction::Bridge live;char route_error[96]={};
uint32_t live_tx_wait=0,live_output_rejected=0,live_short_writes=0;
void live_json(JsonObject d){
  d["transport"]=live.active()?"sik":"wifi";d["session"]=live.session();d["fault"]=live.fault();d["station"]=live.station();d["error"]=route_error;
  d["submitted"]=live.submitted;d["envelopes"]=live.envelopes;d["received"]=live.complete;
  d["station_rejected"]=live.station_rejected;d["queue_pending"]=live.queue().size();d["queue_expired"]=live.queue().expired;
  d["queue_overflow"]=live.queue().overflow;d["queue_replaced"]=live.queue().replaced;d["sender_expired"]=live.sender_expired();
  d["wire_errors"]=live.stats().wire_errors;d["assembly_expired"]=live.stats().expired;d["wrong_session"]=live.stats().wrong_session;
  d["replays"]=live.stats().replays;d["rtcm_errors"]=live.stats().rtcm_errors;d["tx_wait"]=live_tx_wait;
  d["output_rejected"]=live_output_rejected;d["short_writes"]=live_short_writes;d["workspace_bytes"]=sizeof(live);
  const auto output=correction_output_stats();auto out=d.createNestedObject("output");out["forwarded"]=output.forwarded;
  out["expired"]=output.expired;out["overflow"]=output.overflow;out["wait_polls"]=output.waiting;out["faults"]=output.faults;out["pending"]=output.queued;
}
bool test_busy(){return paired?pair_engine.busy():engine.busy();}
bool peer_received(){return paired?pair_engine.peer_received:engine.peer_result;}
bool radio_started=false,udp_started=false,locked=false,persisted=false,persisted_peer=false;
uint32_t probe_start=0;uint8_t probe_phase=0;char probe_text[256]={};size_t probe_size=0;
struct UartErrors{uint32_t fifo=0,buffer=0,frame=0,parity=0,brk=0;};
portMUX_TYPE uart_guard=portMUX_INITIALIZER_UNLOCKED;
UartErrors uart_errors;bool uart_monitoring=false;
struct UartWork{uint32_t rx_bytes=0,tx_bytes=0,rx_peak=0,service_gap=0,tx_wait=0,short_writes=0,last_service=0;bool observed=false;}uart_work;
void radio_error(hardwareSerial_error_t error){
  // HardwareSerial event task: counters only, no UART reads, JSON or logging here.
  portENTER_CRITICAL(&uart_guard);
  if(uart_monitoring)switch(error){
    case UART_FIFO_OVF_ERROR:++uart_errors.fifo;break;case UART_BUFFER_FULL_ERROR:++uart_errors.buffer;break;
    case UART_FRAME_ERROR:++uart_errors.frame;break;case UART_PARITY_ERROR:++uart_errors.parity;break;
    case UART_BREAK_ERROR:++uart_errors.brk;break;default:break;
  }
  portEXIT_CRITICAL(&uart_guard);
}
void start_radio(){if(!radio_started){radio.setRxBufferSize(4096);radio.setTxBufferSize(1024);radio.onReceiveError(radio_error);radio.begin(57600,SERIAL_8N1,18,17);radio_started=true;}}
void start_uart_observation(uint32_t now){
  uart_work=UartWork{};uart_work.last_service=now;uart_work.observed=true;
  portENTER_CRITICAL(&uart_guard);uart_errors=UartErrors{};uart_monitoring=true;portEXIT_CRITICAL(&uart_guard);
}
void stop_uart_observation(){portENTER_CRITICAL(&uart_guard);uart_monitoring=false;portEXIT_CRITICAL(&uart_guard);}
void uart_json(JsonObject d){
  UartErrors e;portENTER_CRITICAL(&uart_guard);e=uart_errors;portEXIT_CRITICAL(&uart_guard);
  d["observed"]=uart_work.observed;d["fifo_overflow"]=e.fifo;d["buffer_full"]=e.buffer;d["frame_errors"]=e.frame;d["parity_errors"]=e.parity;d["breaks"]=e.brk;
  d["rx_bytes"]=uart_work.rx_bytes;d["tx_bytes"]=uart_work.tx_bytes;d["rx_backlog_peak"]=uart_work.rx_peak;d["max_service_gap_ms"]=uart_work.service_gap;
  d["tx_wait_polls"]=uart_work.tx_wait;d["short_writes"]=uart_work.short_writes;
}
uint8_t transport=0; // 0 Wi-Fi; 1 SiK
uint8_t rx[sizeof(linktest::Packet)]={};size_t rx_size=0;
QueueHandle_t queue=nullptr;portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
char cached[kDiagnosticCapacity]="{\"state\":\"idle\"}",last_report[4096]="null";
char self_report[1536]="null",self_error[80]={};
bool cached_busy=false;uint32_t published=0;
struct Request{bool route=false;uint32_t session=0;bool cancel=false,probe=false,selftest=false,paired=false;linktest::Config config;uint8_t transport=0,profile=correctiontest::Injected;};
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
  d["self_test_error"]=self_error;live_json(d.createNestedObject("corrections"));
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
bool correction_radio_active(){return live.active();}
bool correction_radio_linked(uint32_t now){return live.linked(now);}
bool correction_radio_submit(const uint8_t *frame,size_t size,uint32_t now){return live.enqueue(frame,size,now);}
void correction_radio_stop(){live.stop();correction_output_reset();}
bool diagnostic_busy(){portENTER_CRITICAL(&guard);bool value=cached_busy;portEXIT_CRITICAL(&guard);return value;}
bool diagnostic_snapshot(char *out,size_t capacity){portENTER_CRITICAL(&guard);size_t n=std::strlen(cached);bool ok=n<capacity;if(ok)std::memcpy(out,cached,n+1);portEXIT_CRITICAL(&guard);return ok;}
bool diagnostic_request(const char *json){
  StaticJsonDocument<512>d;if(!queue||deserializeJson(d,json))return false;Request r;
  const char *op=d["op"]|"";
  if(!std::strcmp(op,"corrections")){
    r.route=true;const char *t=d["transport"]|"";
    if(d["confirm"]!=true||(std::strcmp(t,"sik")&&std::strcmp(t,"wifi")))return false;
    r.transport=!std::strcmp(t,"sik");
    if(d.containsKey("session")){if(!d["session"].is<uint32_t>())return false;r.session=d["session"];if(r.session&&r.session<1000000)return false;}
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
    if(request.route){
      route_error[0]=0;
      if(test_busy()||probe_phase||profile_busy||!survey_diagnostic_acquire())std::strcpy(route_error,"Finish the current test, survey or receiver operation first.");
      else {
        if(request.transport&&((rover&&request.session<1000000)||(!rover&&request.session)))std::strcpy(route_error,"Start a new Base session, then copy its number to Rover.");
        else if(request.transport&&rover&&live.active()&&live.session()==request.session){
          // Retried join requests must not reset replay history or queues.
        }
        else {
          correction_radio_stop();
          // Stop completed diagnostic result repetitions before assigning UART2.
          paired=false;engine=linktest::Engine{};pair_engine.state=correctiontest::Idle;pair_engine.run=0;engine.node=rover?1:0;
          if(request.transport){uint32_t session=request.session;
            if(!rover){do{session=esp_random();}while(session<1000000);}
            start_radio();live.begin(session,rover);live_tx_wait=live_output_rejected=live_short_writes=0;
          }
        }
        survey_diagnostic_release();
      }
    }
    else if(live.active()){std::strcpy(route_error,"Switch corrections to Wi-Fi before running diagnostics.");}
    else if(request.selftest){
      if(test_busy()||probe_phase||profile_busy||!survey_diagnostic_acquire())std::strcpy(self_error,"Finish the current test, survey or receiver operation first.");
      else {run_transport_selftest();survey_diagnostic_release();}
    }
    else if(request.probe){if(!test_busy()&&!probe_phase&&!profile_busy&&survey_diagnostic_acquire()){locked=true;start_radio();while(radio.available())radio.read();probe_size=0;probe_text[0]=0;probe_start=now;probe_phase=1;}}
    else if(request.cancel){if(paired){if(pair_engine.busy()&&request.config.run==pair_engine.run)pair_engine.abort(now,"cancelled");}else if(engine.busy()&&request.config.run==engine.config.run)engine.abort(now,"cancelled");}
    else if(!test_busy()&&!probe_phase&&request.config.run!=engine.config.run&&request.config.run!=pair_engine.run){
      if(profile_busy||!survey_diagnostic_acquire()){engine.reason="finish_survey_or_receiver_operation_first";}
      else {
        locked=true;Preferences p;bool saved=false;
        if(p.begin("linkdiag",false)){saved=p.putUInt("run",request.config.run)==4&&p.putBool("active",true)==1;p.end();}
        if(!saved){survey_diagnostic_release();locked=false;engine.reason="cannot_save_test_start";}
        else {paired=request.paired;if(paired)pair_engine.arm(request.config.run,request.config.seconds,rover?1:0,now,request.profile);else engine.arm(request.config,rover?1:0,now);transport=request.transport;persisted=persisted_peer=false;rx_size=0;
          if(transport){start_radio();while(radio.available())radio.read();}
          if(paired)start_uart_observation(now);
          if(!transport&&!udp_started)udp_started=udp.begin(22346);
          if(!transport&&!udp_started)engine.abort(now,"wifi_diagnostic_socket_unavailable");
        }
      }
    }
  }
  if(live.active()&&live.rover()!=rover){correction_radio_stop();std::strcpy(route_error,"Role changed; select a new correction session.");}
  if(live.active()){
    live.tick(now);
    for(unsigned budget=0;budget<2048&&radio.available();++budget){
      if(live.byte(uint8_t(radio.read()),now)&&!correction_radio_input(live.data(),live.size(),now-live.known_age(now)))++live_output_rejected;
    }
    correction::Packet packet;
    if(live.next(packet,now)){
      if(radio.availableForWrite()>=int(sizeof(packet))){
        const size_t written=radio.write(packet.bytes,sizeof(packet));
        if(written==sizeof(packet))live.committed();
        else {++live_short_writes;live.fail();correction_output_reset();std::strcpy(route_error,"Radio output fault; select a new session.");}
      }else ++live_tx_wait;
    }
  }
  if(paired&&pair_engine.busy()&&pair_engine.node!=(rover?1:0))pair_engine.abort(now,"role_changed");
  if(!paired&&engine.busy()&&engine.node!=(rover?1:0))engine.abort(now,"role_changed");
  if(engine.state==linktest::Idle)engine.node=rover?1:0;
  if(probe_phase){
    for(unsigned budget=0;budget<512&&radio.available();++budget){int c=radio.read();if(probe_size<sizeof(probe_text)-1&&c>=32&&c<127){probe_text[probe_size++]=char(c);probe_text[probe_size]=0;}}
    uint32_t elapsed=now-probe_start;
    if(probe_phase==1&&elapsed>=1200){radio.print("+++");probe_phase=2;}
    if(probe_phase==2&&elapsed>=2700){radio.print("\rATI\r");probe_phase=3;}
    if(probe_phase==3&&elapsed>=4200){radio.print("ATO\r");probe_phase=4;}
    if(probe_phase==4&&elapsed>=4800){probe_phase=0;if(!probe_size){std::strcpy(probe_text,"no response");probe_size=11;}survey_diagnostic_release();locked=false;}
  }
  if(paired&&!probe_phase){
    const bool observing=pair_engine.busy();
    if(observing){uart_work.service_gap=std::max(uart_work.service_gap,now-uart_work.last_service);uart_work.last_service=now;uart_work.rx_peak=std::max(uart_work.rx_peak,uint32_t(std::max(0,radio.available())));}
    pair_engine.tick(now);
    for(unsigned budget=0;budget<2048&&radio.available();++budget){pair_engine.byte(uint8_t(radio.read()),now);if(observing)++uart_work.rx_bytes;}
    correction::Packet packet;if(pair_engine.next(packet,now)){
      if(radio.availableForWrite()>=sizeof(packet)){
        const size_t written=radio.write(packet.bytes,sizeof(packet));if(observing){uart_work.tx_bytes+=written;if(written!=sizeof(packet))++uart_work.short_writes;}
        if(written==sizeof(packet))pair_engine.committed(packet,now);
      }else if(observing)++uart_work.tx_wait;
    }
    if(!pair_engine.busy())stop_uart_observation();
  }
  if(!paired&&engine.config.run&&!probe_phase){
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
  if(!test_busy()&&!probe_phase&&locked){save_result();survey_diagnostic_release();locked=false;}
  if((paired?pair_engine.state==correctiontest::Done:engine.state==linktest::Done)&&peer_received()&&!persisted_peer)save_result();
  portENTER_CRITICAL(&guard);cached_busy=test_busy()||probe_phase;portEXIT_CRITICAL(&guard);
  if(now-published>=200)publish(now);
}
