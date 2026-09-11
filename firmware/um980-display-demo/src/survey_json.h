#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

namespace survey {
// ArduinoJson 6's default float formatter keeps about ten significant digits.
// Keep 15 significant digits: sub-micrometre precision over supported UTM ranges.
// Avoid 17-digit decimal tails that drift through ArduinoJson 6 parsing.
inline void append_precise_json(JsonVariantConst value,std::string &out){
  if(value.is<JsonObjectConst>()){
    out+='{' ;bool first=true;
    for(JsonPairConst pair:value.as<JsonObjectConst>()){
      if(!first)out+=',';first=false;out+='"';
      for(const unsigned char *p=reinterpret_cast<const unsigned char *>(pair.key().c_str());*p;++p){
        if(*p=='"'||*p=='\\'){out+='\\';out+=char(*p);}
        else if(*p<32){char escaped[7];std::snprintf(escaped,sizeof(escaped),"\\u%04x",unsigned(*p));out+=escaped;}
        else out+=char(*p);
      }
      out+="\":";append_precise_json(pair.value(),out);
    }out+='}';
  }else if(value.is<JsonArrayConst>()){
    out+='[';bool first=true;for(auto item:value.as<JsonArrayConst>()){if(!first)out+=',';first=false;append_precise_json(item,out);}out+=']';
  }else if(value.is<double>()&&!value.is<int64_t>()&&!value.is<uint64_t>()){
    const double n=value.as<double>();if(!std::isfinite(n)){out+="null";return;}char buffer[32];std::snprintf(buffer,sizeof(buffer),"%.15g",n);out+=buffer;
  }else serializeJson(value,out);
}
inline std::string precise_json(JsonVariantConst value){std::string out;append_precise_json(value,out);return out;}
}
