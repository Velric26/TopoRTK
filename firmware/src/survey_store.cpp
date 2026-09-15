#include "survey_store.h"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

namespace survey {
std::string JournalStore::path(unsigned sequence,const char *suffix) const {
  char name[32];std::snprintf(name,sizeof(name),"/%08u.%s",sequence,suffix);return directory_+name;
}
bool JournalStore::initialize() {
  struct stat st;
  if(stat(directory_.c_str(),&st)!=0) {
#ifdef _WIN32
    if(_mkdir(directory_.c_str())!=0)return false;
#else
    if(mkdir(directory_.c_str(),0700)!=0)return false;
#endif
  }
  DIR *dir=opendir(directory_.c_str());if(!dir)return false;highest_=0;
  while(dirent *entry=readdir(dir)){
    unsigned seq=0;char tail[8]={};
    if(std::sscanf(entry->d_name,"%8u.%7s",&seq,tail)==2 && std::strcmp(tail,"rec")==0){
      if(seq==0||seq>1024){closedir(dir);return false;}highest_=std::max(highest_,seq);
    }
  }
  closedir(dir);return true;
}
bool JournalStore::read(unsigned sequence,std::string &record,bool &exists) {
  const std::string filename=path(sequence,"rec");exists=false;
  FILE *file=std::fopen(filename.c_str(),"rb");
  if(!file)return errno==ENOENT && sequence>highest_;
  exists=true;char header[80]={},magic[16]={};size_t length=0;unsigned checksum=0;
  bool ok=std::fgets(header,sizeof(header),file) && std::sscanf(header,"%15s %zu %x",magic,&length,&checksum)==3 &&
          std::strcmp(magic,"TRTKJ001")==0 && length>0 && length<=max_record;
  if(ok){record.resize(length);ok=std::fread(&record[0],1,length,file)==length;
    char tail[9]={};ok=ok && std::fread(tail,1,8,file)==8 && std::strcmp(tail,"\nCOMMIT\n")==0 && std::fgetc(file)==EOF && crc32(record)==checksum;}
  std::fclose(file);return ok;
}
bool JournalStore::commit(unsigned sequence,const std::string &record) {
  if(record.empty()||record.size()>max_record||sequence!=highest_+1)return false;
  const std::string target=path(sequence,"rec"),temp=path(sequence,"tmp");
  if(access(target.c_str(),F_OK)==0)return false; // Never replace committed data.
  FILE *file=std::fopen(temp.c_str(),"wb");if(!file)return false;
  bool ok=std::fprintf(file,"TRTKJ001 %zu %08x\n",record.size(),crc32(record))>0;
  ok=ok && std::fwrite(record.data(),1,record.size(),file)==record.size() && std::fwrite("\nCOMMIT\n",1,8,file)==8;
  ok=std::fflush(file)==0 && ok;
#ifdef _WIN32
  ok=::_commit(fileno(file))==0 && ok;
#else
  ok=::fsync(fileno(file))==0 && ok;
#endif
  ok=std::fclose(file)==0 && ok;
  if(!ok || std::rename(temp.c_str(),target.c_str())!=0)return false;
  highest_=sequence;std::string verify;bool exists=false;
  return read(sequence,verify,exists) && exists && verify==record;
}
}
