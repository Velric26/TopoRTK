#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
using SemaphoreHandle_t=int*;
SemaphoreHandle_t xSemaphoreCreateMutex(){return new int(1);}
int xSemaphoreTake(SemaphoreHandle_t,int){return 1;}
void xSemaphoreGive(SemaphoreHandle_t){}
uint32_t clock_ms=100;
uint32_t millis(){return clock_ms;}
bool nvs_fail=false;
std::map<std::string,std::vector<uint8_t>> nvs;
class Preferences {
 public:
  bool begin(const char*,bool){return !nvs_fail;}
  void end(){}
  size_t putBytes(const char *key,const void *p,size_t n){if(nvs_fail)return 0;nvs[key]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n);return n;}
  size_t getBytes(const char *key,void *p,size_t n){if(nvs[key].size()!=n)return 0;std::memcpy(p,nvs[key].data(),n);return n;}
  uint32_t getUInt(const char *key,uint32_t fallback){uint32_t n;return getBytes(key,&n,4)==4?n:fallback;}
  size_t putUInt(const char *key,uint32_t n){return putBytes(key,&n,4);}
};
struct ESPDouble {unsigned restarts=0;void restart(){++restarts;}}ESP;
constexpr int ESP_OK=0,ESP_OTA_IMG_PENDING_VERIFY=1;
using esp_ota_handle_t=unsigned;using esp_ota_img_states_t=int;
struct esp_partition_t {uint32_t address,size;};
esp_partition_t first_slot{0x10000,0x640000},second_slot{0x650000,0x640000};
const esp_partition_t *running=&first_slot,*selected=&first_slot;
int image_state=0,begin_error=0,write_error=0,end_error=0,select_error=0;
unsigned begins=0,aborts=0,ends=0,validations=0,rollbacks=0;
std::vector<uint8_t> flash_bytes;
const esp_partition_t *esp_ota_get_running_partition(){return running;}
const esp_partition_t *esp_ota_get_next_update_partition(const void*){return &second_slot;}
int esp_ota_get_state_partition(const esp_partition_t*,int *out){*out=image_state;return 0;}
int esp_ota_begin(const esp_partition_t *p,size_t,esp_ota_handle_t *out){assert(p!=running);++begins;flash_bytes.clear();*out=begin_error?0:1;return begin_error;}
int esp_ota_write(unsigned,const void *p,size_t n){if(write_error)return write_error;flash_bytes.insert(flash_bytes.end(),(const uint8_t*)p,(const uint8_t*)p+n);return 0;}
int esp_ota_abort(unsigned){++aborts;return 0;}
int esp_ota_end(unsigned){++ends;return end_error;}
int esp_ota_set_boot_partition(const esp_partition_t *p){if(select_error)return select_error;selected=p;return 0;}
int esp_ota_mark_app_valid_cancel_rollback(){++validations;image_state=0;return 0;}
int esp_ota_mark_app_invalid_rollback_and_reboot(){++rollbacks;return 0;}
void esp_task_wdt_init(int,bool){}
void esp_task_wdt_add(void*){}
void esp_task_wdt_delete(void*){}
void esp_task_wdt_reset(){}
// Cryptographic primitive is a failure-injection double. Production links
// mbedTLS SHA256; packaging tests independently use Python hashlib.
struct mbedtls_sha256_context{};
bool digest_fail=false;
void mbedtls_sha256_init(mbedtls_sha256_context*){}
void mbedtls_sha256_free(mbedtls_sha256_context*){}
int mbedtls_sha256_starts_ret(mbedtls_sha256_context*,int){return 0;}
int mbedtls_sha256_update_ret(mbedtls_sha256_context*,const uint8_t*,size_t){return 0;}
int mbedtls_sha256_finish_ret(mbedtls_sha256_context*,uint8_t *out){std::memset(out,digest_fail?1:0,32);return 0;}
