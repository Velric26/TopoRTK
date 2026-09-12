#pragma once
#include "correction_transport.h"

namespace correction {
constexpr uint32_t suite_version=1;
struct TestReport {uint32_t checks=0,failed_mask=0;bool passed()const{return checks==22&&!failed_mask;}};
struct TestWorkspace {
  Sender sender;Stream stream;uint8_t source[max_rtcm]{};Packet packets[5]{};
  size_t length=0,count=0;unsigned output=0;bool bad_output=false;
  void make_source(size_t size){
    length=size;for(size_t i=0;i<size;++i)source[i]=uint8_t(i*37+11);
    source[0]=0xd3;source[1]=uint8_t((size-6)>>8);source[2]=uint8_t(size-6);
    const uint32_t c=crc24(source,size-3);source[size-3]=uint8_t(c>>16);source[size-2]=uint8_t(c>>8);source[size-1]=uint8_t(c);
  }
  void encode(uint32_t seq,uint32_t now=0){
    count=0;if(!sender.enqueue(seq,source,length,now)){bad_output=true;return;}
    while(count<5&&sender.next(packets[count],now)){++count;sender.committed();}
  }
  void reset(size_t size=max_rtcm,uint32_t now=0){
    sender.begin(12345,0);stream.select(12345,0);output=0;bad_output=false;make_source(size);encode(1,now);
  }
  void byte(uint8_t value,uint32_t now){if(stream.push(value,now)){++output;if(stream.receiver.size()!=length||std::memcmp(stream.receiver.data(),source,length))bad_output=true;}}
  void packet(size_t index,uint32_t now){for(size_t i=0;i<packet_size;++i)byte(packets[index].bytes[i],now);}
  void all(uint32_t now=0){for(size_t i=0;i<count;++i)packet(i,now+uint32_t(i)*10);}
};
inline TestReport run_selftest(TestWorkspace &w){
  TestReport report;
  const auto check=[&](bool ok){if(!ok||w.bad_output)report.failed_mask|=1u<<report.checks;++report.checks;};
  w.reset();check(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u&&crc24(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcde703u);
  w.reset(8);w.all();check(w.output==1);
  w.reset();w.all();check(w.count==5&&w.output==1);
  w.reset();for(size_t i=w.count;i>0;--i)w.packet(i-1,uint32_t(w.count-i)*10);check(w.output==1);
  w.reset();w.packet(0,0);w.packet(0,10);for(size_t i=1;i<w.count;++i)w.packet(i,20);check(w.output==1&&w.stream.receiver.stats.duplicates==1);
  w.reset();for(size_t i=0;i<w.count;++i)if(i!=2)w.packet(i,10);w.stream.receiver.tick(1010);w.packet(2,1011);check(w.output==0&&w.stream.receiver.stats.expired==1);
  w.reset();w.packets[0].bytes[70]^=1;w.all();w.encode(2,100);w.all(100);check(w.output==1&&w.stream.receiver.stats.wire_errors>0);
  w.reset();w.packet(0,0);for(size_t i=0;i<packet_size;++i)if(i!=64)w.byte(w.packets[1].bytes[i],10);for(size_t i=2;i<w.count;++i)w.packet(i,20);w.encode(2,100);w.all(100);check(w.output==1&&w.stream.discarded_bytes>0);
  w.reset();w.packet(0,0);for(size_t i=0;i<packet_size;++i){if(i==64)w.byte(0x99,10);w.byte(w.packets[1].bytes[i],10);}for(size_t i=2;i<w.count;++i)w.packet(i,20);w.encode(2,100);w.all(100);check(w.output==1&&w.stream.discarded_bytes>0);
  w.reset(16);w.packets[0].bytes[6]=1;seal(w.packets[0]);w.all();w.packets[0].bytes[6]=0;seal(w.packets[0]);w.all(10);check(w.output==1&&w.stream.receiver.stats.wrong_session==1);
  w.reset(16);put32(w.packets[0].bytes+8,54321);seal(w.packets[0]);w.all();put32(w.packets[0].bytes+8,12345);seal(w.packets[0]);w.all(10);check(w.output==1&&w.stream.receiver.stats.wrong_session==1);
  w.reset(16);w.all();w.all(10);check(w.output==1&&w.stream.receiver.stats.replays==1);
  w.reset();w.packet(0,0);w.encode(2,100);w.all(100);check(w.output==1&&w.stream.receiver.stats.preempted==1);
  w.reset(16);w.sender.enqueue(2,w.source,w.length,0);check(!w.sender.next(w.packets[0],age_limit_ms)&&w.sender.expired==1&&!w.sender.pending());
  w.reset();put32(w.packets[0].bytes+24,1499);seal(w.packets[0]);w.packet(0,0);w.packet(1,2);w.encode(2,100);w.all(100);check(w.output==1&&w.stream.receiver.stats.expired==1);
  w.reset(max_rtcm,0xfffffff0u);w.all(0xfffffff0u);w.encode(2,0xfffffff0u);w.packet(0,0xfffffff0u);w.stream.receiver.tick(1000);w.sender.enqueue(3,w.source,w.length,0xfffffff0u);const bool age_wrap=w.sender.next(w.packets[0],10)&&u32(w.packets[0].bytes+24)==26;check(w.output==1&&w.stream.receiver.stats.expired==1&&age_wrap);
  w.reset(16);
  const auto malformed=[&](){seal(w.packets[0]);w.all();w.sender.begin(12345,0);w.encode(1);};
  put16(w.packets[0].bytes+18,1);malformed();put16(w.packets[0].bytes+16,1030);malformed();put16(w.packets[0].bytes+20,0);malformed();w.packets[0].bytes[7]=1;malformed();w.packets[0].bytes[100]=1;malformed();w.packets[0].bytes[4]=2;malformed();w.packets[0].bytes[5]=2;malformed();w.all(100);check(w.output==1&&w.stream.receiver.stats.malformed==7);
  w.reset(16);w.packets[0].bytes[header_size+15]^=1;seal(w.packets[0]);w.all();check(w.output==0&&w.stream.receiver.stats.rtcm_errors==1);
  w.reset(16);w.source[15]^=1;bool rejected=!w.sender.enqueue(2,w.source,16,0)&&!w.sender.enqueue(2,nullptr,16,0)&&!w.sender.enqueue(2,w.source,max_rtcm+1,0);w.source[15]^=1;rejected=rejected&&!w.sender.enqueue(1,w.source,16,0);w.sender.begin(98765,0);w.stream.select(98765,0);w.encode(1);w.all();check(rejected&&w.output==1);
  w.reset(16);for(unsigned i=0;i<300;++i)w.byte(0x7f,0);w.all(10);check(w.output==1&&w.stream.discarded_bytes>0);
  w.reset();w.packet(0,0);w.packet(0,900);for(size_t i=1;i<w.count;++i)w.packet(i,1000);check(w.output==0&&w.stream.receiver.stats.expired==1&&w.stream.receiver.stats.duplicates==1);
  w.reset();w.packet(0,0);put16(w.packets[1].bytes+16,1000);seal(w.packets[1]);w.packet(1,10);for(size_t i=2;i<w.count;++i)w.packet(i,20);check(w.output==0&&w.stream.receiver.stats.malformed==1);
  return report;
}
static_assert(sizeof(TestWorkspace)<5120,"Local self-test memory budget");
} // namespace correction
