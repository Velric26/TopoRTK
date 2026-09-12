#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include "survey_math.h"

namespace survey {
constexpr size_t max_request=4096,max_snapshot=16384,max_record=8192;
constexpr size_t max_read=65536;
struct Fix {
  char unit='?';uint32_t boot_id=0,reset_reason=0,free_heap=0,min_heap=0,free_psram=0;
  uint32_t now=0,received=0,correction_age=0,reference_age=0;
  uint64_t epoch=0;
  bool rover=true,profile_ok=false,position_valid=false,fixed=false,linked=false,reference_valid=false;
  bool base_apply_pending=false,base_apply_failed=false;
  uint32_t base_revision=0;
  uint32_t base_attempt_revision=0;
  bool base_fixed=false;
  Position base_setting;
  Position position;
  Cartesian reference;
  double hacc=0,vacc=0;
  uint16_t station=0,satellites=0;
  char utc[32]={};
};
struct BaseRequest { bool fixed=false; Position position; uint32_t revision=0; };
class Store {
 public:
  virtual ~Store() = default;
  virtual bool initialize()=0;
  // Missing=false, exists=true. Existing unreadable/corrupt files are faults.
  virtual bool read(unsigned sequence,std::string &record,bool &exists)=0;
  virtual bool commit(unsigned sequence,const std::string &record)=0;
};
class Receiver {
 public: virtual ~Receiver()=default; virtual bool apply(const BaseRequest &request)=0;
};
struct Job {
  std::string id,name,config,last_point;
  unsigned revision=0,points=0;
};
class Engine {
 public:
  Engine(Store &store,Receiver &receiver):store_(store),receiver_(receiver){}
  void recover();
  void command(const char *json,const Fix &fix);
  void tick(const Fix &fix);
  std::string snapshot(const Fix &fix);
  std::string read(const char *request,const Fix &fix);
  bool operation_active()const{return collecting_||!pending_base_id_.empty();}
 private:
  struct Receipt { std::string id,state,message; uint32_t payload_crc=0;unsigned payload_format=1;int line_index=-1; };
  Store &store_; Receiver &receiver_;
  std::vector<Job> jobs_;
  std::vector<Receipt> receipts_;
  struct PointIndex {std::string job,id,code,description,note,line_id;unsigned sequence=0,revision=1;bool deleted=false;};
  std::vector<PointIndex> points_;
  struct Target {std::string job,id,data;unsigned revision=0;};
  std::vector<Target> targets_;
  struct Line {std::string job,id,code,last_point,state="open",reason,units;unsigned revision=0,vertices=0;Cartesian reference;uint16_t station=0;};
  std::vector<Line> lines_;
  Line *find_line(const std::string &job,const std::string &id);
  bool replay_line(JsonObjectConst point,const std::string &job,unsigned revision);
  Target *find_target(const std::string &job,const std::string &id);
  PointIndex *find_point(const std::string &job,const std::string &id);
  bool point_data(const PointIndex &point,JsonDocument &document);
  unsigned sequence_=0;
  bool storage_ok_=false;
  std::string active_,last_id_,last_state_="idle",last_message_="",storage_error_;
  bool collecting_=false;
  std::string occupation_id_,occupation_payload_,occupation_job_,point_id_,point_code_,point_description_;
  std::string occupation_comparison_,occupation_line_;
  uint32_t start_ms_=0,last_sample_ms_=0;
  uint64_t last_epoch_=0;
  unsigned samples_=0;
  Cartesian sum_;
  double worst_h_=0,worst_v_=0;
  uint32_t worst_correction_=0;
  uint16_t min_satellites_=0,station_=0;
  Cartesian base_reference_;
  std::string first_utc_;
  std::string pending_base_id_,pending_base_payload_;
  BaseRequest pending_base_;
  uint32_t base_started_ms_=0;
  Job *active_job();
  Job *find_job(const std::string &id);
  Receipt *receipt(const std::string &id);
  void result(const std::string &id,const char *state,const std::string &message);
  bool commit(JsonDocument &event);
  bool replay(JsonObjectConst event);
  const char *quality(const Fix &fix,JsonObjectConst config);
  void finish_failure(const std::string &message,const Fix &fix);
};
uint32_t crc32(const std::string &text);
bool valid_id(const char *text);
}
