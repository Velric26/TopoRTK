std::string published_web_status;
bool published_web_rover = false;
uint32_t web_boot_id() { return 0x1234abcd; }
void publish_web_status(const char *json, size_t length, uint32_t, bool rover) {
  published_web_status.assign(json, length);
  published_web_rover = rover;
}

void reply(const std::string &body, bool corrupt = false) {
  uint8_t checksum = 0;
  for (char c : body) checksum ^= c;
  char line[256];
  std::snprintf(line, sizeof(line), "%s*%02X", body.c_str(), corrupt ? checksum ^ 1 : checksum);
  handle_line(line);
}

void complete_profile() {
  unsigned count = 0;
  while (profile_running) {
    assert(++count <= 12);
    assert(!unit_profile_applied);
    service_profile();
    const std::string command = active_profile_command(profile_step);
    reply("$command," + command + ",response: OK");
    if (command == "MODE") reply(std::string("#MODE,TEST;MODE ") + (is_base() ? "BASE" : "ROVER"));
    host_now += 110;
    service_profile();
  }
  assert(unit_profile_applied && !profile_failed);
}

void contact(int x, int y, int points=1, int elapsed=30) {
  Wire.x=x; Wire.y=y; Wire.points=points;
  host_now += elapsed;
  service_swipe_navigation();
}
void release() { contact(Wire.x, Wire.y, 0, 80); }
void tap(int x,int y) { contact(x,y); release(); }

