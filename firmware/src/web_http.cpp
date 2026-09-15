#include "web_http.h"
#include "web_ui.h"
#include "survey_ui.h"
#include "survey_tools_ui.h"
#include "survey_service.h"
#include "link_diagnostic.h"
#include "link_diagnostic_ui.h"
#include "debug_service.h"
#include "debug_ui.h"
#include "ota_service.h"
#include "ota_ui.h"
#include "link_service.h"
#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <cstring>

namespace {
httpd_handle_t server = nullptr;
constexpr size_t kSettingsCapacity = 1536;
portMUX_TYPE snapshot_mutex = portMUX_INITIALIZER_UNLOCKED;
char snapshot[kWebStatusCapacity] = {};
size_t snapshot_length = 0;
uint32_t snapshot_ms = 0;
uint32_t last_start_attempt = 0;
bool rover_enabled = false;

void headers(httpd_req_t *request) {
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
}

esp_err_t error(httpd_req_t *request, const char *status, const char *body) {
  headers(request);
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

// A request refused before admission keeps its documented status code; anything
// discovered after 202 is reported as that operation's outcome instead.
esp_err_t settings_refusal(httpd_req_t *r, link_operation::Reason reason) {
  switch (reason) {
    case link_operation::Reason::StaleRevision:
      return error(r, "409 Conflict", "{\"error\":\"stale_revision\"}");
    case link_operation::Reason::ConflictingId:
      return error(r, "409 Conflict", "{\"error\":\"conflicting_id\"}");
    case link_operation::Reason::Busy:
      return error(r, "409 Conflict", "{\"error\":\"operation_busy\"}");
    case link_operation::Reason::Unsupported:
      return error(r, "503 Service Unavailable", "{\"error\":\"test_operation_not_available\"}");
    case link_operation::Reason::Cancelled:
      return error(r, "409 Conflict", "{\"error\":\"operation_cancelled\"}");
    case link_operation::Reason::StorageFailure:
      return error(r, "503 Service Unavailable", "{\"error\":\"link_storage_unavailable\"}");
    default:
      return error(r, "400 Bad Request", "{\"error\":\"request_refused\"}");
  }
}

esp_err_t get_status(httpd_req_t *request) {
  char copy[kWebStatusCapacity];
  size_t length;
  uint32_t sampled;
  bool enabled;
  portENTER_CRITICAL(&snapshot_mutex);
  length = snapshot_length;
  sampled = snapshot_ms;
  enabled = rover_enabled;
  std::memcpy(copy, snapshot, length);
  portEXIT_CRITICAL(&snapshot_mutex);
  if (!enabled) return error(request, "409 Conflict", "{\"error\":\"rover_role_required\"}");
  if (!length || millis() - sampled > 1500)
    return error(request, "503 Service Unavailable", "{\"error\":\"status_stale\"}");
  headers(request);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, copy, length);
}

esp_err_t get_page(httpd_req_t *request) {
  headers(request);
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return rover_enabled?httpd_resp_send(request, kWebStatusPage, sizeof(kWebStatusPage) - 1):httpd_resp_send(request,kSurveyPage,sizeof(kSurveyPage)-1);
}

esp_err_t get_survey_page(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,kSurveyPage,sizeof(kSurveyPage)-1);}
esp_err_t get_survey_tools(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"application/javascript; charset=utf-8");return httpd_resp_send(r,kSurveyTools,sizeof(kSurveyTools)-1);}
bool auth(httpd_req_t *r){char value[48]={};return httpd_req_get_hdr_value_str(r,"Authorization",value,sizeof(value))==ESP_OK && std::strncmp(value,"Bearer ",7)==0 && survey_authorized(value+7);}
bool same_origin(httpd_req_t *r){
  if(!httpd_req_get_hdr_value_len(r,"Origin"))return true;
  char origin[128]={},host[96]={},expected[104]={};
  if(httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK || httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK)return false;
  std::snprintf(expected,sizeof(expected),"http://%s",host);return std::strcmp(origin,expected)==0;
}
bool body(httpd_req_t *r,std::string &out){
  char type[48]={};if(!r->content_len||r->content_len>survey::max_request||httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||std::strcmp(type,"application/json")!=0)return false;
  out.resize(r->content_len);size_t got=0;while(got<out.size()){const int n=httpd_req_recv(r,&out[got],out.size()-got);if(n<=0)return false;got+=n;}return out.find('\0')==std::string::npos;
}
esp_err_t get_survey(httpd_req_t *r){
  auto *data=new(std::nothrow) char[survey::max_snapshot];if(!data)return error(r,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
  if(!survey_snapshot(data,survey::max_snapshot)){delete[] data;return error(r,"503 Service Unavailable","{\"error\":\"survey_not_ready\"}");}
  headers(r);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"X-Controller",auth(r)?"true":"false");
  const esp_err_t result=httpd_resp_send(r,data,HTTPD_RESP_USE_STRLEN);delete[] data;return result;
}
esp_err_t get_data(httpd_req_t *r){
  char query[768]={},value[128]={};DynamicJsonDocument d(2048);
  if(httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK)return error(r,"400 Bad Request","{\"error\":\"query_required\"}");
  for(const char *key:{"view","job","point","search","offset","at","deleted"})if(httpd_query_key_value(query,key,value,sizeof(value))==ESP_OK){
    if(!std::strcmp(key,"offset")||!std::strcmp(key,"at")){char *end=nullptr;const auto n=std::strtoul(value,&end,10);if(!value[0]||*end||n>1024)return error(r,"400 Bad Request","{\"error\":\"invalid_cursor\"}");d[key]=n;}
    else if(!std::strcmp(key,"deleted"))d[key]=std::strcmp(value,"true")==0;
    else {
      char decoded[128]={};size_t out=0;
      for(size_t i=0;value[i];++i){char c=value[i];if(c=='%'){auto hex=[](char h){return h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;};if(!value[i+1]||!value[i+2]||hex(value[i+1])<0||hex(value[i+2])<0)return error(r,"400 Bad Request","{\"error\":\"invalid_encoding\"}");c=char(hex(value[i+1])*16+hex(value[i+2]));i+=2;}else if(c=='+')c=' ';if(c<32||c>126)return error(r,"400 Bad Request","{\"error\":\"invalid_text\"}");decoded[out++]=c;}
      d[key]=decoded;
    }
  }
  if(!d.containsKey("offset"))d["offset"]=0;std::string raw;serializeJson(d,raw);
  auto *data=new(std::nothrow) char[survey::max_read];if(!data)return error(r,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
  if(!survey_read(raw.c_str(),data,survey::max_read)){delete[] data;return error(r,"503 Service Unavailable","{\"error\":\"read_busy_retry\"}");}
  headers(r);httpd_resp_set_type(r,"application/json");const auto result=httpd_resp_send(r,data,HTTPD_RESP_USE_STRLEN);delete[] data;return result;
}
esp_err_t claim_control(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  std::string raw;DynamicJsonDocument d(1024);if(!body(r,raw)||deserializeJson(d,raw))return error(r,"400 Bad Request","{\"error\":\"invalid_request\"}");
  char token[33]={};const int status=survey_claim(d["client"]|"",token,sizeof(token));
  if(status!=200)return error(r,"400 Bad Request","{\"error\":\"invalid_takeover_request\"}");
  char response[96];std::snprintf(response,sizeof(response),"{\"token\":\"%s\",\"lease_seconds\":120}",token);return error(r,"200 OK",response);
}
esp_err_t post_command(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  if(!auth(r))return error(r,"401 Unauthorized","{\"error\":\"claim_control_first\"}");
  std::string raw;DynamicJsonDocument d(survey::max_request*2);
  if(!body(r,raw)||deserializeJson(d,raw)||!survey::valid_id(d["id"]|""))return error(r,"400 Bad Request","{\"error\":\"invalid_request\"}");
  if(!survey_queue(raw.c_str()))return error(r,"503 Service Unavailable","{\"error\":\"command_queue_busy\"}");
  return error(r,"202 Accepted","{\"state\":\"queued\"}");
}
esp_err_t release_control(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  char value[48]={};if(httpd_req_get_hdr_value_str(r,"Authorization",value,sizeof(value))==ESP_OK&&std::strncmp(value,"Bearer ",7)==0)survey_release(value+7);
  return error(r,"200 OK","{\"state\":\"released\"}");
}

esp_err_t get_diagnostic_page(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,kDiagnosticPage,sizeof(kDiagnosticPage)-1);}
esp_err_t get_diagnostic(httpd_req_t *r){
  auto *data=new(std::nothrow) char[kDiagnosticCapacity];if(!data)return error(r,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
  if(!diagnostic_snapshot(data,kDiagnosticCapacity)){delete[] data;return error(r,"503 Service Unavailable","{\"error\":\"diagnostic_unavailable\"}");}
  httpd_resp_set_hdr(r,"X-Controller",auth(r)?"true":"false");
  const auto result=error(r,"200 OK",data);delete[] data;return result;
}
esp_err_t post_diagnostic(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  if(!auth(r))return error(r,"401 Unauthorized","{\"error\":\"claim_control_first\"}");
  std::string raw;if(!body(r,raw)||raw.size()>512||!diagnostic_request(raw.c_str()))return error(r,"409 Conflict","{\"error\":\"invalid_settings_or_diagnostic_queue_busy\"}");
  return error(r,"202 Accepted","{\"state\":\"queued\"}");
}
esp_err_t get_debug_page(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"text/html; charset=utf-8");return httpd_resp_send(r,kDebugPage,sizeof(kDebugPage)-1);}
esp_err_t get_update_ui(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"application/javascript; charset=utf-8");return httpd_resp_send(r,kOtaUi,sizeof(kOtaUi)-1);}
esp_err_t get_update(httpd_req_t *r){char out[1536];if(!ota_status(out,sizeof(out)))return error(r,"503 Service Unavailable","{\"error\":\"update_status_unavailable\"}");return error(r,"200 OK",out);}
esp_err_t post_update(httpd_req_t *r){
  if(!same_origin(r)||!auth(r))return error(r,"403 Forbidden","{\"error\":\"current_controller_required\"}");
  char bearer[48]={};httpd_req_get_hdr_value_str(r,"Authorization",bearer,sizeof(bearer));
  std::string raw;if(!body(r,raw)||raw.size()>768||!ota_request(raw.c_str(),bearer+7))return error(r,"409 Conflict","{\"error\":\"update_request_rejected_check_debug_target_and_state\"}");
  return error(r,"202 Accepted","{\"state\":\"queued\"}");
}
esp_err_t reject_upload(httpd_req_t *r,const char *status,const char *message){
  // ESP_OK leaves ESP-IDF draining an unread request body. A failed upload
  // must close its socket so status/control requests can be served promptly.
  httpd_resp_set_hdr(r,"Connection","close");
  error(r,status,message);
  return ESP_FAIL;
}
esp_err_t upload_update(httpd_req_t *r){
  if(!same_origin(r)||!auth(r))return reject_upload(r,"403 Forbidden","{\"error\":\"current_controller_required\"}");
  const uint32_t started=millis();
  char bearer[48]={},type[48]={};httpd_req_get_hdr_value_str(r,"Authorization",bearer,sizeof(bearer));
  if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||std::strcmp(type,"application/octet-stream")||!ota_upload_begin(bearer+7,r->content_len))return reject_upload(r,"409 Conflict","{\"error\":\"review_and_confirm_update_first\"}");
  uint8_t chunk[2048];size_t remaining=r->content_len;
  uint32_t progress=millis();unsigned retries=0;
  while(remaining){
    const uint32_t now=millis();
    if(now-started>=300000||now-progress>=12000){
      ota_upload_abort(now-started>=300000?"Upload exceeded 300-second deadline":"Upload stalled for 12 seconds");
      Serial.printf("OTA HTTP: timeout bytes=%u retries=%u elapsed_ms=%lu\n",unsigned(r->content_len-remaining),retries,static_cast<unsigned long>(now-started));
      return reject_upload(r,"408 Request Timeout","{\"error\":\"upload_timed_out_current_firmware_retained\"}");
    }
    if(!survey_authorized(bearer+7,false)){
      ota_upload_abort("Upload controller lost");
      return reject_upload(r,"403 Forbidden","{\"error\":\"upload_controller_lost\"}");
    }
    const int n=httpd_req_recv(r,reinterpret_cast<char*>(chunk),std::min(remaining,sizeof(chunk)));
    if(n==HTTPD_SOCK_ERR_TIMEOUT){++retries;continue;}
    if(n<=0){
      ota_upload_abort("Upload connection closed or receive error");
      Serial.printf("OTA HTTP: receive=%d bytes=%u retries=%u elapsed_ms=%lu\n",n,unsigned(r->content_len-remaining),retries,static_cast<unsigned long>(millis()-started));
      return reject_upload(r,"408 Request Timeout","{\"error\":\"upload_incomplete_current_firmware_retained\"}");
    }
    if(millis()-started>=300000){ota_upload_abort("Upload exceeded 300-second deadline");return reject_upload(r,"408 Request Timeout","{\"error\":\"upload_timed_out_current_firmware_retained\"}");}
    if(!ota_upload_write(chunk,n))return reject_upload(r,"400 Bad Request","{\"error\":\"package_or_upload_rejected\"}");
    remaining-=n;progress=millis();
  }
  if(!ota_upload_finish())return reject_upload(r,"400 Bad Request","{\"error\":\"image_verification_failed_current_firmware_retained\"}");
  Serial.printf("OTA HTTP: verified bytes=%u retries=%u elapsed_ms=%lu\n",unsigned(r->content_len),retries,static_cast<unsigned long>(millis()-started));
  return error(r,"200 OK","{\"state\":\"restarting\"}");
}
esp_err_t get_debug_nav(httpd_req_t *r){headers(r);httpd_resp_set_type(r,"application/javascript; charset=utf-8");return httpd_resp_send(r,kDebugNav,sizeof(kDebugNav)-1);}
esp_err_t get_debug_status(httpd_req_t *r){char data[512];if(!debug_status(data,sizeof(data)))return error(r,"503 Service Unavailable","{\"error\":\"debug_unavailable\"}");httpd_resp_set_hdr(r,"X-Controller",auth(r)?"true":"false");return error(r,"200 OK",data);}
esp_err_t get_debug_log(httpd_req_t *r){
  if(!auth(r))return error(r,"401 Unauthorized","{\"error\":\"claim_control_first\"}");
  if(!debug_enabled())return error(r,"403 Forbidden","{\"error\":\"enable_debug_on_touchscreen\"}");
  auto *data=new(std::nothrow) char[8192];if(!data)return error(r,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
  if(!debug_logs(data,8192)){delete[] data;return error(r,"503 Service Unavailable","{\"error\":\"debug_capture_unavailable\"}");}
  const auto result=error(r,"200 OK",data);delete[] data;return result;
}
esp_err_t post_debug(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  if(!auth(r))return error(r,"401 Unauthorized","{\"error\":\"claim_control_first\"}");
  std::string raw;StaticJsonDocument<128>d;
  if(!body(r,raw)||raw.size()>128||deserializeJson(d,raw))return error(r,"400 Bad Request","{\"error\":\"invalid_request\"}");
  const char *op=d["op"]|"";
  if(!std::strcmp(op,"disable"))debug_disable();
  else return error(r,"400 Bad Request","{\"error\":\"operation_not_available\"}");
  return error(r,"200 OK","{\"state\":\"applied\"}");
}
esp_err_t get_settings(httpd_req_t *r){
  char *data=new(std::nothrow) char[kSettingsCapacity];if(!data)return error(r,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
  const bool ok=link_service::settings_snapshot(data,kSettingsCapacity);
  if(!ok){delete[] data;return error(r,"503 Service Unavailable","{\"error\":\"settings_unavailable\"}");}
  httpd_resp_set_hdr(r,"X-Controller",auth(r)?"true":"false");
  const auto result=error(r,"200 OK",data);delete[] data;return result;
}
esp_err_t post_settings(httpd_req_t *r){
  if(!same_origin(r))return error(r,"403 Forbidden","{\"error\":\"origin_rejected\"}");
  if(!auth(r))return error(r,"401 Unauthorized","{\"error\":\"claim_control_first\"}");
  std::string raw;StaticJsonDocument<640>d;
  if(!body(r,raw)||raw.size()>512||deserializeJson(d,raw))return error(r,"400 Bad Request","{\"error\":\"invalid_request\"}");
  const char *op=d["op"]|"";
  const char *id=d["id"]|"";
  if(d["confirm"]!=true)return error(r,"400 Bad Request","{\"error\":\"confirm_required\"}");
  link_operation::Reason reason=link_operation::Reason::None;
  if(!std::strcmp(op,"link.cancel")){
    if(!link_service::cancel_operation(id,reason))return settings_refusal(r,reason);
    return error(r,"202 Accepted","{\"state\":\"queued\"}");
  }
  if(std::strcmp(op,"link.select")&&std::strcmp(op,"link.test"))return error(r,"400 Bad Request","{\"error\":\"operation_not_available\"}");
  if(!d["revision"].is<uint32_t>())return error(r,"400 Bad Request","{\"error\":\"revision_required\"}");
  const char *transport=d["transport"]|"";
  link_service::Transport selected;
  if(!std::strcmp(transport,"sik"))selected=link_service::Transport::Radio;
  else if(!std::strcmp(transport,"wifi"))selected=link_service::Transport::WiFi;
  else return error(r,"400 Bad Request","{\"error\":\"transport_required\"}");
  const auto kind=!std::strcmp(op,"link.test")?link_operation::Kind::Test:link_operation::Kind::Select;
  if(!link_service::request_operation(kind,selected,id,d["revision"].as<uint32_t>(),reason))return settings_refusal(r,reason);
  return error(r,"202 Accepted","{\"state\":\"queued\"}");
}
esp_err_t rejected(httpd_req_t *request, httpd_err_code_t) {
  if (request->method != HTTP_GET) {
    httpd_resp_set_hdr(request, "Allow", "GET");
    return error(request, "405 Method Not Allowed", "{\"error\":\"read_only\"}");
  }
  return error(request, "404 Not Found", "{\"error\":\"not_found\"}");
}
}  // namespace

uint32_t web_boot_id() {
  static const uint32_t id = esp_random();
  return id;
}

void publish_web_status(const char *json, size_t length, uint32_t now, bool rover) {
  portENTER_CRITICAL(&snapshot_mutex);
  rover_enabled = rover;
  snapshot_length = json && length < sizeof(snapshot) ? length : 0;
  if (snapshot_length) std::memcpy(snapshot, json, snapshot_length);
  snapshot_ms = now;
  portEXIT_CRITICAL(&snapshot_mutex);

  if (server || (last_start_attempt && now - last_start_attempt < 5000)) return;
  last_start_attempt = now;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 24;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  if (httpd_start(&server, &config) != ESP_OK) { server = nullptr; Serial.println("WEB: start failed"); return; }
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, get_page, nullptr},
      {"/update-ui.js",HTTP_GET,get_update_ui,nullptr},
      {"/api/v1/update",HTTP_GET,get_update,nullptr},
      {"/api/v1/update",HTTP_POST,post_update,nullptr},
      {"/api/v1/update/upload",HTTP_POST,upload_update,nullptr},
      {"/debug",HTTP_GET,get_debug_page,nullptr},
      {"/debug-nav.js",HTTP_GET,get_debug_nav,nullptr},
      {"/api/v1/debug",HTTP_GET,get_debug_status,nullptr},
      {"/api/v1/debug",HTTP_POST,post_debug,nullptr},
      {"/api/v1/debug/log",HTTP_GET,get_debug_log,nullptr},
      {"/ui/v1/", HTTP_GET, get_page, nullptr},
      {"/api/v1/status", HTTP_GET, get_status, nullptr},
      {"/survey",HTTP_GET,get_survey_page,nullptr},
      {"/survey-tools.js",HTTP_GET,get_survey_tools,nullptr},
      {"/api/v1/survey",HTTP_GET,get_survey,nullptr},
      {"/api/v1/data",HTTP_GET,get_data,nullptr},
      {"/api/v1/control",HTTP_POST,claim_control,nullptr},
      {"/api/v1/control/release",HTTP_POST,release_control,nullptr},
      {"/diagnostics",HTTP_GET,get_diagnostic_page,nullptr},
      {"/api/v1/diagnostic",HTTP_GET,get_diagnostic,nullptr},
      {"/api/v1/diagnostic",HTTP_POST,post_diagnostic,nullptr},
      {"/api/v1/command",HTTP_POST,post_command,nullptr},
  {"/api/v1/settings",HTTP_GET,get_settings,nullptr},
  {"/api/v1/settings",HTTP_POST,post_settings,nullptr}};
  bool registered = true;
  for (const auto &route : routes) registered &= httpd_register_uri_handler(server, &route) == ESP_OK;
  registered &= httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, rejected) == ESP_OK;
  registered &= httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, rejected) == ESP_OK;
  if (!registered) { httpd_stop(server); server = nullptr; Serial.println("WEB: routes failed"); return; }
  Serial.println("WEB: status and paired survey controls on port 80; /survey");
}
bool web_service_ready(){return server!=nullptr;}
