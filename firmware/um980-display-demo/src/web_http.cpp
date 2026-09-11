#include "web_http.h"
#include "web_ui.h"
#include "survey_ui.h"
#include "survey_tools_ui.h"
#include "survey_service.h"
#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <cstring>

namespace {
httpd_handle_t server = nullptr;
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
  char token[33]={};const int status=survey_claim(d["pin"]|"",d["client"]|"",token,sizeof(token));
  if(status!=200)return error(r,status==429?"429 Too Many Requests":status==409?"409 Conflict":status==400?"400 Bad Request":"403 Forbidden",status==409?"{\"error\":\"another_controller_is_active\"}":"{\"error\":\"pairing_failed_or_rate_limited\"}");
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
  config.max_uri_handlers = 10;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  if (httpd_start(&server, &config) != ESP_OK) { server = nullptr; Serial.println("WEB: start failed"); return; }
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, get_page, nullptr},
      {"/ui/v1/", HTTP_GET, get_page, nullptr},
      {"/api/v1/status", HTTP_GET, get_status, nullptr},
      {"/survey",HTTP_GET,get_survey_page,nullptr},
      {"/survey-tools.js",HTTP_GET,get_survey_tools,nullptr},
      {"/api/v1/survey",HTTP_GET,get_survey,nullptr},
      {"/api/v1/data",HTTP_GET,get_data,nullptr},
      {"/api/v1/control",HTTP_POST,claim_control,nullptr},
      {"/api/v1/control/release",HTTP_POST,release_control,nullptr},
      {"/api/v1/command",HTTP_POST,post_command,nullptr}};
  bool registered = true;
  for (const auto &route : routes) registered &= httpd_register_uri_handler(server, &route) == ESP_OK;
  registered &= httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, rejected) == ESP_OK;
  registered &= httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, rejected) == ESP_OK;
  if (!registered) { httpd_stop(server); server = nullptr; Serial.println("WEB: routes failed"); return; }
  Serial.println("WEB: status and paired survey controls on port 80; /survey");
}
