std::string published_web_status;
bool published_web_rover = false;
uint32_t web_boot_id() { return 0x1234abcd; }
void publish_web_status(const char *json, size_t length, uint32_t, bool rover) {
  published_web_status.assign(json, length);
  published_web_rover = rover;
}

// The receiver service owns UART1; the host suite observes the same port object
// and feeds it the bytes a UM980 would send, so every receiver fact arrives
// through the production reader rather than a test-only setter.
extern HardwareSerial gnss;

GnssSnapshot receiver() { return gnss_service::snapshot(); }

void feed_raw(const void *bytes, size_t length) {
  gnss.feed(static_cast<const uint8_t *>(bytes), length);
  gnss_service::service_input(host_now);
}

void feed_bytes(const std::vector<uint8_t> &bytes) {
  feed_raw(bytes.data(), bytes.size());
}

void feed_line(const std::string &line) {
  const std::string framed = line + "\r\n";
  feed_raw(framed.data(), framed.size());
}

// NMEA sentences checksum everything after the leading '$'.
std::string nmea_sentence(const std::string &body) {
  uint8_t checksum = 0;
  for (size_t index = 1; index < body.size(); ++index) checksum ^= uint8_t(body[index]);
  char suffix[8];
  std::snprintf(suffix, sizeof(suffix), "*%02X", static_cast<unsigned>(checksum));
  return body + suffix;
}

// The receiver's own GGA for 19.4326 N / 99.1332 W: empty UTC and altitude
// fields, exactly as the UM980 emits them.
std::string gga_fixture(int quality, int satellites, double hdop) {
  char body[160];
  std::snprintf(body, sizeof(body),
                "$GPGGA,,1925.95600,N,09907.99200,W,%d,%d,%.1f,,M,,,", quality,
                satellites, hdop);
  return nmea_sentence(body);
}

// The receiver's own RMC for 2026-09-08 18:24:00 UTC with a valid navigation status.
std::string rmc_fixture() {
  return nmea_sentence("$GPRMC,182400.00,A,1925.95600,N,09907.99200,W,0.0,0.0,080926,,,A");
}

// A whole RTCM 3 1006 reference frame for the given station and position.
std::vector<uint8_t> reference_frame(uint16_t station, const survey::Position &position) {
  const survey::Cartesian ecef = survey::ecef(position);
  std::vector<uint8_t> frame(27, 0);
  frame[0] = 0xd3;
  frame[1] = 0x00;
  frame[2] = 21;
  auto put = [&](int start, uint64_t value, int count) {
    for (int bit = 0; bit < count; ++bit)
      if ((value >> (count - 1 - bit)) & 1)
        frame[3 + (start + bit) / 8] |= static_cast<uint8_t>(1 << (7 - (start + bit) % 8));
  };
  auto signed38 = [](double meters) {
    return static_cast<uint64_t>(static_cast<int64_t>(meters * 10000.0)) & ((uint64_t(1) << 38) - 1);
  };
  put(0, 1006, 12);
  put(12, station, 12);
  put(34, signed38(ecef.x), 38);
  put(74, signed38(ecef.y), 38);
  put(114, signed38(ecef.z), 38);
  const uint32_t crc = crc24q(frame.data(), 24);
  frame[24] = static_cast<uint8_t>(crc >> 16);
  frame[25] = static_cast<uint8_t>(crc >> 8);
  frame[26] = static_cast<uint8_t>(crc);
  return frame;
}

void reply(const std::string &body, bool corrupt = false) {
  uint8_t checksum = 0;
  for (char c : body) checksum ^= c;
  char line[256];
  std::snprintf(line, sizeof(line), "%s*%02X", body.c_str(), corrupt ? checksum ^ 1 : checksum);
  feed_line(line);
}

void complete_profile() {
  unsigned count = 0;
  while (receiver().profile_running) {
    assert(++count <= 12);
    assert(!receiver().profile_applied);
    gnss_service::service_profile(host_now);
    const std::string command = gnss_service::pending_profile_command();
    reply("$command," + command + ",response: OK");
    if (command == "MODE") reply(std::string("#MODE,TEST;MODE ") + (is_base() ? "BASE" : "ROVER"));
    host_now += 110;
    gnss_service::service_profile(host_now);
  }
  assert(receiver().profile_applied && !receiver().profile_failed);
}

void contact(int x, int y, int points=1, int elapsed=30) {
  Wire.x=x; Wire.y=y; Wire.points=points;
  host_now += elapsed;
  service_swipe_navigation();
}
void release() { contact(Wire.x, Wire.y, 0, 80); }
void tap(int x,int y) { contact(x,y); release(); }

// Navigation must leave the same visible page as a clean render, without
// stale rows/buttons from its predecessor or repainting an unchanged page.
void assert_clean_page(const char *preview_path = nullptr) {
  if (preview_path) display->save(preview_path);
  const auto navigated_pixels = display->pixels;
  draw_static_screen();
  draw_dynamic_screen();
  if (display->pixels != navigated_pixels) {
    std::fprintf(stderr, "RENDER MISMATCH at %s (page %u, detail %u)\n",
                 preview_path ? preview_path : "(none)",
                 static_cast<unsigned>(current_page), ui_detail_page());
    std::fflush(stderr);
  }
  assert(display->pixels == navigated_pixels);
  const auto draws = display->draws;
  draw_dynamic_screen();
  assert(display->draws == draws);
}

