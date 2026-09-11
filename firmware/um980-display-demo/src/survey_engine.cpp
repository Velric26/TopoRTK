#include "survey_engine.h"
#include "survey_json.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace survey {
uint32_t crc32(const std::string &s) {
  uint32_t crc=~0U; for (unsigned char c:s) { crc^=c; for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xedb88320U:0); } return ~crc;
}
bool valid_id(const char *s) {
  if (!s || std::strlen(s)!=32) return false;
  for(int i=0;i<32;++i) if (!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
  return true;
}
static bool text(JsonVariantConst v,size_t max,bool allow_empty=false) {
  if (!v.is<const char *>()) return false; const char *s=v.as<const char *>();
  const size_t n=std::strlen(s); if (n>max || (!allow_empty && !n)) return false;
  for (size_t i=0;i<n;++i) if (static_cast<unsigned char>(s[i])<32 || static_cast<unsigned char>(s[i])>126) return false;
  return allow_empty || s[0]!=' ';
}
static bool number(JsonVariantConst v,double low,double high) {
  return v.is<double>() && std::isfinite(v.as<double>()) && v.as<double>()>=low && v.as<double>()<=high;
}
static bool integer(JsonVariantConst v,int low,int high) {
  return number(v,low,high) && std::floor(v.as<double>())==v.as<double>();
}
static bool is(JsonVariantConst v,const char *s) { return v.is<const char *>() && std::strcmp(v.as<const char *>(),s)==0; }
static std::string json(JsonVariantConst v) { return precise_json(v); }
static bool same_reference(JsonObjectConst source,JsonObjectConst config){
  for(const char *key:{"crs","frame","epoch","zone","south","units","vertical"})if(!source.containsKey(key)||json(source[key])!=json(config[key]))return false;
  if(is(config["vertical"],"constant_geoid"))for(const char *key:{"geoid_name","geoid_n","geoid_lat","geoid_lon","geoid_radius"})if(!source.containsKey(key)||json(source[key])!=json(config[key]))return false;
  return true;
}
static bool antenna(JsonObjectConst c,const char *prefix,double &height) {
  const std::string p=prefix;
  if (!text(c[p+"model"],40) || !number(c[p+"height"],0,10) || !number(c[p+"offset"],-1,1) ||
      !number(c[p+"radius"],0,1) || (!is(c[p+"method"],"vertical") && !is(c[p+"method"],"slant"))) return false;
  const double measured=c[p+"height"], radius=c[p+"radius"];
  if (is(c[p+"method"],"slant") && measured<=radius) return false;
  height=(is(c[p+"method"],"slant") ? std::sqrt(measured*measured-radius*radius) : measured)+c[p+"offset"].as<double>();
  return height>=0 && height<=11;
}
static const char *validate_config(JsonObjectConst c) {
  if ((!is(c["crs"],"wgs84_utm")&&!is(c["crs"],"wgs84_geographic")) || !is(c["frame"],"WGS84") ||
      !number(c["epoch"],2000,2100) || c["reference_confirmed"]!=true) return "Confirm the WGS84 reference frame and coordinate epoch.";
  if (!integer(c["zone"],1,60)||!c["south"].is<bool>() || (!is(c["units"],"m")&&!is(c["units"],"ft"))) return "Select zone, hemisphere and units.";
  if (!is(c["vertical"],"ellipsoid")&&!is(c["vertical"],"constant_geoid")) return "Select the height reference.";
  if (is(c["vertical"],"constant_geoid") && (!text(c["geoid_name"],40)||!number(c["geoid_n"],-150,150)||
      !number(c["geoid_lat"],-80,84)||!number(c["geoid_lon"],-180,180)||!number(c["geoid_radius"],1,10000))) return "Enter the local geoid separation, source and validity area.";
  double h;
  if (!antenna(c,"rover_",h)||!antenna(c,"base_",h)||c["antenna_confirmed"]!=true) return "Complete and confirm both antenna reference heights.";
  if (!text(c["base_name"],40)||!integer(c["station"],0,4095)||(!is(c["base_mode"],"known")&&!is(c["base_mode"],"temporary"))) return "Identify the base and its RTCM station ID.";
  if (is(c["base_mode"],"known") && (!number(c["base_lat"],-80,84)||!number(c["base_lon"],-180,180)||
      !number(c["base_ground_h"],-1000,10000)||!number(c["base_tolerance"],.001,1))) return "Enter known base ground coordinates, ellipsoidal height and tolerance.";
  if (is(c["base_mode"],"temporary") && c["temporary_confirmed"]!=true) return "Confirm temporary, unverified base coordinates.";
  if (!integer(c["duration"],1,600)||!integer(c["min_samples"],1,600)||!number(c["h_limit"],.001,1)||
      !number(c["v_limit"],.001,2)||!integer(c["correction_limit"],500,10000)) return "Set valid occupation and quality limits.";
  return nullptr;
}
Job *Engine::find_job(const std::string &id) { for(auto &j:jobs_) if(j.id==id)return &j; return nullptr; }
Job *Engine::active_job(){return find_job(active_);}
Engine::PointIndex *Engine::find_point(const std::string &job,const std::string &id){for(auto &p:points_)if(p.job==job&&p.id==id)return &p;return nullptr;}
Engine::Target *Engine::find_target(const std::string &job,const std::string &id){for(auto &t:targets_)if(t.job==job&&t.id==id)return &t;return nullptr;}
bool Engine::point_data(const PointIndex &p,JsonDocument &d){
  std::string raw;bool exists=false;
  if(!store_.read(p.sequence,raw,exists)||!exists||deserializeJson(d,raw)){storage_ok_=false;storage_error_="Point record unreadable; recover storage.";return false;}
  JsonObject point=d["point"];if(point.isNull())return false;
  JsonObject original=point.createNestedObject("original_metadata");original["code"]=point["code"];original["description"]=point["description"];
  point["code"]=p.code;point["description"]=p.description;point["note"]=p.note;point["deleted"]=p.deleted;point["edit_revision"]=p.revision;point["record"]=p.sequence;return !d.overflowed();
}
Engine::Receipt *Engine::receipt(const std::string &id){for(auto &r:receipts_)if(r.id==id)return &r; return nullptr;}
void Engine::result(const std::string &id,const char *state,const std::string &message){last_id_=id;last_state_=state;last_message_=message;}
bool Engine::replay(JsonObjectConst e) {
  if (e["version"]!=1 || !valid_id(e["id"]|"") || !text(e["op"],30) || !text(e["state"],20)) return false;
  const std::string op=e["op"], id=e["id"], job=e["job"]|"";
  if(op=="job.create") {
    if(find_job(job)||jobs_.size()>=16||!valid_id(job.c_str())||!text(e["name"],48))return false;
    Job j;j.id=job;j.name=e["name"].as<std::string>();jobs_.push_back(j);active_=job;
  } else if(op=="job.open") {if(!find_job(job))return false;active_=job;}
  else if(op=="job.configure") {
    Job *j=find_job(job);if(!j || validate_config(e["config"]))return false;
    j->config=json(e["config"]);j->revision=e["revision"].as<unsigned>();
  } else if(op=="target.create") {
    auto *j=find_job(job);auto t=e["target"].as<JsonObjectConst>();
    if(!j||targets_.size()>=64||!text(t["id"],32)||find_target(job,t["id"]|"")||t["configuration_revision"].as<unsigned>()!=j->revision)return false;
    Target target;target.job=job;target.id=t["id"].as<std::string>();target.data=json(t);target.revision=j->revision;targets_.push_back(target);
  } else if(op=="point.saved") {
    Job *j=find_job(job);if(!j || !e["point"].is<JsonObjectConst>())return false;
    if(find_point(job,e["point"]["id"]|"")||!text(e["point"]["id"],32))return false;
    PointIndex p;p.job=job;p.id=e["point"]["id"].as<std::string>();p.code=e["point"]["code"]|"";p.description=e["point"]["description"]|"";p.sequence=sequence_;points_.push_back(p);
    ++j->points;j->last_point=json(e["point"]);
  } else if(op=="point.edit") {
    auto *p=find_point(job,e["point_id"]|"");if(!p||e["revision"].as<unsigned>()!=p->revision+1)return false;
    p->code=e["code"]|"";p->description=e["description"]|"";p->note=e["note"]|"";p->deleted=e["deleted"]|false;++p->revision;
  } else if(op!="collect.start" && op!="collect.stop" && op!="collect.cancel" && op!="base.apply" && op!="base.result") return false;
  Receipt *r=receipt(id);if(!r) {if(receipts_.size()>=512)return false;receipts_.push_back({});r=&receipts_.back();r->id=id;}
  r->payload_crc=e["payload_crc"].as<uint32_t>();r->payload_format=e["payload_format"]|1U;r->state=e["state"].as<std::string>();r->message=e["message"]|"";
  result(id,r->state.c_str(),r->message);
  return true;
}
void Engine::recover() {
  collecting_=false;pending_base_id_.clear();jobs_.clear();points_.clear();targets_.clear();receipts_.clear();active_.clear();sequence_=0;result("","idle","");
  storage_ok_=store_.initialize();storage_error_=storage_ok_?"":"SD storage unavailable. Check card and restart.";
  if(!storage_ok_)return;
  for(unsigned i=1;i<=1024;++i){
    std::string raw;bool exists=false;
    if(!store_.read(i,raw,exists)){storage_ok_=false;storage_error_="Unreadable journal record; writes disabled.";break;}
    if(!exists)break;
    DynamicJsonDocument d(max_record*2);
    sequence_=i;
    if(deserializeJson(d,raw)||!replay(d.as<JsonObjectConst>())) {storage_ok_=false;storage_error_="Invalid journal record; writes disabled.";break;}
  }
  for(auto &r:receipts_) if(r.state=="collecting"||r.state=="applying") {
    r.state="interrupted";r.message="Instrument restarted. Check setup and start a new operation.";
    if(last_id_==r.id)result(r.id,r.state.c_str(),r.message);
  }
}
bool Engine::commit(JsonDocument &e) {
  if(!storage_ok_||sequence_>=1024||(!receipt(e["id"]|"")&&receipts_.size()>=512)) {storage_error_="Storage unavailable or prototype journal limit reached.";return false;}
  e["version"]=1;e["payload_format"]=2;std::string raw=json(e.as<JsonVariantConst>());
  if(e.overflowed()||raw.size()>max_record||!store_.commit(sequence_+1,raw)) {
    storage_ok_=false;storage_error_="Write not confirmed. Recover storage before retrying the same request.";return false;
  }
  ++sequence_;
  if(!replay(e.as<JsonObjectConst>())) {storage_ok_=false;storage_error_="Journal replay failed; writes disabled.";return false;}
  return true;
}
const char *Engine::quality(const Fix &f,JsonObjectConst c) {
  if(!storage_ok_)return "SD storage is not ready.";
  if(!f.rover||!f.profile_ok)return "Rover profile is not verified.";
  if(!f.position_valid||!f.epoch||f.now-f.received>1500)return "GNSS position/height data is stale or invalid.";
  if(!f.fixed)return "RTK FIXED is required throughout the occupation.";
  if(!f.linked||f.correction_age>c["correction_limit"].as<uint32_t>())return "Corrections are missing or too old.";
  if(!std::isfinite(f.hacc)||!std::isfinite(f.vacc)||f.hacc<0||f.vacc<0||f.hacc>c["h_limit"].as<double>()||f.vacc>c["v_limit"].as<double>())return "Horizontal or vertical uncertainty exceeds the job limit.";
  if(!f.reference_valid||f.reference_age>30000||f.station!=c["station"].as<unsigned>())return "Fresh reference coordinates from the selected base are required.";
  if(is(c["base_mode"],"known")) {
    double height=0;antenna(c,"base_",height);
    Position expected{c["base_lat"],c["base_lon"],c["base_ground_h"].as<double>()+height};
    const Cartesian p=ecef(expected);
    const double d=std::sqrt(std::pow(p.x-f.reference.x,2)+std::pow(p.y-f.reference.y,2)+std::pow(p.z-f.reference.z,2));
    if(d>c["base_tolerance"].as<double>())return "Broadcast base coordinates do not match the configured control.";
  }
  if(is(c["crs"],"wgs84_utm")) {double e,n;if(!utm(f.position,c["zone"],c["south"],e,n))return "Position lies outside the selected UTM zone/hemisphere.";}
  if(is(c["vertical"],"constant_geoid")) {
    Position center{c["geoid_lat"],c["geoid_lon"],f.position.height};
    if(distance(center,f.position)>c["geoid_radius"].as<double>())return "Position lies outside the local geoid model's validity area.";
  }
  return nullptr;
}
void Engine::command(const char *input,const Fix &f) {
  DynamicJsonDocument d(max_request*2);
  if(!input||std::strlen(input)>max_request||deserializeJson(d,input)){result("","rejected","Invalid command JSON.");return;}
  const char *id=d["id"]|"", *op=d["op"]|"";
  if(!valid_id(id)){result("","rejected","A 32-digit lowercase hexadecimal request ID is required.");return;}
  const std::string payload=json(d.as<JsonVariantConst>());const uint32_t hash=crc32(payload);
  if(std::strcmp(op,"storage.recover")==0){if(collecting_||!pending_base_id_.empty()){result(id,"rejected","Finish the active operation before recovery.");return;}recover();result(id,storage_ok_?"completed":"rejected",storage_ok_?"Storage journal recovered.":storage_error_);return;}
  if(Receipt *r=receipt(id)) {uint32_t expected=hash;if(r->payload_format==1){std::string legacy;serializeJson(d,legacy);expected=crc32(legacy);}result(id,r->payload_crc==expected?r->state.c_str():"rejected",r->payload_crc==expected?r->message:"Request ID already used for different data.");return;}
  auto reject=[&](const char *message){result(id,"rejected",message);};
  if(!storage_ok_){reject(storage_error_.c_str());return;}
  if(collecting_ && std::strcmp(op,"collect.cancel")!=0){reject("Finish or cancel the active occupation first.");return;}
  if(!pending_base_id_.empty()){reject("Receiver setup is in progress.");return;}
  DynamicJsonDocument e(max_record*2);
  e["id"]=id;e["op"]=op;e["payload_crc"]=hash;e["state"]="completed";e["message"]="Saved on instrument.";e["utc"]=f.utc;
  const std::string jobid=d["job"]|"";Job *j=find_job(jobid);e["job"]=jobid;
  if(std::strcmp(op,"job.create")==0){
    if(!f.rover||!text(d["name"],48)||jobs_.size()>=16){reject("Rover required; enter a job name (1–48 printable characters), up to 16 jobs.");return;}
    for(const auto &job:jobs_)if(job.name==d["name"].as<std::string>()){reject("A job with that name already exists. Open it instead.");return;}
    e["job"]=id;e["name"]=d["name"];
  } else if(std::strcmp(op,"job.open")==0){if(!f.rover||!j){reject("Job not found on this Rover.");return;}}
  else if(std::strcmp(op,"job.configure")==0){
    if(!f.rover||!j||!d["revision"].is<unsigned>()||d["revision"].as<unsigned>()!=j->revision){reject("Job changed or does not exist. Reload before saving.");return;}
    if(d["confirm"]!=true){reject("Review and confirm the job configuration.");return;}
    if(const char *error=validate_config(d["config"])){reject(error);return;}
    e["config"]=d["config"];e["revision"]=j->revision+1;
  } else if(std::strcmp(op,"target.create")==0){
    if(!f.rover||!j||j->id!=active_||j->config.empty()||!d["revision"].is<unsigned>()||d["revision"].as<unsigned>()!=j->revision){reject("Open the configured current job and reload its revision.");return;}
    if(targets_.size()>=64){reject("The instrument target limit (64) is reached.");return;}
    auto t=d["target"].as<JsonObjectConst>();DynamicJsonDocument c(8192);deserializeJson(c,j->config);
    if(!is(c["crs"],"wgs84_utm")||!same_reference(t["reference"],c.as<JsonObjectConst>())||t["reference_confirmed"]!=true){reject("Confirm target coordinates use exactly this job's UTM reference, epoch, units and height model.");return;}
    const double factor=is(c["units"],"ft")?1/.3048:1;
    if(!text(t["id"],32)||!text(t["source"],120)||(!is(t["kind"],"control")&&!is(t["kind"],"design"))||
       !number(t["easting"],100000*factor,900000*factor)||!number(t["northing"],0,10000000*factor)||!number(t["height"],-1000*factor,10000*factor)){
      reject("Enter a target ID, documented source, control/design type and valid UTM coordinates.");return;}
    if(is(t["kind"],"control")&&t["independent_source"]!=true){reject("Control targets require an explicitly confirmed independent source.");return;}
    if(find_target(jobid,t["id"]|"")){reject("Target ID already exists. Keep its original coordinates; use a new ID for a correction.");return;}
    JsonObject target=e.createNestedObject("target");for(const char *key:{"id","source","kind","easting","northing","height"})target[key]=t[key];
    JsonObject reference=target.createNestedObject("reference");for(const char *key:{"crs","frame","epoch","zone","south","units","vertical"})reference[key]=c[key];
    if(is(c["vertical"],"constant_geoid"))for(const char *key:{"geoid_name","geoid_n","geoid_lat","geoid_lon","geoid_radius"})reference[key]=c[key];
    target["configuration_revision"]=j->revision;target["reference_confirmed"]=true;target["independent_source"]=is(t["kind"],"control");target["created_utc"]=f.utc;
    e["message"]="Reference target saved; this is not a measured point.";
  } else if(std::strcmp(op,"point.edit")==0){
    auto *p=find_point(jobid,d["point_id"]|"");
    if(!f.rover||!p||!d["revision"].is<unsigned>()||d["revision"].as<unsigned>()!=p->revision){reject("Point changed or does not exist; reload it.");return;}
    if(!text(d["code"],32,true)||!text(d["description"],120,true)||!text(d["note"],240,true)||!text(d["reason"],120)||!d["deleted"].is<bool>()){reject("Supply metadata, deletion state and an audit reason.");return;}
    e["point_id"]=p->id;e["revision"]=p->revision+1;e["code"]=d["code"];e["description"]=d["description"];e["note"]=d["note"];e["deleted"]=d["deleted"];e["reason"]=d["reason"];
    JsonObject before=e.createNestedObject("before");before["code"]=p->code;before["description"]=p->description;before["note"]=p->note;before["deleted"]=p->deleted;
  } else if(std::strcmp(op,"collect.start")==0){
    if(sequence_>=1022||receipts_.size()>=511){reject("Journal capacity is too low to start another occupation.");return;}
    j=active_job();if(!j||j->id!=jobid||j->config.empty()||d["revision"].as<unsigned>()!=j->revision){reject("Open and configure the current job first.");return;}
    if(!text(d["point_id"],32)||!text(d["code"],32,true)||!text(d["description"],120,true)){reject("Enter a point ID; check code and description lengths.");return;}
    if(find_point(jobid,d["point_id"]|"")){reject("Point ID already exists, including deleted records. Use a new point ID.");return;}
    DynamicJsonDocument c(8192);deserializeJson(c,j->config);
    if(const char *error=quality(f,c.as<JsonObjectConst>())){reject(error);return;}
    std::string comparison;
    if(d.containsKey("comparison")){
      auto request=d["comparison"].as<JsonObjectConst>();const double factor=is(c["units"],"ft")?1/.3048:1;
      if(!is(c["crs"],"wgs84_utm")||(!is(request["purpose"],"check")&&!is(request["purpose"],"repeat")&&!is(request["purpose"],"stake"))||
         (!is(request["phase"],"start")&&!is(request["phase"],"intermediate")&&!is(request["phase"],"end"))||
         !number(request["h_tolerance"],.001*factor,100*factor)||!number(request["v_tolerance"],.001*factor,100*factor)){
        reject("Check/repeat/stakeout requires UTM coordinates, a phase and explicit positive tolerances in job units.");return;}
      JsonObject comp=e.createNestedObject("comparison");comp["purpose"]=request["purpose"];comp["phase"]=request["phase"];comp["h_tolerance"]=request["h_tolerance"];comp["v_tolerance"]=request["v_tolerance"];comp["units"]=c["units"];
      if(!is(request["purpose"],"repeat")){
        auto *target=find_target(jobid,request["target_id"]|"");DynamicJsonDocument t(4096);
        if(!target||target->revision!=j->revision||deserializeJson(t,target->data)||(is(request["purpose"],"check")&&(!is(t["kind"],"control")||t["independent_source"]!=true))){reject("Choose an independent control target from the current setup revision.");return;}
        comp["reference"]=t.as<JsonVariantConst>();comp["independent_control"]=is(request["purpose"],"check");
      }else{
        auto *p=find_point(jobid,request["reference_point"]|"");DynamicJsonDocument prior(max_record*3);
        if(!p||p->deleted||!point_data(*p,prior)||prior["point"]["configuration_revision"].as<unsigned>()!=j->revision||!prior["point"]["easting"].is<double>()){
          reject("Choose a non-deleted measured point from the current setup revision.");return;}
        JsonObject ref=comp.createNestedObject("reference");for(const char *key:{"id","easting","northing","height","configuration_revision","record","utc_end"})ref[key]=prior["point"][key];ref["kind"]="measured_point";comp["independent_control"]=false;
      }
      comparison=json(comp);
    }
    e["point_id"]=d["point_id"];e["state"]="collecting";e["message"]="Occupation started; point is not saved yet.";
    if(!commit(e)){reject(storage_error_.c_str());return;}
    collecting_=true;occupation_id_=id;occupation_payload_=payload;occupation_job_=jobid;
    occupation_comparison_=comparison;
    point_id_=d["point_id"].as<std::string>();point_code_=d["code"].as<std::string>();point_description_=d["description"].as<std::string>();
    start_ms_=f.now;last_sample_ms_=0;last_epoch_=0;samples_=0;sum_={};worst_h_=worst_v_=0;worst_correction_=0;min_satellites_=65535;
    station_=f.station;base_reference_=f.reference;first_utc_=f.utc;tick(f);return;
  } else if(std::strcmp(op,"collect.cancel")==0){
    if(!collecting_){reject("No active occupation.");return;}finish_failure("Cancelled by operator; no point saved.",f);
    e["message"]="Occupation cancelled; no point saved.";
    if(!commit(e))reject(storage_error_.c_str());return;
  } else if(std::strcmp(op,"base.apply")==0){
    if(sequence_>=1023){reject("Journal capacity is too low to start receiver setup.");return;}
    if(f.rover||f.base_apply_pending||d["confirm"]!=true){reject("Open the idle Base instrument and confirm receiver setup there.");return;}
    if(!is(d["mode"],"fixed")&&!is(d["mode"],"temporary")){reject("Choose fixed control or temporary survey-in.");return;}
    if(is(d["mode"],"fixed")&&(!number(d["latitude"],-80,84)||!number(d["longitude"],-180,180)||!number(d["reference_h"],-1000,10000))){reject("Enter valid receiver-reference coordinates and ellipsoidal height.");return;}
    pending_base_.fixed=is(d["mode"],"fixed");pending_base_.position={d["latitude"]|0.0,d["longitude"]|0.0,d["reference_h"]|0.0};pending_base_.revision=f.base_revision+1;
    e["settings"]=d.as<JsonVariantConst>();e["state"]="applying";e["message"]="Saved intent; waiting for receiver verification.";
    if(!commit(e)){reject(storage_error_.c_str());return;}
    if(!receiver_.apply(pending_base_)){e["op"]="base.result";e["state"]="rejected";e["message"]="Receiver command queue is busy; request was not applied.";if(!commit(e))reject(storage_error_.c_str());return;}
    pending_base_id_=id;pending_base_payload_=payload;base_started_ms_=f.now;return;
  } else {reject("Unknown command.");return;}
  if(!commit(e))reject(storage_error_.c_str());
}
void Engine::finish_failure(const std::string &message,const Fix &f) {
  collecting_=false;DynamicJsonDocument e(4096);e["id"]=occupation_id_;e["payload_crc"]=crc32(occupation_payload_);
  e["op"]="collect.stop";e["job"]=occupation_job_;e["state"]="rejected";e["message"]=message;e["utc"]=f.utc;
  if(!commit(e))result(occupation_id_,"rejected",message+" Storage confirmation failed; no saved point was acknowledged.");
}
void Engine::tick(const Fix &f) {
  if(!pending_base_id_.empty()) {
    bool matches=!pending_base_.fixed;
    if(f.reference_valid && f.reference_age<15000){const Cartesian e=ecef(pending_base_.position);matches=std::sqrt(std::pow(e.x-f.reference.x,2)+std::pow(e.y-f.reference.y,2)+std::pow(e.z-f.reference.z,2))<.02;}
    const bool success=f.base_revision==pending_base_.revision && f.profile_ok && !f.base_apply_pending && (pending_base_.fixed?matches:true);
    const bool fail=(f.base_apply_failed&&f.base_attempt_revision==pending_base_.revision)||f.rover||f.now-base_started_ms_>45000;
    if(success||fail){DynamicJsonDocument e(4096);e["id"]=pending_base_id_;e["payload_crc"]=crc32(pending_base_payload_);e["op"]="base.result";
      e["state"]=success?"completed":"rejected";e["message"]=success?(pending_base_.fixed?"Base profile and broadcast reference verified.":"Temporary base profile verified; coordinates remain unverified."):"Base setup not verified. Check receiver and broadcast reference before use.";
      if(!commit(e))result(pending_base_id_,"rejected",storage_error_);pending_base_id_.clear();}
  }
  if(!collecting_)return;Job *j=find_job(occupation_job_);if(!j){finish_failure("Job unavailable.",f);return;}
  DynamicJsonDocument c(8192);deserializeJson(c,j->config);JsonObjectConst config=c.as<JsonObjectConst>();
  if(const char *error=quality(f,config)){finish_failure(error,f);return;}
  const double base_motion=std::sqrt(std::pow(f.reference.x-base_reference_.x,2)+std::pow(f.reference.y-base_reference_.y,2)+std::pow(f.reference.z-base_reference_.z,2));
  if(f.station!=station_||base_motion>.002){finish_failure("Base identity or coordinates changed during occupation.",f);return;}
  if(last_sample_ms_ && f.now-last_sample_ms_>1500){finish_failure("GNSS epoch gap; restart the occupation.",f);return;}
  if(f.epoch==last_epoch_)return;
  if(last_epoch_ && f.epoch<last_epoch_){finish_failure("GNSS epoch moved backwards.",f);return;}
  const Cartesian p=ecef(f.position);sum_.x+=p.x;sum_.y+=p.y;sum_.z+=p.z;++samples_;last_epoch_=f.epoch;last_sample_ms_=f.now;
  worst_h_=std::max(worst_h_,f.hacc);worst_v_=std::max(worst_v_,f.vacc);worst_correction_=std::max(worst_correction_,f.correction_age);min_satellites_=std::min(min_satellites_,f.satellites);
  if(f.now-start_ms_<config["duration"].as<unsigned>()*1000 || samples_<config["min_samples"].as<unsigned>())return;
  const Position average=geodetic({sum_.x/samples_,sum_.y/samples_,sum_.z/samples_});double antenna_height=0;antenna(config,"rover_",antenna_height);
  const double ground_h=average.height-antenna_height, factor=is(config["units"],"ft")?1/.3048:1;
  double east=0,north=0;if(is(config["crs"],"wgs84_utm")&&!utm(average,config["zone"],config["south"],east,north)){finish_failure("Averaged position is outside the selected projection.",f);return;}
  DynamicJsonDocument e(max_record*2);e["id"]=occupation_id_;e["payload_crc"]=crc32(occupation_payload_);e["op"]="point.saved";e["job"]=j->id;
  e["state"]="completed";e["message"]="Point committed and read back from Rover SD.";
  JsonObject point=e.createNestedObject("point");point["id"]=point_id_;point["code"]=point_code_;point["description"]=point_description_;
  point["latitude"]=average.latitude;point["longitude"]=average.longitude;point["receiver_ellipsoidal_h_m"]=average.height;point["ground_ellipsoidal_h_m"]=ground_h;
  point["height"]=factor*(ground_h-(is(config["vertical"],"constant_geoid")?config["geoid_n"].as<double>():0));
  if(is(config["crs"],"wgs84_utm")){point["easting"]=east*factor;point["northing"]=north*factor;}
  point["samples"]=samples_;point["duration_ms"]=f.now-start_ms_;point["h_uncertainty_max_m"]=worst_h_;point["v_uncertainty_max_m"]=worst_v_;
  point["correction_age_max_ms"]=worst_correction_;point["satellites_min"]=min_satellites_;point["fix"]="RTK FIXED";point["utc_start"]=first_utc_;point["utc_end"]=f.utc;
  point["configuration_revision"]=j->revision;point["configuration"]=config;point["base_reference_x_m"]=base_reference_.x;point["base_reference_y_m"]=base_reference_.y;point["base_reference_z_m"]=base_reference_.z;
  point["base_control_verified"]=is(config["base_mode"],"known");
  if(!occupation_comparison_.empty()){
    DynamicJsonDocument comparison(8192);deserializeJson(comparison,occupation_comparison_);auto ref=comparison["reference"];
    const double de=east*factor-ref["easting"].as<double>(),dn=north*factor-ref["northing"].as<double>(),dh=point["height"].as<double>()-ref["height"].as<double>();
    comparison["delta_e"]=de;comparison["delta_n"]=dn;comparison["delta_h"]=dh;comparison["horizontal"]=std::hypot(de,dn);
    comparison["horizontal_pass"]=std::hypot(de,dn)<=comparison["h_tolerance"].as<double>();comparison["vertical_pass"]=std::abs(dh)<=comparison["v_tolerance"].as<double>();
    comparison["pass"]=comparison["horizontal_pass"]==true&&comparison["vertical_pass"]==true;
    point["kind"]=comparison["purpose"];point["comparison"]=comparison.as<JsonVariantConst>();
    e["message"]=comparison["pass"]==true?"Observation saved; comparison is within the stated tolerances.":"Observation saved; comparison is OUTSIDE tolerance. Investigate before continuing survey work.";
  }
  collecting_=false;if(!commit(e))result(occupation_id_,"rejected",storage_error_);
}
std::string Engine::read(const char *request,const Fix &) {
  DynamicJsonDocument q(4096),out(max_read*2);
  if(!request||deserializeJson(q,request))return "{\"error\":\"invalid_query\"}";
  if(q.containsKey("at")&&q["at"].as<unsigned>()!=sequence_)return "{\"error\":\"job_changed_reload\"}";
  const std::string job=q["job"]|"";auto *j=find_job(job);
  if(!j)return "{\"error\":\"job_not_found\"}";
  out["at"]=sequence_;out["job"]=job;out["revision"]=j->revision;
  if(is(q["view"],"targets")){
    if(!integer(q["offset"],0,64))return "{\"error\":\"invalid_offset\"}";
    unsigned total=0,added=0,offset=q["offset"];JsonArray rows=out.createNestedArray("targets");
    for(const auto &t:targets_){if(t.job!=job)continue;if(total++<offset||added>=25)continue;DynamicJsonDocument target(4096);if(deserializeJson(target,t.data))return "{\"error\":\"target_read_failed\"}";rows.add(target.as<JsonVariantConst>());++added;}
    out["total"]=total;if(offset+added<total)out["next"]=offset+added;
  }else if(is(q["view"],"backup")){
    if(!integer(q["offset"],0,1024))return "{\"error\":\"invalid_offset\"}";
    out["format"]="TopoRTK job journal";out["version"]=1;out["name"]=j->name;
    JsonArray records=out.createNestedArray("records");unsigned cursor=q["offset"],scanned=0;
    // Bound both matching records and skipped records to keep SD reads responsive.
    while(cursor<sequence_&&records.size()<3&&scanned++<32){
      std::string raw;bool exists=false;++cursor;DynamicJsonDocument event(max_record*2);
      if(!store_.read(cursor,raw,exists)||!exists||deserializeJson(event,raw)){
        storage_ok_=false;storage_error_="Backup record unreadable; recover storage.";return "{\"error\":\"backup_read_failed\"}";
      }
      if(event["job"].as<std::string>()!=job)continue;
      JsonObject row=records.createNestedObject();row["sequence"]=cursor;row["crc32"]=crc32(raw);row["json"]=raw;
    }
    if(cursor<sequence_)out["next"]=cursor;
  }else if(is(q["view"],"point")){
    auto *p=find_point(job,q["point"]|"");DynamicJsonDocument d(max_record*3);
    if(!p)return "{\"error\":\"point_not_found\"}";
    if(!point_data(*p,d))return "{\"error\":\"point_read_failed\"}";
    out["point"]=d["point"];
  }else if(is(q["view"],"points")){
    if(!integer(q["offset"],0,1024))return "{\"error\":\"invalid_offset\"}";
    const unsigned offset=q["offset"],limit=25;unsigned total=0,added=0;
    const std::string search=q["search"]|"";const bool deleted=q["deleted"]|false;
    JsonArray rows=out.createNestedArray("points");
    for(const auto &p:points_){
      if(p.job!=job||(!deleted&&p.deleted)||(!search.empty()&&p.id.find(search)==std::string::npos&&p.code.find(search)==std::string::npos))continue;
      if(total++<offset||added>=limit)continue;
      DynamicJsonDocument d(max_record*3);if(!point_data(p,d))return "{\"error\":\"point_read_failed\"}";
      JsonObject point=d["point"],row=rows.createNestedObject();
      for(const char *key:{"id","code","description","deleted","edit_revision","record","easting","northing","height","latitude","longitude","utc_end","configuration_revision","base_control_verified","kind","line_id","line_action"})if(point.containsKey(key))row[key]=point[key];
      row["units"]=point["configuration"]["units"];row["crs"]=point["configuration"]["crs"];row["vertical"]=point["configuration"]["vertical"];++added;
    }
    out["total"]=total;if(offset+added<total)out["next"]=offset+added;
  }else return "{\"error\":\"unknown_view\"}";
  if(out.overflowed())return "{\"error\":\"read_capacity\"}";std::string result=json(out.as<JsonVariantConst>());return result.size()<max_read?result:"{\"error\":\"read_capacity\"}";
}
std::string Engine::snapshot(const Fix &f) {
  DynamicJsonDocument d(max_snapshot*2);d["version"]=1;d["role"]=f.rover?"ROVER":"BASE";d["storage_ready"]=storage_ok_;d["storage_error"]=storage_error_;
  char unit[2]={f.unit,0};d["unit"]=unit;d["boot_id"]=f.boot_id;d["uptime_ms"]=f.now;d["reset_reason"]=f.reset_reason;
  d["free_heap"]=f.free_heap;d["min_heap"]=f.min_heap;d["free_psram"]=f.free_psram;
  d["records_used"]=sequence_;d["record_limit"]=1024;d["active_job"]=active_;
  d["targets_used"]=targets_.size();d["target_limit"]=64;
  JsonArray jobs=d.createNestedArray("jobs");for(const auto &j:jobs_){JsonObject row=jobs.createNestedObject();row["id"]=j.id;row["name"]=j.name;row["revision"]=j.revision;row["points"]=j.points;}
  Job *j=active_job();if(j){DynamicJsonDocument config(8192),point(max_record*2);if(!j->config.empty()&&!deserializeJson(config,j->config))d["config"]=config.as<JsonVariantConst>();
    if(!j->last_point.empty()&&!deserializeJson(point,j->last_point))d["last_point"]=point.as<JsonVariantConst>();
    d["block_reason"]=j->config.empty()?"Configure this job before collection.":quality(f,config.as<JsonObjectConst>());
    if(!j->config.empty()&&!quality(f,config.as<JsonObjectConst>())){
      double e,n,h=0;antenna(config.as<JsonObjectConst>(),"rover_",h);
      if(is(config["crs"],"wgs84_utm")&&utm(f.position,config["zone"],config["south"],e,n)){JsonObject rover=d.createNestedObject("rover_position");const double scale=is(config["units"],"ft")?1/.3048:1;
        rover["easting"]=e*scale;rover["northing"]=n*scale;rover["height"]=(f.position.height-h-(is(config["vertical"],"constant_geoid")?config["geoid_n"].as<double>():0))*scale;rover["epoch"]=f.epoch;rover["age_ms"]=f.now-f.received;rover["configuration_revision"]=j->revision;}
    }
  }else d["block_reason"]="Create or open a job.";
  JsonObject op=d.createNestedObject("operation");op["id"]=last_id_;op["state"]=last_state_;op["message"]=last_message_;
  JsonObject collect=d.createNestedObject("collection");collect["active"]=collecting_;collect["samples"]=samples_;collect["elapsed_ms"]=collecting_?f.now-start_ms_:0;collect["point_id"]=point_id_;
  JsonObject gnss=d.createNestedObject("gnss");gnss["fixed"]=f.fixed;gnss["position_valid"]=f.position_valid;gnss["profile_verified"]=f.profile_ok;
  gnss["station"]=f.reference_valid?f.station:-1;gnss["base_reference_available"]=f.reference_valid&&f.reference_age<30000;gnss["base_revision"]=f.base_revision;
  gnss["base_mode"]=f.base_fixed?"fixed":"temporary";
  if(f.base_fixed){gnss["saved_base_latitude"]=f.base_setting.latitude;gnss["saved_base_longitude"]=f.base_setting.longitude;gnss["saved_base_reference_h"]=f.base_setting.height;}
  gnss["correction_age_ms"]=f.correction_age;gnss["h_uncertainty_m"]=f.hacc;gnss["v_uncertainty_m"]=f.vacc;
  if(f.reference_valid){const Position p=geodetic(f.reference);gnss["base_latitude"]=p.latitude;gnss["base_longitude"]=p.longitude;gnss["base_reference_h"]=p.height;}
  std::string output=json(d.as<JsonVariantConst>());if(d.overflowed()||output.size()>=max_snapshot)return "{\"error\":\"snapshot_capacity\"}";return output;
}
}