int main() {
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
    const auto before=host_now;device_config.role=DeviceRole::kRover;unit_profile_applied=true;
    correction_output_reset();debug_enable_local(true);gnss.binary_output.clear();gnss.tx_free=0;
    auto reference=msm(1006,0),observation=msm(1074,1000);
    assert(!queue_correction(observation.data(),observation.size(),host_now));
    assert(queue_correction(reference.data(),reference.size(),host_now));service_correction_output();assert(gnss.binary_output.empty());
    host_now+=1500;service_correction_output();assert(!correction_output.size()&&gnss.binary_output.empty());
    assert(queue_correction(reference.data(),reference.size(),host_now));
    assert(queue_correction(observation.data(),observation.size(),host_now));gnss.tx_free=31;
    service_correction_output();assert(gnss.binary_output.empty());gnss.tx_free=2048;
    service_correction_output();assert(gnss.binary_output==reference);service_correction_output();
    auto expected=reference;expected.insert(expected.end(),observation.begin(),observation.end());assert(gnss.binary_output==expected);
    assert(correction_health.arrival_age(host_now)==0);
    // A SiK selection excludes Wi-Fi copies, even when the old peer is online.
    WiFiRtcmHeader header{};header.magic=network::kMagic;header.version=network::kVersion;header.type=network::kRtcmPacket;
    header.rtcm_length=reference.size();header.rtcm_message=1006;header.packet_size=sizeof(header)+reference.size();
    std::vector<uint8_t> packet(header.packet_size);std::memcpy(packet.data(),&header,sizeof(header));std::memcpy(packet.data()+sizeof(header),reference.data(),reference.size());
    header.checksum=fnv1a(packet.data(),packet.size());std::memcpy(packet.data(),&header,sizeof(header));
    host_radio_active=true;assert(!handle_wifi_rtcm_packet(packet.data(),packet.size()));
    assert(correction_radio_input(reference.data(),reference.size(),host_now-1000));
    host_now+=500;service_correction_output();assert(correction_output.size()==0); // carried age expires
    host_radio_active=false;
    // A surfaced adapter fault latches inhibition; it never retries a suffix.
    assert(queue_correction(reference.data(),reference.size(),host_now));gnss.short_limit=3;service_correction_output();
    assert(correction_output_fault&&correction_health.arrival_age(host_now)==UINT32_MAX);
    assert(!queue_correction(reference.data(),reference.size(),host_now));gnss.short_limit=2048;
    correction_output_reset();unit_profile_applied=false;assert(!queue_correction(reference.data(),reference.size(),host_now));
    host_now=before;gnss.binary_output.clear();debug_enable_local(false);
    std::puts("PASS: production COM2 whole-frame admission, backpressure/expiry, carried age, exclusive route, profile gate and latched output fault");
  }
  // Five startup commands are scheduled individually; no invocation advances time.
  profile_running=false;version_ok=false;gnss_startup_complete=false;next_gnss_handshake_ms=host_now;gnss_handshake_step=0;
  for(unsigned i=0;i<5;++i){const auto before=host_now;service_gnss_startup();assert(host_now==before);host_now+=100;}assert(!gnss_handshake_step);
  std::puts("PASS: observation freshness, repeated epochs, metadata outage, receiver age/station, corruption, week/timer wrap, nonblocking handshake");
  auto bestnav=[](const std::string &body){const std::string payload="BESTNAVA,97,GPS,FINE,2435,432000000,0,0,18,16;"+body;
    char suffix[12];std::snprintf(suffix,sizeof(suffix),"*%08X",ascii_crc32(payload.c_str(),payload.c_str()+payload.size()));return "#"+payload+suffix;};
  HorizontalAccuracyData observed;
  const auto fixed=bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234.0,-30.0,WGS84,0.01,0.02,0.03,\"7\",1.200,0.000,30,25,25,0");
  assert(parse_bestnav_accuracy(fixed.c_str(),observed)&&observed.position_valid&&observed.rtk_fixed);
  assert(observed.position.height==2204 && observed.epoch==2435ULL*604800000+432000000 && observed.solution_station==7 && observed.differential_age_ms==1200);
  assert(parse_bestnav_accuracy(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234,-30,WGS84,0.01,0.02,0.03,\"7\",0.0,0.0,30,25").c_str(),observed)&&!observed.position_valid);
  assert(parse_bestnav_accuracy(bestnav("SOL_COMPUTED,NARROW_INT,19.4,-99.1,2234,-30,WGS84,,0.02,0.03,\"7\",1.2,0.0,30,25").c_str(),observed)&&!observed.position_valid);
  auto corrupt=fixed;corrupt.back()=corrupt.back()=='0'?'1':'0';assert(!parse_bestnav_accuracy(corrupt.c_str(),observed));
  device_config.role=DeviceRole::kBase;base_settings.fixed=1;base_settings.latitude=19.4;base_settings.longitude=-99.1;base_settings.height=2201.5;
  assert(std::string(active_profile_command(1))=="MODE BASE 19.40000000000 -99.10000000000 2201.5000");
  base_settings=BaseSettings{};
  std::puts("PASS: BESTNAV epoch, ellipsoidal height, correction age/base ID, invalid data and saved fixed-base command");
  display_ready = touch_ready = true;
  version_ok = gnss_startup_complete = true;
  load_config();
  setup_backlight();
  assert(backlight_pwm_ready && ledcReadFreq(0) == 5000);
  assert(automatic_brightness_target(host_now) == 255); // No clock: full brightness.
  latest_gnss_time.valid = true;
  latest_gnss_time.received_ms = host_now;
  latest_gnss_time.year = 2026; latest_gnss_time.month = 9; latest_gnss_time.day = 9;
  latest_gnss_time.hour = 5; latest_gnss_time.minute = 18; // 23:18 UTC-6, previous day.
  assert(automatic_brightness_target(host_now) == board::kNightBacklightDuty);
  uint16_t year; uint8_t month, day, hour, minute, second;
  assert(local_time_utc_minus_6(host_now, year, month, day, hour, minute, second));
  assert(day == 8 && hour == 23 && minute == 18);
  latest_gnss_time.hour = 18; // Noon local.
  assert(automatic_brightness_target(host_now) == 255);
  latest_gnss_time.hour = 11; latest_gnss_time.minute = 0; // Dawn 05:00 local.
  assert(automatic_brightness_target(host_now) == board::kNightBacklightDuty);
  latest_gnss_time.hour = 12; // Mid-dawn transition.
  assert(automatic_brightness_target(host_now) > board::kNightBacklightDuty);
  assert(automatic_brightness_target(host_now) < 255);
  assert(automatic_brightness_target(host_now + 3000) == 255); // Stale clock.
  latest_gnss_time = GnssTimeData{};
  brightness_mode = BrightnessMode::kNight;
  for (int tick=0; tick<75; ++tick) { host_now += 20; service_brightness(); }
  assert(backlight_duty == board::kNightBacklightDuty && ledcRead(0) == board::kNightBacklightDuty);
  brightness_mode = BrightnessMode::kDay;
  host_now += 500; service_brightness(); // A busy loop must catch up, not move one step.
  assert(backlight_duty > 100 && backlight_duty < 255);
  host_now += 1000; service_brightness();
  assert(backlight_duty == 255 && ledcRead(0) == 256);
  brightness_mode = BrightnessMode::kNight;
  setup_backlight(); // Saved Night starts dim, without a full-brightness flash.
  assert(backlight_duty == board::kNightBacklightDuty && ledcRead(0) == board::kNightBacklightDuty);
  brightness_mode = BrightnessMode::kAutomatic;
  setup_backlight();
  assert(!is_base());
  // Failed NVS writes must leave role, Wi-Fi, and displayed brightness unchanged.
  preferences.fail = true;
  select_role(DeviceRole::kBase);
  assert(!is_base() && config_error && !profile_running);
  preferences.fail = false;
  select_role(DeviceRole::kBase);
  assert(is_base() && profile_running && WiFi.selected_mode == WIFI_AP);
  service_profile();
  reply("$command,UNLOG COM2,response: OK", true);
  assert(!profile_ack && !unit_profile_applied);
  complete_profile();
  // A new profile can complete before the next GGA; it must not restart immediately.
  last_gga_ms = 0;
  host_now += 100;
  service_gnss_startup();
  assert(unit_profile_applied && !profile_running);
  select_brightness(BrightnessMode::kNight);
  const auto writes = preferences.writes;
  select_brightness(BrightnessMode::kNight);
  assert(preferences.writes == writes);
  device_config = DeviceConfig{};
  load_config();
  assert(is_base() && brightness_mode == BrightnessMode::kNight);
  wifi_peer_known = true;
  wifi_last_peer_ms = rtcm_last_rx_ms = host_now;
  select_role(DeviceRole::kRover);
  assert(!wifi_peer_known && !wifi_last_peer_ms && !rtcm_last_rx_ms);
  assert(WiFi.selected_mode == WIFI_AP_STA);
  complete_profile();
  apply_unit_profile();
  service_profile();
  for (int retry=0; retry<3; ++retry) { host_now += 2100; service_profile(); }
  assert(profile_failed && !profile_running && !unit_profile_applied);
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
  assert(is_base() && profile_running);
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
  latest_gga.received=true; latest_gga.quality=4; latest_gga.satellites=28;
  latest_gga.hdop=0.5; latest_gga.latitude=19.4326; latest_gga.longitude=-99.1332;
  latest_horizontal_accuracy.received=true;
  latest_horizontal_accuracy.horizontal_1drms_m=0.012;
  latest_horizontal_accuracy.received_ms=host_now;
  latest_horizontal_accuracy.position_valid=true;latest_horizontal_accuracy.differential_age_ms=500;latest_horizontal_accuracy.solution_station=7;
  const auto current_msm=msm(1074,3000);assert(correction_health.observe(current_msm.data(),current_msm.size(),host_now));
  latest_gnss_time.valid=latest_gnss_time.received=true;
  latest_gnss_time.year=2026; latest_gnss_time.month=9; latest_gnss_time.day=8;
  latest_gnss_time.hour=18; latest_gnss_time.minute=24; latest_gnss_time.received_ms=host_now;
  byte_count=1; last_rx_ms=last_gga_ms=wifi_last_peer_ms=rtcm_last_rx_ms=host_now;
  WiFi.linked=true;
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
  const double valid_accuracy = latest_horizontal_accuracy.horizontal_1drms_m;
  latest_horizontal_accuracy.horizontal_1drms_m = NAN;
  assert(format_web_status(json, sizeof(json), host_now) > 0);
  assert(std::strstr(json, "\"horizontal_uncertainty_m\":null"));
  latest_horizontal_accuracy.horizontal_1drms_m = valid_accuracy;
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
  change_page(ScreenPage::kWifiDetails); tap(150,402);
  assert(current_page == ScreenPage::kPhone && phone_key_shown_ms == 0);
  tap(75,236); assert(phone_key_shown_ms);
  host_now += 30001; draw_dynamic_screen(); assert(!phone_key_shown_ms);
  tap(235,236); assert(phone_key_confirm_ms && original_key == rover_ap_password());
  tap(200,455); // Leaving cancels the pending replacement.
  tap(150,402); tap(235,236);
  host_now += 10001; draw_dynamic_screen(); assert(!phone_key_confirm_ms);
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
  // Hardware-only enable, polling does not extend lease, user activity does.
  assert(!debug_enabled());change_page(ScreenPage::kSettings);profile_running=true;
  tap(260,400);assert(current_page==ScreenPage::kDebug);tap(140,268);assert(debug_enabled());
  profile_running=false;const auto debug_start=host_now;char debug_json[8192];
  debug_observe(debugmode::Channel::GnssRx,"$GPGGA,observed");assert(debug_logs(debug_json,sizeof(debug_json)));assert(std::strstr(debug_json,"GPGGA"));
  for(unsigned i=0;i<10;++i){host_now=debug_start+i*80000;assert(debug_status(debug_json,sizeof(debug_json)));}
  host_now=debug_start+899999;assert(debug_enabled());assert(debug_activity());
  host_now+=899999;assert(debug_enabled());host_now+=1;debug_service();assert(!debug_enabled());assert(!debug_logs(debug_json,sizeof(debug_json)));
  debug_enable_local(true);assert(debug_logs(debug_json,sizeof(debug_json)));assert(!std::strstr(debug_json,"GPGGA"));
  tap(140,268);assert(!debug_enabled());host_now=debug_start;
  debugmode::Session wrapped;wrapped.enable(0xfffffff0u);assert(wrapped.active(100));assert(!wrapped.active(uint32_t(0xfffffff0u+debugmode::idle_ms)));
  debugmode::Log bounded;bounded.clear(0);for(unsigned i=0;i<1000;++i)bounded.push(debugmode::Channel::Event,"test",0);assert(bounded.size()==8&&bounded.throttled==992);
  for(unsigned t=1000;t<10000;t+=1000)for(unsigned i=0;i<8;++i)bounded.push(debugmode::Channel::Event,"line",t);assert(bounded.size()==32&&bounded.overwritten>0);
  std::puts("PASS: Debug physical touch, enable during receiver setup, 15-minute idle lease, polling isolation, activity renewal, expiry/clear, wrap, bounded capture and unchanged COM2 output");
  for (unsigned page=0; page<6; ++page) {
    current_page=static_cast<ScreenPage>(page); pending_role=device_config.role;
    draw_static_screen(); draw_dynamic_screen();
    const auto draws=display->draws;
    draw_dynamic_screen();
    assert(display->draws==draws); // No repaint when nothing changed.
    const std::string path=".pio/ui-"+std::to_string(page)+".ppm";
    display->save(path.c_str());
  }
  current_page=ScreenPage::kSettings;
  draw_static_screen(); draw_dynamic_screen();
  pending_role=DeviceRole::kBase;
  draw_dynamic_screen(); display->save(".pio/ui-base-selection.ppm");
  std::puts("PASS: production profile, NVS failures/reload, reset grace, touch actions/cancellation, UI bounds and repaint cache");
}
