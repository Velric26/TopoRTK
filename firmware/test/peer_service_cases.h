uint32_t now=100,base_boot=1111,rover_boot=2222;
bool quality=false;
pair_session::Transport medium=pair_session::Transport::Radio;
pair_session::Engine base_link,rover_link;
uint32_t entropy=980;
uint32_t random_word(){entropy=1664525u*entropy+1013904223u;return entropy;}
void sample(){
  A::time_ms=B::time_ms=now;
  A::peer_update_service(now,base_link.snapshot(now),true);
  B::peer_update_service(now,rover_link.snapshot(now),quality);
}
void transfer(unsigned ticks,bool drop_pair=false,bool drop_notice=false){
  for(unsigned i=0;i<ticks;++i){
    now+=100;base_link.tick(now);rover_link.tick(now);
    correction::Packet p;
    if(base_link.next(p,now)){base_link.committed(p,now);if(!drop_pair)rover_link.receive(p,now);}
    if(rover_link.next(p,now)){rover_link.committed(p,now);if(!drop_pair)base_link.receive(p,now);}
    sample();
    if(A::peer_update_next(p,now)){A::peer_update_committed(p,now);if(!drop_notice)B::peer_update_receive(p,now);}
    if(B::peer_update_next(p,now)){B::peer_update_committed(p,now);if(!drop_notice)A::peer_update_receive(p,now);}
  }
}
std::string peer_label(){char b[80];B::peer_update_label(b,sizeof(b));return b;}
void reboot_base(){
  ++base_boot;base_link.begin(1,false,medium,base_boot,now,random_word);
  A::peer_update_close();A::peer_update_resume(1,rover_boot,false);sample();
}
int main(int argc,char **argv){
  assert(argc==2);medium=std::string(argv[1])=="wifi"?pair_session::Transport::WiFi:pair_session::Transport::Radio;
  base_link.begin(1,false,medium,base_boot,now,random_word);
  rover_link.begin(2,true,medium,rover_boot,now,random_word);sample();
  assert(!A::peer_update_prepare(1,now));
  transfer(50);assert(base_link.snapshot(now).connected&&rover_link.snapshot(now).connected);
  const auto original_session=base_link.snapshot(now).session;
  assert(original_session>999999&&original_session==rover_link.snapshot(now).session);
  assert(A::peer_update_prepare(1,now));
  correction::Packet first,retry;
  assert(A::peer_update_next(first,now)&&A::peer_update_next(retry,now));
  assert(!std::memcmp(&first,&retry,sizeof(first))); // Backpressure does not consume a notice.
  assert(correction::u32(first.bytes+8)==original_session);
  transfer(10,false,true);assert(!A::peer_update_acknowledged());
  transfer(10);assert(A::peer_update_acknowledged());assert(peer_label().find("preparing")!=std::string::npos);
  assert(A::peer_update_phase(update_notice::Kind::Updating,now));
  correction::Packet stale_cancel;assert(A::peer_update_next(stale_cancel,now));
  update_notice::Message cancel;cancel.kind=update_notice::Kind::Cancel;
  cancel.from=1;cancel.to=2;cancel.session=rover_boot;cancel.attempt=1;cancel.sequence=3;
  update_notice::Packet cancelled;assert(update_notice::encode(cancel,cancelled));
  std::memcpy(stale_cancel.bytes+12,cancelled.bytes,40);correction::seal(stale_cancel);
  transfer(10);assert(A::peer_update_acknowledged());assert(peer_label().find("updating")!=std::string::npos);

  // Quality and old-boot connectivity cannot erase an update without return evidence.
  quality=true;transfer(20);assert(peer_label().find("updating")!=std::string::npos);
  quality=false;transfer(50,true,true);
  assert(!base_link.snapshot(now).connected&&!rover_link.snapshot(now).connected);
  assert(!A::peer_update_prepare(2,now));assert(!A::peer_update_next(retry,now));
  transfer(50);assert(base_link.snapshot(now).connected&&rover_link.snapshot(now).connected);
  assert(base_link.snapshot(now).session==original_session&&rover_link.snapshot(now).session==original_session);
  assert(peer_label().find("updating")!=std::string::npos);

  // Rebooted OTA sender must first establish a new live session. The notice
  // receiver keeps its attempt/label across the peer's session replacement.
  reboot_base();quality=true;transfer(20,true,true);
  assert(peer_label().find("updating")!=std::string::npos);
  transfer(50,false,true);
  assert(base_link.snapshot(now).connected&&rover_link.snapshot(now).peer_boot==base_boot);
  assert(base_link.snapshot(now).session!=original_session);
  assert(peer_label().find("updating")!=std::string::npos); // Pair proof alone is insufficient.
  B::peer_update_receive(stale_cancel,now);assert(peer_label().find("updating")!=std::string::npos);
  correction::put32(stale_cancel.bytes+8,0);correction::seal(stale_cancel);
  B::peer_update_receive(stale_cancel,now);assert(peer_label().find("updating")!=std::string::npos);
  quality=false;transfer(20);assert(peer_label().find("checking corrections")!=std::string::npos);

  // Previously received Reconnected cannot authorize a different proven boot.
  reboot_base();transfer(50,false,true);
  quality=true;transfer(10,false,true);assert(peer_label().find("checking corrections")!=std::string::npos);
  quality=false;transfer(20);assert(peer_label().find("checking corrections")!=std::string::npos);
  transfer(50,true,true);quality=true;transfer(2,true,true);
  assert(peer_label().find("checking corrections")!=std::string::npos); // Stale pair proof cannot clear recovery.
  transfer(50);assert(peer_label().empty());

  // Automatic pair heartbeats continue independently from update notices.
  transfer(60);assert(A::peer_update_prepare(2,now));
  correction::Packet delayed_prepare;assert(A::peer_update_next(delayed_prepare,now));
  assert(A::peer_update_phase(update_notice::Kind::Cancel,now));transfer(10);
  assert(A::peer_update_acknowledged()&&peer_label().empty());
  B::peer_update_receive(delayed_prepare,now);assert(peer_label().empty());
  A::peer_update_close();assert(!A::peer_update_prepare(2,now));
  assert(A::peer_update_prepare(3,now));transfer(10);
  assert(A::peer_update_phase(update_notice::Kind::Updating,now));transfer(10);
  transfer(1810,true,true);assert(peer_label().find("overdue")!=std::string::npos);
  transfer(50);assert(peer_label().find("overdue")!=std::string::npos); // No return notice for attempt 3.

  // UART multiplexer consumes a complete valid data envelope, not marker-like
  // bytes inside its payload as an independent control packet.
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
  std::printf("PASS: %s production pair/OTA notices, retry loss, reboot isolation, current-boot quality recovery, cancel tombstones and UART framing\n",argv[1]);
}
