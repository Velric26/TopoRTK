#include "ota_service.h"
#include "peer_update.h"
#include "debug_service.h"
#include "survey_service.h"
#include "link_diagnostic.h"
#include "web_http.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <freertos/semphr.h>
#include <atomic>

extern "C" bool verifyRollbackLater(){return true;}
namespace {
struct ImageIdentity {char marker[16];uint8_t unit,hardware,reserved[2];char version[32];uint8_t padding[12];};
const ImageIdentity identity __attribute__((used))={"TOPORTK_FW_V1",TOPORTK_UNIT_ID,1,{},TOPORTK_FIRMWARE_VERSION,{}};
static_assert(sizeof(identity)==64,"Package identity layout");
enum class State {Idle,Requested,Preparing,Review,StartRequested,Signalling,Ready,Uploading,Success,Failed};
SemaphoreHandle_t gate=nullptr;
std::atomic<bool> locked{false},paused{false};
State state=State::Idle;uint32_t changed=0,received=0,attempt=0,upload_started=0;
bool acquired=false,notice_started=false,ack=false,cancel=false,unconfirmed=false;
char owner[33]={},error_text[96]={},boot_text[64]="USB / normal boot";
ota_package::Header header;ota_package::IdentityCheck identity_check;
esp_ota_handle_t handle=0;const esp_partition_t *partition=nullptr;
mbedtls_sha256_context sha;
bool boot_pending=false,boot_done=false,nvs_ready=false;uint32_t boot_at=0;
struct Receipt {uint32_t magic,attempt,target,route,partition;uint8_t rover,reserved[3];char version[32];uint32_t crc;};
Receipt receipt{};bool have_receipt=false;
const char *name(){switch(state){case State::Requested:return "requested";case State::Preparing:return "notifying";case State::Review:return "review";case State::StartRequested:return "starting";case State::Signalling:return "pausing";case State::Ready:return "ready";case State::Uploading:return "uploading";case State::Success:return "restarting";case State::Failed:return "failed";default:return "idle";}}
bool access(const char *token){return token&&std::strlen(token)==32&&!std::strcmp(owner,token)&&survey_authorized(token,false);}
void fail(const char *reason,uint32_t now){
  if(handle){esp_ota_abort(handle);handle=0;}mbedtls_sha256_free(&sha);
  std::snprintf(error_text,sizeof(error_text),"%s",reason);state=State::Failed;changed=now;
  // Main loop releases admission and resumes forwarding at a safe boundary.
}
bool save_receipt(bool rover){
  receipt={};receipt.magic=0x31544f54;receipt.attempt=attempt;receipt.target=peer_update_target();
  receipt.route=correction_radio_session();receipt.partition=partition->address;receipt.rover=rover;
  std::memcpy(receipt.version,header.bytes+48,32);
  receipt.crc=correction::crc32(reinterpret_cast<const uint8_t*>(&receipt),offsetof(Receipt,crc));
  Preferences p;Receipt check{};if(!p.begin("topoota",false))return false;
  const bool ok=p.putBytes("receipt",&receipt,sizeof(receipt))==sizeof(receipt)&&p.getBytes("receipt",&check,sizeof(check))==sizeof(check)&&!std::memcmp(&check,&receipt,sizeof(check));p.end();return ok;
}
bool role=false;
}
void ota_boot_begin(){
  gate=xSemaphoreCreateMutex();boot_at=millis();mbedtls_sha256_init(&sha);
  Preferences p;nvs_ready=p.begin("topoota",false);
  if(nvs_ready){have_receipt=p.getBytes("receipt",&receipt,sizeof(receipt))==sizeof(receipt)&&receipt.magic==0x31544f54&&receipt.crc==correction::crc32(reinterpret_cast<const uint8_t*>(&receipt),offsetof(Receipt,crc));p.end();}
  const auto *running=esp_ota_get_running_partition();esp_ota_img_states_t status;
  boot_pending=running&&esp_ota_get_state_partition(running,&status)==ESP_OK&&status==ESP_OTA_IMG_PENDING_VERIFY;
  if(boot_pending){std::strcpy(boot_text,"Checking new firmware");esp_task_wdt_init(30,true);esp_task_wdt_add(nullptr);}
  else if(have_receipt&&running&&receipt.partition!=running->address)std::strcpy(boot_text,"Previous firmware restored (rollback)");
}
bool ota_locked(){return locked.load();}
bool ota_paused(){return paused.load();}
bool ota_request(const char *json,const char *token){
  if(!gate||!token||std::strlen(token)!=32||!survey_authorized(token,false)||xSemaphoreTake(gate,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  StaticJsonDocument<768>d;bool ok=false;
  if(!deserializeJson(d,json)){
    const char *op=d["op"]|"";
    if(!std::strcmp(op,"prepare")&&(state==State::Idle||state==State::Failed)&&!locked&&debug_enabled()&&!boot_pending&&nvs_ready){
      const char *hex=d["header"]|"";ota_package::Header candidate;
      auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
      bool valid=std::strlen(hex)==256;
      if(valid)for(unsigned i=0;i<128;++i){int a=digit(hex[2*i]),b=digit(hex[2*i+1]);if(a<0||b<0){valid=false;break;}candidate.bytes[i]=uint8_t(a*16+b);}
      partition=esp_ota_get_next_update_partition(nullptr);
      if(valid&&partition&&partition!=esp_ota_get_running_partition()&&ota_package::valid(candidate,identity.unit,partition->size)){
        header=candidate;std::strcpy(owner,token);state=State::Requested;locked=true;cancel=false;unconfirmed=false;received=0;error_text[0]=0;changed=millis();ok=true;
      }
    }else if(!std::strcmp(op,"start")&&state==State::Review&&access(token)&&debug_enabled()&&d["confirm"]==true&&(ack||d["allow_unconfirmed"]==true)){
      unconfirmed=d["allow_unconfirmed"]==true;state=State::StartRequested;changed=millis();ok=true;
    }else if(!std::strcmp(op,"cancel")&&locked&&state!=State::Uploading&&state!=State::Success&&access(token)){cancel=true;ok=true;}
  }
  xSemaphoreGive(gate);return ok;
}
bool ota_status(char *out,size_t capacity){
  if(!gate||xSemaphoreTake(gate,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  StaticJsonDocument<1024>d;d["state"]=name();d["available"]=nvs_ready&&!boot_pending;
  d["version"]=1;d["unit"]=identity.unit;d["firmware"]=identity.version;d["boot_id"]=web_boot_id();d["boot"]=boot_text;
  d["locked"]=locked.load();d["paused"]=paused.load();d["peer_acknowledged"]=ack;d["unconfirmed_override"]=unconfirmed;
  d["received"]=received;d["total"]=ota_package::size(header)+128;d["attempt"]=attempt;d["error"]=error_text;
  d["target_version"]=reinterpret_cast<const char*>(header.bytes+48);
  char peer[80];peer_update_label(peer,sizeof(peer));d["peer_status"]=peer;
  const bool ok=!d.overflowed()&&measureJson(d)<capacity;if(ok)serializeJson(d,out,capacity);xSemaphoreGive(gate);return ok;
}
void ota_service(uint32_t now,bool rover,bool profile_busy,bool healthy){
  if(boot_pending)esp_task_wdt_reset();
  if(!boot_done&&now-boot_at>=10000){
    if(boot_pending){
      if(healthy&&nvs_ready&&gate&&have_receipt&&receipt.partition==esp_ota_get_running_partition()->address&&!std::strcmp(receipt.version,identity.version)){
        if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK){boot_pending=false;esp_task_wdt_delete(nullptr);std::strcpy(boot_text,"New firmware verified");boot_done=true;}
      }else if(now-boot_at>=30000){std::strcpy(boot_text,"Startup failed - rolling back");esp_ota_mark_app_invalid_rollback_and_reboot();}
    }else boot_done=true;
    if(boot_done&&have_receipt){
      // Only an update receipt may restore the saved live SiK session. It is an
      // explicit continuation of the update, not automatic radio provisioning.
      if(receipt.route&&receipt.rover==rover)correction_radio_restore(receipt.route,rover);
      peer_update_resume(receipt.attempt,receipt.target,rover);
    }
  }
  if(!gate||xSemaphoreTake(gate,0)!=pdTRUE)return;
  // HTTP may have advanced changed after the caller sampled now. Resample
  // while holding the same mutex, otherwise unsigned elapsed time can wrap
  // and expire a request that is only a millisecond old.
  now=millis();
  role=rover;
  if(state==State::Success){if(now-changed>=1500)ESP.restart();xSemaphoreGive(gate);return;}
  if(locked&&state!=State::Uploading&&state!=State::Failed&&
      (cancel||!survey_authorized(owner,false)||(!paused&&!debug_enabled())||now-changed>60000)){
    const char *reason=cancel?"Update cancelled":!survey_authorized(owner,false)?"Update controller lost":(!paused&&!debug_enabled())?"Debug disabled before update":"Update stage expired";
    fail(reason,now);
  }
  if(state==State::Failed&&locked){
    if(notice_started)peer_update_phase(update_notice::Kind::Cancel,now);
    notice_started=false;if(paused)ota_reset_corrections();paused=false;
    if(acquired)survey_diagnostic_release();acquired=false;locked=false;
  }
  if(state==State::Requested){
    if(profile_busy||diagnostic_busy()||!survey_diagnostic_acquire())fail("Finish collection, receiver setup and diagnostics first",now);
    else {
      acquired=true;Preferences p;bool saved=p.begin("topoota",false);uint32_t next=saved?p.getUInt("attempt",0)+1:0;
      saved=saved&&next&&p.putUInt("attempt",next)==4&&p.getUInt("attempt",0)==next;p.end();
      if(!saved)fail("Cannot save update attempt",now);
      else {attempt=next;peer_update_close();notice_started=peer_update_prepare(attempt,now);ack=false;state=State::Preparing;changed=now;}
    }
  }else if(state==State::Preparing){
    ack=notice_started&&peer_update_acknowledged();if(ack||now-changed>=3500){state=State::Review;changed=now;}
  }else if(state==State::StartRequested){
    ota_reset_corrections();paused=true;
    notice_started=notice_started&&peer_update_phase(update_notice::Kind::Updating,now);ack=false;state=State::Signalling;changed=now;
  }else if(state==State::Signalling){
    ack=notice_started&&peer_update_acknowledged();
    if(now-changed>=3500){if(!ack&&!unconfirmed)fail("Updating notice unconfirmed; retry and explicitly allow unconfirmed peer if needed",now);else {state=State::Ready;changed=now;}}
  }
  xSemaphoreGive(gate);
}
bool ota_upload_begin(const char *token,size_t length){
  if(!gate||xSemaphoreTake(gate,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  bool ok=state==State::Ready&&access(token)&&length==128+ota_package::size(header);
  if(ok){state=State::Uploading;received=0;upload_started=changed=millis();identity_check.begin(header);mbedtls_sha256_init(&sha);
    ok=mbedtls_sha256_starts_ret(&sha,0)==0&&esp_ota_begin(partition,ota_package::size(header),&handle)==ESP_OK;
    if(!ok)fail("Cannot open inactive OTA slot",millis());}
  xSemaphoreGive(gate);return ok;
}
bool ota_upload_write(const uint8_t *bytes,size_t size){
  if(!gate||xSemaphoreTake(gate,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  bool ok=state==State::Uploading&&millis()-upload_started<120000&&survey_authorized(owner,true)&&size<=128+ota_package::size(header)-received;
  if(ok&&received<128){const auto n=std::min(size,size_t(128-received));ok=!std::memcmp(bytes,header.bytes+received,n);bytes+=n;size-=n;received+=n;}
  if(ok&&size){const uint32_t position=received-128;
    ok=identity_check.feed(bytes,size,position)&&mbedtls_sha256_update_ret(&sha,bytes,size)==0&&esp_ota_write(handle,bytes,size)==ESP_OK;received+=size;
  }
  if(!ok)fail("Upload interrupted, expired, or image does not match reviewed package",millis());
  changed=millis();xSemaphoreGive(gate);return ok;
}
bool ota_upload_finish(){
  if(!gate||xSemaphoreTake(gate,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  uint8_t digest[32];bool ok=state==State::Uploading&&millis()-upload_started<120000&&received==128+ota_package::size(header)&&identity_check.complete()&&access(owner)&&
    mbedtls_sha256_finish_ret(&sha,digest)==0&&!std::memcmp(digest,header.bytes+16,32);
  if(ok){const auto result=esp_ota_end(handle);handle=0;ok=result==ESP_OK;}
  if(ok)ok=save_receipt(role)&&esp_ota_set_boot_partition(partition)==ESP_OK;
  if(ok){mbedtls_sha256_free(&sha);state=State::Success;changed=millis();}
  else fail("Image verification or boot selection failed; current firmware retained",millis());
  xSemaphoreGive(gate);return ok;
}
void ota_upload_abort(const char *reason){if(gate&&xSemaphoreTake(gate,pdMS_TO_TICKS(100))==pdTRUE){if(state==State::Uploading)fail(reason,millis());xSemaphoreGive(gate);}}
