"""Exercise the production upload HTTP handler with explicit socket/clock/OTA doubles."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'.pio/update-tests';out.mkdir(parents=True,exist_ok=True)
source=(root/'src/web_http.cpp').read_text(encoding='utf-8')
handler=source[source.index('esp_err_t reject_upload('):source.index('esp_err_t get_debug_nav(')]
stubs=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,HTTPD_SOCK_ERR_TIMEOUT=-3;
struct httpd_req_t {size_t content_len;};
uint32_t clock_ms=100;uint32_t millis(){return clock_ms;}
struct {template<class...T>void printf(const char*,T...){}}Serial;
bool origin=true,owner=true,begin_ok=true,write_ok=true,finish_ok=true,lose_owner=false;
std::string type="application/octet-stream",response,status,connection,abort_reason;
size_t writes=0,reads=0;unsigned begins=0,finishes=0;
struct Step {int bytes;uint32_t elapsed;};std::vector<Step>steps;
bool same_origin(httpd_req_t*){return origin;}bool auth(httpd_req_t*){return owner;}
bool survey_authorized(const char*,bool){return owner;}
int httpd_resp_set_hdr(httpd_req_t*,const char *key,const char *value){if(!strcmp(key,"Connection"))connection=value;return 0;}
int error(httpd_req_t*,const char *s,const char *b){status=s;response=b;return 0;}
int httpd_req_get_hdr_value_str(httpd_req_t*,const char *key,char *out,size_t n){std::string value=!strcmp(key,"Content-Type")?type:"Bearer "+std::string(32,'a');if(value.size()>=n)return -1;strcpy(out,value.c_str());return 0;}
bool ota_upload_begin(const char*,size_t){++begins;return begin_ok;}
bool ota_upload_write(const uint8_t*,size_t n){if(!write_ok)return false;writes+=n;return true;}
bool ota_upload_finish(){++finishes;return finish_ok;}
void ota_upload_abort(const char *reason){abort_reason=reason;}
int httpd_req_recv(httpd_req_t*,char *out,size_t capacity){assert(reads<steps.size());const auto step=steps[reads++];clock_ms+=step.elapsed;if(lose_owner)owner=false;assert(step.bytes<=int(capacity));if(step.bytes>0)memset(out,0,step.bytes);return step.bytes;}
'''
cases=r'''
int main(int argc,char **argv){assert(argc==2);std::string test=argv[1];httpd_req_t request{100};
 if(test=="transient"){steps={{40,1},{-3,2000},{-3,2000},{60,1}};assert(upload_update(&request)==ESP_OK);assert(writes==100&&finishes==1&&abort_reason.empty());}
 else if(test=="stall"){for(int i=0;i<6;++i)steps.push_back({-3,2000});assert(upload_update(&request)==ESP_FAIL);assert(reads==6&&abort_reason=="Upload stalled for 12 seconds");}
 else if(test=="disconnect"||test=="receive_error"){steps={{40,1},{test=="disconnect"?0:-1,1}};assert(upload_update(&request)==ESP_FAIL);assert(writes==40&&finishes==0);}
 else if(test=="deadline"){request.content_len=1000;for(int i=0;i<150;++i)steps.push_back({1,2000});assert(upload_update(&request)==ESP_FAIL);assert(writes==149&&finishes==0&&abort_reason=="Upload exceeded 300-second deadline");}
 else if(test=="takeover"){lose_owner=true;steps={{-3,2000}};assert(upload_update(&request)==ESP_FAIL);assert(abort_reason=="Upload controller lost"&&reads==1);}
 else if(test=="origin"){origin=false;assert(upload_update(&request)==ESP_FAIL);assert(begins==0&&reads==0);}
 else if(test=="content_type"){type="text/plain";assert(upload_update(&request)==ESP_FAIL);assert(begins==0&&reads==0);}
 else if(test=="admission"){begin_ok=false;assert(upload_update(&request)==ESP_FAIL);assert(reads==0);}
 else if(test=="write"){write_ok=false;steps={{40,1}};assert(upload_update(&request)==ESP_FAIL);assert(finishes==0);}
 else if(test=="finish"){finish_ok=false;steps={{100,1}};assert(upload_update(&request)==ESP_FAIL);assert(finishes==1);}
 else if(test=="wrap"){clock_ms=UINT32_MAX-1000;steps={{-3,2000},{100,1}};assert(upload_update(&request)==ESP_OK);assert(finishes==1);}
 else return 2;
 if(test!="transient"&&test!="wrap")assert(connection=="close");
 std::cout<<"PASS: production HTTP upload "<<test<<"\n";
}
'''
generated=out/'ota_http.cpp';generated.write_text(stubs+handler+cases,encoding='utf-8')
exe=out/'ota_http.exe'
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(generated),'-o',str(exe)],check=True)
for case in ['transient','stall','disconnect','receive_error','deadline','takeover','origin','content_type','admission','write','finish','wrap']:
 subprocess.run([str(exe),case],check=True)
