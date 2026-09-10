#include "survey_service.h"
#include "survey_store.h"
#include "survey_control.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <cstring>

namespace {
portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t sd_mutex=nullptr;
QueueHandle_t commands=nullptr,base_commands=nullptr;
survey::Fix current;
char cached[survey::max_snapshot]={};
survey::Control control;
bool initialized=false;
bool card_ready=false;
uint32_t cached_ms=0;
class CheckedStore:public survey::JournalStore {
 public: CheckedStore():JournalStore("/sdcard/TOPO-RTK/SURVEY"){}
 bool initialize() override {return card_ready && JournalStore::initialize();}
};
struct Request {char json[survey::max_request+1];};
class ReceiverBridge:public survey::Receiver {
  bool apply(const survey::BaseRequest &r) override {return base_commands && xQueueSend(base_commands,&r,0)==pdTRUE;}
} receiver;
void random_hex(char *out){uint8_t bytes[16];esp_fill_random(bytes,16);const char *hex="0123456789abcdef";
  for(int i=0;i<16;++i){out[2*i]=hex[bytes[i]>>4];out[2*i+1]=hex[bytes[i]&15];}out[32]=0;}
void worker(void *) {
  CheckedStore store;survey::Engine engine(store,receiver);
  xSemaphoreTake(sd_mutex,portMAX_DELAY);engine.recover();xSemaphoreGive(sd_mutex);
  auto *request=new Request;
  for(;;){
    survey::Fix fix;portENTER_CRITICAL(&guard);fix=current;portEXIT_CRITICAL(&guard);fix.now=millis();
    xSemaphoreTake(sd_mutex,portMAX_DELAY);
    if(xQueueReceive(commands,request,0)==pdTRUE)engine.command(request->json,fix);
    engine.tick(fix);
    xSemaphoreGive(sd_mutex);
    const std::string state=engine.snapshot(fix);
    portENTER_CRITICAL(&guard);std::memcpy(cached,state.c_str(),state.size()+1);cached_ms=millis();portEXIT_CRITICAL(&guard);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
}
void survey_begin(bool storage_ready) {
  if(initialized)return;
  // Keep larger JSON documents, journal strings and vectors in board PSRAM.
  if(psramFound())heap_caps_malloc_extmem_enable(1024);
  card_ready=storage_ready;
  sd_mutex=xSemaphoreCreateMutex();commands=xQueueCreate(2,sizeof(Request));base_commands=xQueueCreate(1,sizeof(survey::BaseRequest));
  if(!sd_mutex||!commands||!base_commands){Serial.println("SURVEY: task allocation failed");return;}
  survey_revoke_control();
  initialized=xTaskCreate(worker,"survey",16384,nullptr,1,nullptr)==pdPASS;
  Serial.println(initialized?"SURVEY: jobs service started":"SURVEY: task creation failed");
}
void survey_update(const survey::Fix &fix){portENTER_CRITICAL(&guard);current=fix;portEXIT_CRITICAL(&guard);}
bool survey_queue(const char *command){if(!initialized||!command||std::strlen(command)>survey::max_request)return false;
  auto *r=new Request;std::strcpy(r->json,command);const bool ok=xQueueSend(commands,r,0)==pdTRUE;delete r;return ok;}
bool survey_snapshot(char *out,size_t cap){portENTER_CRITICAL(&guard);const size_t n=std::strlen(cached);const bool ok=n&&n<cap&&millis()-cached_ms<2000;
  if(ok)std::memcpy(out,cached,n+1);portEXIT_CRITICAL(&guard);return ok;}
bool survey_take_base(survey::BaseRequest &r){return base_commands && xQueueReceive(base_commands,&r,0)==pdTRUE;}
bool survey_sd_lock(){return !sd_mutex || xSemaphoreTake(sd_mutex,0)==pdTRUE;}
void survey_sd_unlock(){if(sd_mutex)xSemaphoreGive(sd_mutex);}
const char *survey_control_pin(){return control.pin();}
void survey_revoke_control(){char next[7];std::snprintf(next,sizeof(next),"%06lu",static_cast<unsigned long>(esp_random()%1000000));
  portENTER_CRITICAL(&guard);control.reset(next);portEXIT_CRITICAL(&guard);}
int survey_claim(const char *pin,const char *client,char *token,size_t capacity){
  const uint32_t now=millis();char candidate[33];random_hex(candidate);
  portENTER_CRITICAL(&guard);int status=control.claim(pin,client,candidate,now,token,capacity);portEXIT_CRITICAL(&guard);return status;
}
bool survey_authorized(const char *token,bool renew){const uint32_t now=millis();portENTER_CRITICAL(&guard);
  const bool ok=control.authorized(token,now,renew);portEXIT_CRITICAL(&guard);return ok;}
void survey_release(const char *token){portENTER_CRITICAL(&guard);control.release(token,millis());portEXIT_CRITICAL(&guard);}
