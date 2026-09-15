bool debug_on=true,controller=true,busy=false,admission=true,peer_ack=true;
unsigned reset_count=0,release_count=0;std::vector<update_notice::Kind> phases;
const char *test_token="12345678901234567890123456789012";
bool debug_enabled(){return debug_on;}
bool survey_authorized(const char *token,bool){return controller&&!std::strcmp(token,test_token);}
bool survey_diagnostic_acquire(){return admission;}
void survey_diagnostic_release(){++release_count;}
bool diagnostic_busy(){return busy;}
void ota_reset_corrections(){++reset_count;}
uint32_t correction_radio_session(){return 7777777;}
bool correction_radio_restore(uint32_t,bool){return true;}
uint32_t web_boot_id(){return 1234;}
bool peer_update_prepare(uint32_t,uint32_t){return true;}
bool peer_update_phase(update_notice::Kind kind,uint32_t){phases.push_back(kind);return true;}
bool peer_update_acknowledged(){return peer_ack;}
void peer_update_close(){}
uint32_t peer_update_target(){return 5678;}
void peer_update_resume(uint32_t,uint32_t,bool){}
void peer_update_label(char *out,size_t n){std::snprintf(out,n,"%s","");}
void step(uint32_t ms=1,bool healthy=true){clock_ms+=ms;ota_service(clock_ms,false,false,healthy);}
std::string status(){char b[1536];assert(ota_status(b,sizeof(b)));return b;}
int main(int argc,char **argv){
  assert(argc==2);const std::string test=argv[1];ota_boot_begin();
  ota_package::Header h;auto *b=h.bytes;std::memcpy(b,"TPK1",4);b[4]=1;b[5]=1;b[6]=1;
  correction::put32(b+8,512);correction::put32(b+12,200);std::strcpy((char*)b+48,TOPORTK_FIRMWARE_VERSION);
  correction::put32(b+124,correction::crc32(b,124));
  std::vector<uint8_t> image(512,0xE9);ota_package::identity(h,image.data()+200);
  std::vector<uint8_t> package(b,b+128);package.insert(package.end(),image.begin(),image.end());
  auto prepare_request=[&](){const char *digits="0123456789abcdef";std::string hex;for(uint8_t v:h.bytes){hex+=digits[v>>4];hex+=digits[v&15];}return "{\"op\":\"prepare\",\"header\":\""+hex+"\"}";};
  assert(!ota_upload_begin(test_token,640));assert(begins==0);
  if(test=="wrong_target"){h.bytes[5]=2;correction::put32(h.bytes+124,correction::crc32(h.bytes,124));assert(!ota_request(prepare_request().c_str(),test_token));assert(begins==0);return 0;}
  if(test=="debug_off"){debug_on=false;assert(!ota_request(prepare_request().c_str(),test_token));return 0;}
  assert(ota_request(prepare_request().c_str(),test_token));assert(ota_locked()&&!ota_paused());
  assert(!ota_request(prepare_request().c_str(),test_token));
  if(test=="stage_deadline"){step(60001);assert(!ota_locked()&&begins==0&&status().find("Update stage expired")!=std::string::npos);return 0;}
  if(test=="stale_prepare_clock"){
    const uint32_t old_now=clock_ms-1;ota_service(old_now,false,false,true);
    assert(ota_locked()&&status().find("notifying")!=std::string::npos);return 0;
  }
  if(test=="busy")admission=false;
  step();
  if(test=="busy"){step();assert(!ota_locked()&&begins==0&&status().find("Finish collection")!=std::string::npos);return 0;}
  if(test=="no_ack")peer_ack=false;
  step(3501);
  if(test=="cancel"){assert(ota_request("{\"op\":\"cancel\"}",test_token));step();assert(!ota_locked()&&begins==0&&release_count==1);return 0;}
  if(test=="takeover"){controller=false;step();assert(!ota_locked()&&begins==0);return 0;}
  if(test=="stale_start_clock"){
    const uint32_t old_now=clock_ms;++clock_ms;
    assert(ota_request("{\"op\":\"start\",\"confirm\":true}",test_token));
    ota_service(old_now,false,false,true);
    assert(ota_locked()&&ota_paused()&&status().find("pausing")!=std::string::npos);return 0;
  }
  if(test=="no_ack")assert(!ota_request("{\"op\":\"start\",\"confirm\":true}",test_token));
  assert(ota_request("{\"op\":\"start\",\"confirm\":true,\"allow_unconfirmed\":true}",test_token));step();assert(ota_paused());step(3501);
  if(test=="begin_failure")begin_error=1;
  bool opened=ota_upload_begin(test_token,640);
  if(test=="begin_failure"){assert(!opened);step();assert(!ota_locked()&&selected==running);return 0;}
  assert(opened);assert(!ota_request("{\"op\":\"cancel\"}",test_token));
  debug_on=false;step();assert(ota_paused()); // Debug disabled mid-upload cannot abort flash.
  if(test=="disconnect"){assert(ota_upload_write(package.data(),180));ota_upload_abort("Disconnected");step();assert(!ota_locked()&&aborts==1&&selected==running);return 0;}
  if(test=="timeout")clock_ms+=300001;
  if(test=="write_failure")write_error=1;
  if(test=="changed_header")package[48]^=1;
  if(test=="changed_identity")package[128+216]=2;
  if(test=="owner_lost")controller=false;
  bool wrote=true;for(size_t at=0;at<package.size()&&wrote;at+=37)wrote=ota_upload_write(package.data()+at,std::min(size_t(37),package.size()-at));
  if(test=="timeout"||test=="write_failure"||test=="changed_header"||test=="changed_identity"||test=="owner_lost"){
    assert(!wrote);step();assert(!ota_locked()&&selected==running&&aborts==1);return 0;
  }
  assert(wrote&&flash_bytes==image);
  if(test=="digest_failure")digest_fail=true;
  if(test=="end_failure")end_error=1;
  if(test=="nvs_failure")nvs_fail=true;
  if(test=="select_failure")select_error=1;
  const bool complete=ota_upload_finish();
  if(test=="digest_failure"||test=="end_failure"||test=="nvs_failure"||test=="select_failure"){
    assert(!complete);step();assert(!ota_locked()&&selected==running);return 0;
  }
  assert(complete&&selected==&second_slot);
  if(test=="stale_success_clock"){ota_service(clock_ms-1,false,false,true);assert(ESP.restarts==0);step(1500);assert(ESP.restarts==1);return 0;}
  step(1600);assert(ESP.restarts==1);
  if(test=="boot_health"||test=="boot_failure"){
    running=selected;image_state=ESP_OTA_IMG_PENDING_VERIFY;ota_boot_begin();
    step(10001,test=="boot_health");
    if(test=="boot_health")assert(validations==1&&rollbacks==0);
    else {assert(validations==0);step(20001,false);assert(rollbacks==1);}
  }
  return 0;
}
