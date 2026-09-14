uint32_t now=100;
bool quality=false;
void transfer(unsigned ticks,bool drop=false){
  for(unsigned i=0;i<ticks;++i){now+=100;A::time_ms=B::time_ms=now;
    A::peer_update_service(now,false,7777777,{},true);B::peer_update_service(now,true,7777777,{},quality);
    correction::Packet p;
    if(A::peer_update_next(p,now)){A::peer_update_committed(p,now);if(!drop)B::peer_update_receive(p,now);}
    if(B::peer_update_next(p,now)){B::peer_update_committed(p,now);if(!drop)A::peer_update_receive(p,now);}
  }
}
std::string peer_label(){char b[80];B::peer_update_label(b,sizeof(b));return b;}
int main(){
  A::peer_update_service(now,false,7777777,{},true);B::peer_update_service(now,true,7777777,{},false);
  assert(!A::peer_update_prepare(1,now)); // A first unconfirmed hello cannot admit a notice.
  transfer(30);assert(A::peer_update_prepare(1,now));transfer(10);
  assert(A::peer_update_acknowledged());assert(peer_label()=="Base preparing update");
  assert(A::peer_update_phase(update_notice::Kind::Updating,now));transfer(10);
  assert(A::peer_update_acknowledged());assert(peer_label()=="Base updating - corrections paused");
  // Fresh old-boot hello/quality cannot erase Updating without recovery evidence.
  quality=true;transfer(20);assert(peer_label().find("updating")!=std::string::npos);quality=false;
  A::nonce=3333;A::peer_update_close();A::peer_update_resume(1,B::nonce,false);transfer(60);
  assert(peer_label()=="Peer reconnected - checking corrections");
  quality=true;transfer(2);assert(peer_label().empty());
  // The reboot recovery sender continues HELLOs alongside its return notices.
  transfer(60);assert(A::peer_update_prepare(2,now));transfer(10);assert(A::peer_update_acknowledged());
  assert(A::peer_update_phase(update_notice::Kind::Cancel,now));transfer(10);assert(peer_label().empty());

  // UART multiplexer must consume a whole valid data envelope. It never exposes
  // an apparent update marker inside that envelope's payload as a second frame.
  peer_wire::Stream stream;correction::Packet outer{},out;
  std::memcpy(outer.bytes,"RTM1",4);outer.bytes[4]=1;outer.bytes[5]=1;
  std::memcpy(outer.bytes+32,"RTM1",4);outer.bytes[36]=1;outer.bytes[37]=3;correction::seal(outer);
  unsigned delivered=0;for(auto b:outer.bytes)if(stream.byte(b,out)){++delivered;assert(out.bytes[5]==1);}
  assert(delivered==1);
  for(unsigned i=0;i<19;++i)assert(!stream.byte(0x77,out));
  correction::Packet control{};std::memcpy(control.bytes,"RTM1",4);control.bytes[4]=1;control.bytes[5]=3;correction::seal(control);
  for(auto b:control.bytes)if(stream.byte(b,out)){++delivered;assert(peer_wire::control(out));}
  assert(delivered==2);
  control.bytes[70]=1;correction::seal(control);assert(!peer_wire::control(control));
  std::puts("PASS: production two-peer HELLO/echo, preparation/update ACK, fresh reboot recovery, quality gate, recovery heartbeat continuity and UART envelope demultiplexing");
}
