#pragma once
#include "survey_engine.h"
namespace survey {
class JournalStore : public Store {
 public:
  explicit JournalStore(const std::string &directory):directory_(directory){}
  bool initialize() override;
  bool read(unsigned sequence,std::string &record,bool &exists) override;
  bool commit(unsigned sequence,const std::string &record) override;
 private:
  std::string directory_;
  unsigned highest_=0;
  std::string path(unsigned sequence,const char *suffix) const;
};
}