int main() {
  // The receiver service owns UART1 and installs its observations here; every
  // receiver fact below arrives through its own reader and sequencers. The
  // correction service is handed the selected role it cannot read itself.
  receiver_service_begin();
  correction_service_begin();
  // Same production observer: metadata/replayed epochs cannot hide an outage.
  auto msm=[](unsigned type,uint32_t epoch){std::vector<uint8_t> b(32);b[0]=0xd3;b[2]=26;b[3]=type>>4;b[4]=(type&15)<<4;b[5]=7;
    b[6]=epoch>>22;b[7]=epoch>>14;b[8]=epoch>>6;b[9]=epoch<<2;const auto crc=crc24q(b.data(),29);b[29]=crc>>16;b[30]=crc>>8;b[31]=crc;return b;};
  correction::Health health;auto data=msm(1074,1000),ref=msm(1006,0);
  assert(health.observe(data.data(),data.size(),0));assert(health.arrival_age(500)==500);
  assert(!health.observe(ref.data(),ref.size(),2500));assert(!health.observe(data.data(),data.size(),2600));
  assert(health.effective_age(3001,true,3001,0,7)==3001);
  data=msm(1074,2000);assert(health.observe(data.data(),data.size(),3100));assert(health.effective_age(3100,true,3100,500,7)==500);
  assert(health.effective_age(3600,true,3100,500,7)==1000);assert(health.effective_age(3100,true,3100,500,8)==UINT32_MAX);
  assert(health.effective_age(4601,true,3100,0,7)==UINT32_MAX);assert(health.effective_age(3100,false,3100,0,7)==UINT32_MAX);
  data.back()^=1;assert(!health.observe(data.data(),data.size(),4000));health.reset();assert(health.arrival_age(4000)==UINT32_MAX);
  data=msm(1074,604799000);assert(health.observe(data.data(),data.size(),0xfffffff0u));data=msm(1074,0);assert(health.observe(data.data(),data.size(),10));assert(health.arrival_age(20)==10);
  data=msm(1074,604799000);assert(!health.observe(data.data(),data.size(),30));
  // Actual firmware queue and COM2 writer: no partial admission under pressure.
  {
    const auto before=host_now;gnss_service::set_config(device_config);
    gnss_service::apply_unit_profile();        // the correction gate needs a verified profile
    complete_profile();
    wifi_last_peer_ms=host_now;
    correction_service::reset();debug_enable_local(true);gnss.binary_output.clear();gnss.tx_free=0;
    auto reference=msm(1006,0),observation=msm(1074,1000);
    assert(!correction_service::admit(observation.data(),observation.size(),host_now,host_now));
    assert(correction_service::admit(reference.data(),reference.size(),host_now,host_now));correction_service::service_output(host_now);assert(gnss.binary_output.empty());
    assert(correction_service::snapshot().station==7);   // the reference latches the station
    host_now+=1500;correction_service::service_output(host_now);assert(!correction_service::snapshot().queued&&gnss.binary_output.empty());
    assert(correction_service::admit(reference.data(),reference.size(),host_now,host_now));
    assert(correction_service::admit(observation.data(),observation.size(),host_now,host_now));gnss.tx_free=31;
    correction_service::service_output(host_now);assert(gnss.binary_output.empty());gnss.tx_free=2048;
    correction_service::service_output(host_now);assert(gnss.binary_output==reference);correction_service::service_output(host_now);
    auto expected=reference;expected.insert(expected.end(),observation.begin(),observation.end());assert(gnss.binary_output==expected);
    // Counter meanings: whole frames written, their bytes, the age-limit drop,
    // and TX-ring refusals that were retried rather than written partially.
    const auto counters=correction_service::snapshot();
    assert(counters.forwarded==2&&counters.forwarded_bytes==expected.size()&&counters.expired==1&&counters.waiting==2&&!counters.faults&&!counters.queued);
    assert(correction_service::health().arrival_age(host_now)==0);
    // An observation from another station cannot join the latched reference.
    auto mismatched=observation;mismatched[5]=8;
    const auto mismatch_crc=crc24q(mismatched.data(),29);mismatched[29]=mismatch_crc>>16;mismatched[30]=mismatch_crc>>8;mismatched[31]=mismatch_crc;
    assert(!correction_service::admit(mismatched.data(),mismatched.size(),host_now,host_now));
    // A SiK selection excludes Wi-Fi copies, even when the old peer is online.
    WiFiRtcmHeader header{};header.magic=network::kMagic;header.version=network::kVersion;header.type=network::kRtcmPacket;
    header.rtcm_length=reference.size();header.rtcm_message=1006;header.packet_size=sizeof(header)+reference.size();
    std::vector<uint8_t> packet(header.packet_size);std::memcpy(packet.data(),&header,sizeof(header));std::memcpy(packet.data()+sizeof(header),reference.data(),reference.size());
    header.checksum=fnv1a(packet.data(),packet.size());std::memcpy(packet.data(),&header,sizeof(header));
    assert(!correction_link_input(reference.data(),reference.size(),host_now));  // the radio is not the route yet
    host_radio_active=true;assert(!handle_wifi_rtcm_packet(packet.data(),packet.size()));
    host_radio_linked=true;
    assert(correction_link_input(reference.data(),reference.size(),host_now-1000));
    host_now+=500;correction_service::service_output(host_now);assert(correction_service::snapshot().queued==0); // carried age expires
    host_radio_active=false;
    // Wi-Fi v3 binds every data envelope to both proven boots and the current
    // nonzero session. Replays stay rejected across an ordinary link outage.
    auto wifi_frame = [&](uint32_t sequence, uint32_t session, uint32_t sender_boot, uint32_t receiver_boot, uint8_t version=3) {
      auto h=header;h.sequence=sequence;h.session=session;h.sender_boot=sender_boot;
      h.receiver_boot=receiver_boot;h.sender_unit=1;h.sender_role=0;h.version=version;h.checksum=0;
      std::vector<uint8_t> bytes(h.packet_size);std::memcpy(bytes.data(),&h,sizeof(h));
      std::memcpy(bytes.data()+sizeof(h),reference.data(),reference.size());
      h.checksum=fnv1a(bytes.data(),bytes.size());std::memcpy(bytes.data(),&h,sizeof(h));return bytes;
    };
    correction_service::reset();wifi_last_peer_ms=host_now;
    const auto identity=link_service::snapshot(host_now);
    auto current=wifi_frame(1,identity.session,identity.peer_boot,identity.local_boot);
    assert(handle_wifi_rtcm_packet(current.data(),current.size()));
    correction_service::service_output(host_now);
    const auto delivered=gnss.binary_output.size();
    current=wifi_frame(1,identity.session,identity.peer_boot,identity.local_boot);
    assert(!handle_wifi_rtcm_packet(current.data(),current.size()));
    host_now+=4500;wifi_last_peer_ms=host_now;
    current=wifi_frame(1,identity.session,identity.peer_boot,identity.local_boot);
    assert(!handle_wifi_rtcm_packet(current.data(),current.size()));
    auto old_session=wifi_frame(2,identity.session+1,identity.peer_boot,identity.local_boot);
    auto old_sender=wifi_frame(2,identity.session,identity.peer_boot+1,identity.local_boot);
    auto old_receiver=wifi_frame(2,identity.session,identity.peer_boot,identity.local_boot+1);
    auto old_version=wifi_frame(2,identity.session,identity.peer_boot,identity.local_boot,2);
    for(auto *bad:{&old_session,&old_sender,&old_receiver,&old_version})
      assert(!handle_wifi_rtcm_packet(bad->data(),bad->size()));
    correction_service::service_output(host_now);assert(gnss.binary_output.size()==delivered);
    current=wifi_frame(2,identity.session,identity.peer_boot,identity.local_boot);
    assert(handle_wifi_rtcm_packet(current.data(),current.size()));correction_service::service_output(host_now);
    assert(gnss.binary_output.size()==delivered+reference.size());
    assert(rtcm_wifi_rx_frames==2);
    // Admitted OTA pause rejects new input and discards pending output. Preparing
    // without a pause keeps forwarding available while collection is reserved.
    host_ota_locked=true;assert(correction_service::admit(reference.data(),reference.size(),host_now,host_now));
    host_ota_paused=true;assert(!correction_service::admit(reference.data(),reference.size(),host_now,host_now));
    correction_service::service_output(host_now);assert(correction_service::snapshot().queued==0);assert(!system_ready(host_now));
    host_ota_paused=host_ota_locked=false;
    // A surfaced adapter fault latches inhibition; it never retries a suffix.
    assert(correction_service::admit(reference.data(),reference.size(),host_now,host_now));gnss.short_limit=3;correction_service::service_output(host_now);
    const auto faulted=correction_service::snapshot();
    assert(faulted.fault&&faulted.faults==1&&correction_service::health().arrival_age(host_now)==UINT32_MAX);
    assert(!correction_service::admit(reference.data(),reference.size(),host_now,host_now));gnss.short_limit=2048;
    // The correction gate follows the receiver's own profile state: dropping it
    // (a role/reference reset) refuses corrections again.
    correction_service::reset();gnss_service::reset_for_config_change();
    assert(!receiver().profile_applied&&!receiver().profile_running);
    assert(!correction_service::admit(reference.data(),reference.size(),host_now,host_now));
    host_now=before;gnss.binary_output.clear();
    std::puts("PASS: production COM2 whole-frame admission, backpressure/expiry, carried age, exclusive route, station match, profile gate, counter meanings and latched output fault");
  }
  // Five startup commands are scheduled individually; no invocation advances time.
  receiver_service_begin();                   // a fresh receiver: nothing proven yet
  assert(!receiver().version_ok&&!receiver().startup_complete);
  host_now+=1500;                             // the service schedules its first attempt
  for(unsigned i=0;i<5;++i){
    const auto before=host_now;
    const auto commands=gnss.output;
    gnss_service::service_startup(host_now);
    assert(host_now==before);                     // no invocation advances time
    assert(gnss.output.size()>commands.size());   // exactly one command per invocation
    host_now+=100;
  }
  // The block is the allowlisted handshake in order; the wrap above sends
  // nothing more until the 3 s retry window elapses.
  const std::string handshake =
      "VERSION\r\nGPGGA COM2 1\r\nGPRMC COM2 1\r\nBESTNAVA COM2 1\r\nMODE\r\n";
  assert(gnss.output.size()>=handshake.size()&&
         gnss.output.compare(gnss.output.size()-handshake.size(),handshake.size(),handshake)==0);
  host_now+=100;
  const std::string wrapped=gnss.output;
  gnss_service::service_startup(host_now);
  assert(gnss.output==wrapped);
  std::puts("PASS: observation freshness, repeated epochs, metadata outage, receiver age/station, corruption, week/timer wrap, nonblocking handshake");
  auto bestnav=[](const std::string &body){const std::string payload="BESTNAVA,97,GPS,FINE,2435,432000000,0,0,18,16;"+body;
    char suffix[12];std::snprintf(suffix,sizeof(suffix),"*%08X",ascii_crc32(payload.c_str(),payload.c_str()+payload.size()));return "#"+payload+suffix;};
  HorizontalAccuracyData observed;
  GnssParseStats parse_stats;
  const auto fixed=bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234.0,-30.0,WGS84,0.01,0.02,0.03,\"7\",1.200,0.000,30,25,25,0");
  assert(parse_bestnav_accuracy(fixed.c_str(),0,observed,parse_stats)&&observed.position_valid&&observed.rtk_fixed);
  assert(observed.position.height==2204 && observed.epoch==2435ULL*604800000+432000000 && observed.solution_station==7 && observed.differential_age_ms==1200);
  assert(parse_bestnav_accuracy(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234,-30,WGS84,0.01,0.02,0.03,\"7\",0.0,0.0,30,25").c_str(),0,observed,parse_stats)&&!observed.position_valid);
  assert(parse_bestnav_accuracy(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234,-30,WGS84,,0.02,0.03,\"7\",1.2,0.0,30,25").c_str(),0,observed,parse_stats)&&!observed.position_valid);
  auto corrupt=fixed;corrupt.back()=corrupt.back()=='0'?'1':'0';assert(!parse_bestnav_accuracy(corrupt.c_str(),0,observed,parse_stats));assert(parse_stats.checksum_errors==1);
  DeviceConfig base_config;
  base_config.role=DeviceRole::kBase;
  base_config.base_rtcm=true;
  gnss_service::set_config(base_config);
  gnss_service::set_base_coordinate(true,19.4,-99.1,2201.5);
  assert(std::string(gnss_service::active_profile_command(1))=="MODE BASE 19.40000000000 -99.10000000000 2201.5000");
  gnss_service::set_base_coordinate(false,0.0,0.0,0.0);
  std::puts("PASS: BESTNAV epoch, ellipsoidal height, correction age/base ID, invalid data and saved fixed-base command");
  ui_module_begin();
  display_ready = touch_ready = true;
  // The receiver proves the version from its own reply line, exactly as the
  // handshake's first command expects.
  reply("$command,VERSION,response: OK");
  assert(receiver().version_ok);
  load_config();
  setup_backlight(device_config.brightness, host_now);
  assert(backlight_pwm_ready && ledcReadFreq(0) == 5000);
  // The backlight curve takes whatever clock it is handed; the receiver's own
  // clock is injected later through the same sentence path.
  GnssTimeData brightness_time;
  assert(automatic_brightness_target(host_now, brightness_time) == 255); // No clock: full brightness.
  brightness_time.valid = true;
  brightness_time.received_ms = host_now;
  brightness_time.year = 2026; brightness_time.month = 9; brightness_time.day = 9;
  brightness_time.hour = 5; brightness_time.minute = 18; // 23:18 UTC-6, previous day.
  assert(automatic_brightness_target(host_now, brightness_time) == board::kNightBacklightDuty);
  uint16_t year; uint8_t month, day, hour, minute, second;
  assert(local_time_utc_minus_6(host_now, brightness_time, year, month, day, hour, minute, second));
  assert(day == 8 && hour == 23 && minute == 18);
  brightness_time.hour = 18; // Noon local.
  assert(automatic_brightness_target(host_now, brightness_time) == 255);
  brightness_time.hour = 11; brightness_time.minute = 0; // Dawn 05:00 local.
  assert(automatic_brightness_target(host_now, brightness_time) == board::kNightBacklightDuty);
  brightness_time.hour = 12; // Mid-dawn transition.
  assert(automatic_brightness_target(host_now, brightness_time) > board::kNightBacklightDuty);
  assert(automatic_brightness_target(host_now, brightness_time) < 255);
  assert(automatic_brightness_target(host_now + 3000, brightness_time) == 255); // Stale clock.
  brightness_time = GnssTimeData{};
  brightness_mode = BrightnessMode::kNight;
  for (int tick=0; tick<75; ++tick) { host_now += 20; service_brightness(host_now, brightness_mode, brightness_time); }
  assert(backlight_duty == board::kNightBacklightDuty && ledcRead(0) == board::kNightBacklightDuty);
  brightness_mode = BrightnessMode::kDay;
  host_now += 500; service_brightness(host_now, brightness_mode, brightness_time); // A busy loop must catch up, not move one step.
  assert(backlight_duty > 100 && backlight_duty < 255);
  host_now += 1000; service_brightness(host_now, brightness_mode, brightness_time);
  assert(backlight_duty == 255 && ledcRead(0) == 256);
  brightness_mode = BrightnessMode::kNight;
  setup_backlight(brightness_mode, host_now); // Saved Night starts dim, without a full-brightness flash.
  assert(backlight_duty == board::kNightBacklightDuty && ledcRead(0) == board::kNightBacklightDuty);
  brightness_mode = BrightnessMode::kAutomatic;
  setup_backlight(brightness_mode, host_now);
  assert(!is_base());
  // Failed NVS writes must leave role, Wi-Fi, and displayed brightness unchanged.
  preferences.fail = true;
  select_role(DeviceRole::kBase);
  assert(!is_base() && config_error && !receiver().profile_running);
  preferences.fail = false;
  select_role(DeviceRole::kBase);
  assert(is_base() && receiver().profile_running && WiFi.selected_mode == WIFI_AP);
  gnss_service::service_profile(host_now);
  const std::string waiting_command = gnss_service::pending_profile_command();
  reply("$command,UNLOG COM2,response: OK", true);
  // A corrupt reply is ignored: the sequencer neither advances nor applies.
  assert(receiver().profile_running && !receiver().profile_applied);
  assert(std::string(gnss_service::pending_profile_command()) == waiting_command);
  complete_profile();
  // A new profile can complete before the next GGA arrives: its own 2 s grace
  // window holds the handshake off, so a stale solution cannot restart it.
  feed_line(gga_fixture(4, 28, 0.5));    // the receiver's last solution
  host_now += 5100;                      // ... which is now older than the window
  gnss_service::apply_unit_profile();    // a fresh profile runs and completes
  complete_profile();
  const std::string after_profile = gnss.output;
  gnss_service::service_startup(host_now);
  assert(receiver().profile_applied && !receiver().profile_running);
  assert(gnss.output == after_profile);  // no handshake command during the grace period
  select_brightness(BrightnessMode::kNight);
  const auto writes = preferences.writes;
  select_brightness(BrightnessMode::kNight);
  assert(preferences.writes == writes);
  device_config = DeviceConfig{};
  load_config();
  assert(is_base() && brightness_mode == BrightnessMode::kNight);
  wifi_peer_known = true;
  wifi_last_peer_ms = host_now;
  const auto role_frame = msm(1074, 1000);
  assert(gnss_service::write_frame(role_frame.data(), role_frame.size(), host_now) == role_frame.size());
  assert(receiver().rtcm_last_rx_ms == host_now);
  select_role(DeviceRole::kRover);
  assert(!wifi_peer_known && !wifi_last_peer_ms && receiver().rtcm_last_rx_ms == 0); // Role change resets link state.
  assert(WiFi.selected_mode == WIFI_AP_STA); // Rover: station plus its phone AP.
  complete_profile();
  gnss_service::apply_unit_profile();
  gnss_service::service_profile(host_now);
  for (int retry=0; retry<3; ++retry) { host_now += 2100; gnss_service::service_profile(host_now); }
  assert(receiver().profile_failed && !receiver().profile_running && !receiver().profile_applied);
  select_role(DeviceRole::kRover);
  complete_profile();

  // Drive actual I2C touch sampling and release handling, including cancellation.
  change_page(ScreenPage::kMain);
  tap(280,455);
  assert(current_page == ScreenPage::kSettings);
  tap(75,125);
  assert(pending_role == DeviceRole::kBase && !is_base());
  contact(150,250); contact(150,290); contact(150,250); release();
  assert(!is_base()); // Dragging out and back is not an Apply tap.
  contact(150,250); contact(150,250,2); contact(150,250); release();
  assert(!is_base());
  contact(150,250); Wire.fail=true; contact(150,250); Wire.fail=false; release();
  assert(!is_base());
  contact(150,250); contact(150,250,1,1300); release();
  assert(!is_base());
  tap(150,250);
  assert(is_base() && receiver().profile_running);
  const auto brightness_before = brightness_mode;
  tap(50,344);
  assert(brightness_mode == brightness_before); // Locked while applying.
  complete_profile();
  tap(50,344);
  assert(brightness_mode == BrightnessMode::kAutomatic);
  tap(40,455);
  assert(current_page == ScreenPage::kMain);
  contact(160,100); contact(160,200); release();
  assert(current_page == ScreenPage::kGpsDetails);
  contact(160,200); contact(160,100); release();
  assert(current_page == ScreenPage::kMain);

  // Render actual production draw functions with the bundled 5x7 bitmap font.
  select_role(DeviceRole::kRover);
  complete_profile();
  // The receiver's own sentences carry the solution the rendered surfaces show;
  // no test setter exists for receiver state.
  feed_line(gga_fixture(4, 28, 0.5));
  feed_line(rmc_fixture());
  feed_line(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234.0,-30.0,WGS84,0.012,0.0,0.03,\"7\",0.500,0.000,30,25,25,0"));
  const auto current_msm=msm(1074,3000);assert(correction_service::health().observe(current_msm.data(),current_msm.size(),host_now));
  assert(gnss_service::write_frame(current_msm.data(),current_msm.size(),host_now)==current_msm.size());
  wifi_last_peer_ms=host_now;
  WiFi.linked=true;
  // What the boundary produced is exactly what the surfaces used to be handed.
  assert(receiver().gga.received&&receiver().gga.quality==4&&receiver().gga.satellites==28);
  assert(receiver().gga.hdop==0.5);
  assert(std::fabs(receiver().gga.latitude-19.4326)<1e-9&&std::fabs(receiver().gga.longitude+99.1332)<1e-9);
  assert(receiver().gga_ms==host_now);
  assert(receiver().accuracy.received&&std::fabs(receiver().accuracy.horizontal_1drms_m-0.012)<1e-12);
  assert(receiver().accuracy.received_ms==host_now&&receiver().accuracy.position_valid);
  assert(receiver().accuracy.differential_age_ms==500&&receiver().accuracy.solution_station==7);
  assert(receiver().time.valid&&receiver().time.received_ms==host_now);
  assert(receiver().time.year==2026&&receiver().time.month==9&&receiver().time.day==8);
  assert(receiver().time.hour==18&&receiver().time.minute==24);
  assert(receiver().uart_seen&&receiver().last_rx_ms==host_now);
  assert(receiver().rtcm_last_rx_ms==host_now);
  char json[kWebStatusCapacity];
  const auto config_before_web = encode_config(device_config);
  const auto commands_before_web = gnss.output;
  const auto writes_before_web = preferences.writes;
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  std::ofstream(".pio/status-ready.json") << json;
  assert(std::strstr(json, "\"ready\":true"));
  assert(std::strstr(json, "\"horizontal_uncertainty_m\":0.012000"));
  service_web_status();
  assert(published_web_rover && !published_web_status.empty());
  assert(encode_config(device_config) == config_before_web && gnss.output == commands_before_web && preferences.writes == writes_before_web);
  char too_small[16];
  assert(format_web_status(too_small, sizeof(too_small), host_now) == 0);
  const auto sample_now = host_now;
  host_now += 4000;
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  std::ofstream(".pio/status-stale.json") << json;
  assert(std::strstr(json, "\"ready\":false"));
  assert(std::strstr(json, "\"fix\":\"GNSS STALE\""));
  assert(std::strstr(json, "\"horizontal_uncertainty_m\":null"));
  assert(std::strstr(json, "\"rssi_dbm\":null"));
  assert(std::strstr(json, "\"local\":null"));
  host_now = sample_now;
  // The parser rejects non-finite sigma fields, so an unusable accuracy is only
  // reachable as "no solution": the same null is emitted for a receiver that
  // has never reported one.
  gnss_service::reset_input_state();
  assert(!receiver().accuracy.received);
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  assert(std::strstr(json, "\"horizontal_uncertainty_m\":null"));
  assert(std::strstr(json, "\"horizontal_uncertainty_label\":\"---\""));
  feed_line(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234.0,-30.0,WGS84,0.012,0.0,0.03,\"7\",0.500,0.000,30,25,25,0"));
  assert(receiver().accuracy.received);

  // R4 agreement: LCD frame, web JSON and CSV must agree on transport,
  // link state and signal availability in both radio and Wi-Fi modes.
  setup_sd_logging();
  assert(sd_ready && sd_test_passed);
  feed_line(gga_fixture(4, 28, 0.5));
  feed_line(rmc_fixture());
  wifi_last_peer_ms = host_now;
  // The receiver's last RTCM observation is the reference the link forwarded.
  const auto link_reference = msm(1006, 0);
  assert(gnss_service::write_frame(link_reference.data(),link_reference.size(),host_now)==link_reference.size());
  auto csv_field = [&](const char *file, unsigned column) {
    const std::string path = std::string(sd_session_path) + "/" + file;
    const std::string &content = SD_MMC.files[path];
    const size_t begin = content.find_last_of('\n', content.size() - 2) + 1;
    const std::string last = content.substr(begin, content.find('\n', begin) - begin);
    unsigned index = 0; size_t start = 0;
    while (start <= last.size()) {
      const size_t comma = last.find(',', start);
      if (index == column) return last.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (comma == std::string::npos) break;
      start = comma + 1; ++index;
    }
    return std::string();
  };
  // Radio mode: transport is SiK, no RSSI is fabricated, LCD shows radio state.
  host_radio_active = true; host_radio_linked = true;
  host_now += 1000;
  service_sd_logging(); sd_log_solution(host_now);
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  assert(std::strstr(json, "\"transport\":\"SiK RADIO\""));
  assert(std::strstr(json, "\"rssi_dbm\":null"));
  assert(std::strstr(json, "\"peer_age_ms\":null"));
  assert(std::strstr(json, "\"correction_link_connected\":true"));
  assert(csv_field("solution.csv", 11).empty());          // link_rssi_dbm unknown on radio
  assert(csv_field("solution.csv", 18) == "SIK");         // link_transport
  change_page(ScreenPage::kMain);
  ui_build_frame(ui_frame, host_now);
  change_page(ScreenPage::kWifiDetails);
  ui_build_frame(ui_frame, host_now);
  // Wi-Fi mode: transport label, real station RSSI and peer age are reported.
  host_radio_active = false; host_radio_linked = false;
  host_now += 1000; wifi_last_peer_ms = host_now;
  assert(gnss_service::write_frame(link_reference.data(),link_reference.size(),host_now)==link_reference.size());
  service_sd_logging(); sd_log_solution(host_now);
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  assert(std::strstr(json, "\"rssi_dbm\":-48"));
  assert(std::strstr(json, "\"peer_age_ms\":0"));
  assert(csv_field("solution.csv", 11) == "-48");
  assert(csv_field("solution.csv", 18) == "WIFI");
  change_page(ScreenPage::kMain);
  ui_build_frame(ui_frame, host_now);
  // Actual AP storage/lifecycle, subnet routing and local password UI.
  assert(rover_ap_ready() && std::strlen(rover_ap_password())==9 && rover_ap_password()[4]=='.');
  for (int i=0;i<9;++i) if (i!=4) assert(rover_ap_password()[i]>='0' && rover_ap_password()[i]<='9');
  const std::string original_key = rover_ap_password();
  const auto key_writes = storage.writes;
  stop_rover_ap(); start_rover_ap(board::kUnitLabel);
  assert(original_key == rover_ap_password() && storage.writes == key_writes);
  assert(station_broadcast()[3] == 255);
  assert(on_station_subnet(IPAddress(192,168,4,1)));
  assert(!on_station_subnet(IPAddress(192,168,8,2)));
  WiFi.station_ip = IPAddress(192,168,8,10);
  host_now += 1100; service_rover_ap();
  assert(std::string(rover_ap_address()) == "172.22.42.1");
  WiFi.station_ip = IPAddress(192,168,4,2);
  const auto saved_config = encode_config(device_config);
  const auto receiver_commands = gnss.output;
  host_diagnostic_busy=true;
  DeviceConfig blocked_config=device_config;
  blocked_config.role=DeviceRole::kBase;
  assert(!select_config(blocked_config));
  assert(!system_ready(host_now));
  assert(encode_config(device_config)==saved_config && gnss.output==receiver_commands);
  host_diagnostic_busy=false;
  change_page(ScreenPage::kWifiDetails); tap(238,402); tap(238,402); // Phone is Link page 3.
  assert(current_page == ScreenPage::kWifiDetails && ui_detail_page() == 2 && phone_key_shown_ms == 0);
  assert_clean_page(".pio/ui-link-phone.ppm");
  tap(75,236); assert(phone_key_shown_ms);
  assert_clean_page(".pio/ui-link-phone-show.ppm");
  host_now += 30001; draw_dynamic_screen(); assert(!phone_key_shown_ms);
  assert_clean_page();
  const auto hidden_phone = display->pixels;
  tap(75,236); tap(238,402); tap(82,402); // NEXT away, PREV back hides the key.
  assert(display->pixels == hidden_phone && original_key == rover_ap_password());
  tap(235,236); assert(phone_key_confirm_ms && original_key == rover_ap_password());
  assert_clean_page(".pio/ui-link-phone-confirm.ppm");
  tap(82,402); tap(238,402); // PREV away, NEXT back cancels replacement.
  assert(display->pixels == hidden_phone && original_key == rover_ap_password());
  tap(235,236);
  assert(original_key == rover_ap_password()); // Must require a new confirmation.
  tap(200,455); // Leaving cancels the pending replacement.
  tap(238,402); tap(238,402); tap(235,236);
  assert(original_key == rover_ap_password()); // Active-tab reset also cancelled it.
  host_now += 10001; draw_dynamic_screen(); assert(!phone_key_confirm_ms);
  assert_clean_page();
  assert(display->pixels == hidden_phone);
  tap(235,236); tap(235,236);
  assert(original_key != rover_ap_password() && !phone_key_shown_ms && !phone_key_confirm_ms);
  assert(encode_config(device_config) == saved_config && gnss.output == receiver_commands && WiFi.linked);
  const std::string rotated = rover_ap_password();
  storage.fail = true;
  assert(!rotate_rover_ap_password() && rotated == rover_ap_password() && rover_ap_ready());
  storage.fail = false;
  stop_rover_ap(); start_rover_ap(board::kUnitLabel);
  assert(rotated == rover_ap_password());
  stop_rover_ap(); storage.key_value="ABCDEFGHJKLMNPQR";
  const auto migration_writes=storage.writes;
  start_rover_ap(board::kUnitLabel);
  assert(rover_ap_ready() && std::strlen(rover_ap_password())==9 && storage.writes==migration_writes+1);
  stop_rover_ap();
  storage.key_value="corrupt";
  start_rover_ap(board::kUnitLabel);
  assert(!rover_ap_ready() && !WiFi.ap_enabled);
  storage.key_value=rotated;
  start_rover_ap(board::kUnitLabel);
  assert(rover_ap_ready());
  assert(format_web_status(json,sizeof(json),host_now) > 0);
  assert(!std::strstr(json,rotated.c_str()) && Serial.output.find(rotated) == std::string::npos);
  std::puts("PASS: Rover AP persistence, corrupt/failed storage, rotation, touch confirmation, subnet isolation, no password in API/logs");
  // Touchscreen Link-mode selector (R6b): the page issues the same pair-wide
  // request as the web Settings page, with one-tap arming, an explicit confirm
  // and a recovery escape hatch that stays gated to an unconfirmed pair.
  change_page(ScreenPage::kWifiDetails);
  tap(238, 402); tap(238, 402); tap(238, 402);  // rows -> counters -> phone -> link mode
  assert(ui_detail_page() == 3);
  assert(touch_action(ScreenPage::kWifiDetails, 3, 80, 236) == TouchAction::kLinkRadio);
  assert(touch_action(ScreenPage::kWifiDetails, 3, 238, 236) == TouchAction::kLinkWifi);
  assert(touch_action(ScreenPage::kWifiDetails, 3, 160, 312) == TouchAction::kLinkRecover);
  assert_clean_page(".pio/ui-link-3.ppm");
  host_link_requests = host_link_selections = 0;
  host_link_refuse = host_link_select_refuse = false;
  host_link_revision_value = 7;
  host_operation_state = "idle";
  host_operation_active = false;
  host_operation_storage_ok = true;
  ui_build_frame(ui_frame, host_now);
  assert(std::string(ui_frame.link_operation_line) == "NONE YET");
  assert(std::string(ui_frame.link_selected_line).find("WI-FI") == 0);
  tap(80, 236);  // Arm the radio switch; nothing is sent yet.
  assert(ui_link_confirm_active(host_now) && ui_link_confirm_action() == TouchAction::kLinkRadio);
  assert(host_link_requests == 0);
  assert_clean_page(".pio/ui-link-3-confirm.ppm");
  tap(80, 236);  // Confirm: the coordinator receives the request.
  assert(!ui_link_confirm_active(host_now));
  assert(host_link_requests == 1 && host_link_transport == 1 && host_link_revision == 7);
  assert(std::strlen(host_link_id) == 32);
  for (size_t i = 0; i < 32; ++i)
    assert((host_link_id[i] >= '0' && host_link_id[i] <= '9') ||
           (host_link_id[i] >= 'a' && host_link_id[i] <= 'f'));
  // A refusal is reported instead of pretending the switch started.
  host_link_refuse = true;
  tap(238, 236); tap(238, 236);
  ui_build_frame(ui_frame, host_now);
  assert(host_link_requests == 2);
  assert(std::string(ui_frame.link_mode_hint).find("NOT ACCEPTED") != std::string::npos);
  host_link_refuse = false;
  // An armed tap expires on its own, exactly like the key confirmation.
  tap(80, 236);
  assert(ui_link_confirm_active(host_now));
  host_now += 10001;
  ui_build_frame(ui_frame, host_now);
  assert(!ui_link_confirm_active(host_now));
  // Apply-locally is refused while the pair record is healthy...
  host_operation_storage_ok = true;
  tap(160, 312); tap(160, 312);
  assert(host_link_selections == 0);
  // ...and offered for a pair that cannot confirm a normal selection.
  host_operation_storage_ok = false;
  ui_build_frame(ui_frame, host_now);
  assert(ui_frame.link_recover_available);
  tap(160, 312); tap(160, 312);
  assert(host_link_selections == 1);
  host_operation_storage_ok = true;
  change_page(ScreenPage::kMain);
  assert(!ui_link_confirm_active(host_now));
  std::puts("PASS: touchscreen Link mode, pair-wide request shape, refusal copy, armed-tap expiry and gated local recovery");
  // Debug is enabled by default at boot; the touchscreen toggle turns it off
  // and on, including during receiver setup, and the session persists over time.
  assert(debug_enabled());change_page(ScreenPage::kSettings);gnss_service::apply_unit_profile();
  tap(260,400);assert(current_page==ScreenPage::kDebug);tap(140,268);assert(!debug_enabled());
  tap(140,268);assert(debug_enabled());
  complete_profile();const auto debug_start=host_now;char debug_json[8192];
  debug_observe(debugmode::Channel::GnssRx,"$GPGGA,observed");assert(debug_logs(debug_json,sizeof(debug_json)));assert(std::strstr(debug_json,"GPGGA"));
  for(unsigned i=0;i<10;++i){host_now=debug_start+i*80000;assert(debug_status(debug_json,sizeof(debug_json)));}
  host_now=debug_start+899999;assert(debug_enabled());assert(std::strstr(debug_json,"\"enabled\":true"));
  host_now+=1800000;assert(debug_enabled());assert(debug_status(debug_json,sizeof(debug_json)));assert(std::strstr(debug_json,"\"enabled\":true")&&debug_logs(debug_json,sizeof(debug_json)));
  tap(140,268);assert(!debug_enabled());assert(!debug_logs(debug_json,sizeof(debug_json)));
  debug_enable_local(true);assert(debug_logs(debug_json,sizeof(debug_json)));assert(!std::strstr(debug_json,"GPGGA"));
  tap(140,268);assert(!debug_enabled());host_now=debug_start;
  debugmode::Session fresh;assert(fresh.active(0)); // A constructed session is enabled by default.
  debugmode::Log bounded;bounded.clear(0);for(unsigned i=0;i<1000;++i)bounded.push(debugmode::Channel::Event,"test",0);assert(bounded.size()==8&&bounded.throttled==992);
  for(unsigned t=1000;t<10000;t+=1000)for(unsigned i=0;i<8;++i)bounded.push(debugmode::Channel::Event,"line",t);assert(bounded.size()==32&&bounded.overwritten>0);
  std::puts("PASS: Debug default-on at boot, touchscreen toggle during receiver setup, persistent session over time, disable clears capture, bounded capture and unchanged COM2 output");
  // The receiver's solution the previews render: the same sentences the
  // surfaces were fed, now old enough that every surface reports it stale.
  feed_line(gga_fixture(4, 28, 0.5));
  feed_line(rmc_fixture());
  feed_line(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234.0,-30.0,WGS84,0.012,0.0,0.03,\"7\",0.500,0.000,30,25,25,0"));
  host_now += 4000;                        // the receiver goes quiet between updates
  const ScreenPage pages[] = {ScreenPage::kMain, ScreenPage::kGpsDetails,
                             ScreenPage::kWifiDetails, ScreenPage::kSettings, ScreenPage::kDebug};
  for (ScreenPage page : pages) {
    change_page(page);
    const std::string path=".pio/ui-"+std::to_string(static_cast<unsigned>(page))+".ppm";
    assert_clean_page(path.c_str());
  }
  change_page(ScreenPage::kGpsDetails);
  assert(ui_detail_page() == 0);
  assert_clean_page();
  tap(160,402); // The single GPS button flips between its two pages.
  assert(ui_detail_page() == 1);
  assert_clean_page();
  tap(160,402);
  assert(ui_detail_page() == 0);
  assert_clean_page();
  change_page(ScreenPage::kWifiDetails);
  // Exercise all six directed transitions, including wraparound. The old
  // standalone Phone preview could not detect rows painted over key controls.
  const uint8_t link_pages[] = {1, 2, 3, 2, 1, 0};
  for (unsigned step=0; step<6; ++step) {
    tap(step < 3 ? 238 : 82,402);
    assert(ui_detail_page() == link_pages[step]);
    const std::string path=".pio/ui-link-"+std::to_string(ui_detail_page())+".ppm";
    assert_clean_page(path.c_str());
  }
  const auto before_unavailable_display = display->draws;
  display_ready = false;
  tap(238,402);
  assert(display->draws == before_unavailable_display);
  display_ready = true;
  change_page(ScreenPage::kSettings);
  pending_role=DeviceRole::kBase;
  draw_dynamic_screen(); display->save(".pio/ui-base-selection.ppm");
  std::puts("PASS: production profile, NVS failures/reload, reset grace, touch actions/cancellation, UI bounds and repaint cache");

  // The receiver's own reader: byte/line accounting, the line-overflow counter
  // and every RTCM framing reject are observable through the snapshot.
  {
    const uint32_t bytes_before = receiver().bytes;
    const uint32_t lines_before = receiver().lines;
    feed_raw("$GPGGA,", 7);                 // a partial sentence stays buffered
    assert(receiver().bytes == bytes_before + 7 && receiver().lines == lines_before);
    feed_line("partial");
    assert(receiver().lines == lines_before + 1);
    const uint32_t errors_before = receiver().checksum_errors;
    const std::string overlong(600, 'x');
    feed_raw(overlong.data(), overlong.size());  // overflows the 512-byte line buffer
    assert(receiver().checksum_errors == errors_before + 1);
    assert(Serial.output.find("UM980> RX LINE OVERFLOW") != std::string::npos);
    feed_line(std::string());               // terminate the remainder the overflow left
    const GnssSnapshot before_rtcm = receiver();
    feed_bytes(std::vector<uint8_t>{0xD3, 0x00, 0x00});  // payload < 2: rejected at the head
    assert(receiver().rtcm_frames == before_rtcm.rtcm_frames &&
           receiver().rtcm_bad == before_rtcm.rtcm_bad + 1);
    feed_bytes(std::vector<uint8_t>{0xD3, 0x02});        // an incomplete frame
    host_now += 300;                                    // past the 250 ms assembler gap
    feed_line(std::string());                           // the gap abandons it, the newline is ignored
    assert(receiver().rtcm_bad == before_rtcm.rtcm_bad + 2);
    auto corrupt_frame = msm(1074, 1000);
    corrupt_frame.back() ^= 1;                          // CRC24Q mismatch
    feed_bytes(corrupt_frame);
    assert(receiver().rtcm_bad == before_rtcm.rtcm_bad + 3 &&
           receiver().rtcm_frames == before_rtcm.rtcm_frames);
    const auto whole_frame = msm(1074, 1000);
    feed_bytes(whole_frame);
    assert(receiver().rtcm_frames == before_rtcm.rtcm_frames + 1);
    assert(receiver().rtcm_last_message == 1074);
    // Only the receiver's own output sets the receiver-side age, and only on Base.
    assert(receiver().rtcm_last_rx_ms == before_rtcm.rtcm_last_rx_ms);
    std::puts("PASS: receiver stream counters, line overflow and RTCM framing rejects");
  }

  // Base: a whole RTCM 1006 frame from the receiver is counted, captured as the
  // reference/station the survey glue reads, and admitted on the started socket.
  // The role/profile/diagnostic gate stays with the composition root.
  {
    select_role(DeviceRole::kBase);
    complete_profile();
    assert(wifi_transport::start(wifi_transport::Channel::Corrections));
    host_radio_active = false;
    wifi_last_peer_ms = host_now;
    const auto reference = reference_frame(7, survey::Position(19.4, -99.1, 2201.5));
    const uint32_t frames_before = receiver().rtcm_frames;
    const uint32_t wifi_tx_before = rtcm_wifi_tx_frames;
    feed_bytes(reference);
    assert(receiver().rtcm_frames == frames_before + 1);
    assert(receiver().rtcm_last_message == 1006);
    assert(receiver().rtcm_last_rx_ms == host_now);        // Base observes its own receiver
    assert(receiver().station == 7 && receiver().reference_ms == host_now);
    assert(rtcm_wifi_tx_frames == wifi_tx_before + 1);     // admitted by the root's gate
    host_diagnostic_busy = true;
    feed_bytes(reference);
    assert(rtcm_wifi_tx_frames == wifi_tx_before + 1);     // a diagnostic owns the link
    host_diagnostic_busy = false;
    std::puts("PASS: receiver RTCM reference capture, station identity and admission gate");
  }

  // Console verbs read the receiver through its snapshot, and the only command
  // the console can send is the allowlisted MODE query.
  {
    const std::string commands_before_console = gnss.output;
    handle_usb_command("role?");
    assert(gnss.output.size() > commands_before_console.size());
    assert(gnss.output.compare(gnss.output.size()-6,6,"MODE\r\n")==0);
    assert(Serial.output.find("ESP32> MODE") != std::string::npos);
    const std::string after_query = gnss.output;
    handle_usb_command("config?");
    handle_usb_command("rtcm?");
    handle_usb_command("accuracy?");
    handle_usb_command("time?");
    assert(Serial.output.find("RTCM STATUS: UART=") != std::string::npos);
    assert(Serial.output.find("ACCURACY STATUS: BESTNAV=") != std::string::npos);
    assert(Serial.output.find("GNSS TIME:") != std::string::npos);
    assert(gnss.output == after_query);                   // no console verb writes COM2
    handle_usb_command("saveconfig");
    assert(gnss.output == after_query);                   // no arbitrary passthrough
    assert(Serial.output.find("CONSOLE> rejected") != std::string::npos);
    std::puts("PASS: receiver console verbs read the snapshot and keep the allowlist");
  }

  // A proven startup that loses the receiver re-arms the handshake and drops the
  // profile and the acknowledged role, all inside the receiver owner.
  {
    reply("$command,VERSION,response: OK");
    feed_line(gga_fixture(4, 28, 0.5));
    gnss_service::service_startup(host_now);
    assert(receiver().startup_complete && receiver().version_ok);
    host_now += 6000;                     // the receiver goes quiet
    const std::string before_link_loss = gnss.output;
    gnss_service::service_startup(host_now);
    assert(!receiver().startup_complete && !receiver().version_ok);
    assert(!receiver().profile_applied && receiver().rtcm_last_rx_ms == 0);
    assert(std::string(receiver().role) == "UNKNOWN");
    assert(gnss.output.size() > before_link_loss.size());  // the handshake restarts
    std::puts("PASS: receiver link loss re-arms the handshake and drops the profile");
  }
}
