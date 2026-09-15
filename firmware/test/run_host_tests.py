"""Run firmware against hardware doubles; render the actual UI into PNG previews.

Run from firmware after a PlatformIO unit_b build.
Requires Python 3 and g++ on PATH; no third-party Python modules.
"""
from pathlib import Path
import re
import struct
import subprocess
import zlib
import json

root = Path(__file__).resolve().parents[1]
source = (root / 'src/rover_ap.cpp').read_text(encoding='utf-8') + '\n' + (root / 'src/main.cpp').read_text(encoding='utf-8')
hardware = r'(Arduino|Arduino_GFX_Library|FS|Preferences|SD_MMC|TCA9554|WiFi|WiFiUdp|Wire|esp_system)\.h'
source += '\n' + (root / 'src/debug_service.cpp').read_text(encoding='utf-8')
source = re.sub(r'^#include <' + hardware + r'>\n', '', source, flags=re.M)
generated = root / '.pio/host_firmware.cpp'
stubs = '''
bool host_diagnostic_busy=false;
bool host_radio_active=false;
bool host_radio_linked=false;
const char *host_pair_reason="peer_unreachable";
uint32_t host_link_requests=0,host_link_revision=0,host_link_revision_value=0,host_link_selections=0;
uint8_t host_link_transport=0;
char host_link_id[33]={};
bool host_link_refuse=false,host_link_select_refuse=false;
const char *host_operation_state="idle",*host_operation_reason="",*host_operation_transport="wifi";
bool host_operation_active=false,host_operation_storage_ok=true;
uint32_t host_operation_remaining_ms=0;
namespace link_service {
bool radio_active(){return host_radio_active;}
pair_session::Snapshot snapshot(uint32_t now){
  pair_session::Snapshot s;
  s.transport=host_radio_active?pair_session::Transport::Radio:pair_session::Transport::WiFi;
  s.local_boot=web_boot_id();s.peer_boot=0x98765432;s.session=7777777;s.established=true;
  s.peer_age_ms=wifi_last_peer_ms?now-wifi_last_peer_ms:UINT32_MAX;
  s.connected=host_radio_active?host_radio_linked:(wifi_last_peer_ms&&now-wifi_last_peer_ms<4000);
  s.reason=s.connected?"connected":host_pair_reason;return s;
}
bool connected(uint32_t now){return snapshot(now).connected;}
bool radio_submit(const uint8_t *,size_t,uint32_t){return true;}
void clear_pending(){}
void begin(bool,uint32_t){}
IPAddress wifi_peer(){return IPAddress(192,168,4,1);}
void note_incompatible(uint32_t){}
void service_settings(uint32_t,bool){}
bool settings_snapshot(char *,size_t){return false;}
// The touchscreen Link-mode page consumes the same published record as the web
// Settings page, so the double records exactly what the page asked for (R6b).
uint32_t revision(){return host_link_revision_value;}
OperationView operation_view(uint32_t){
  OperationView out;
  out.state=host_operation_state;out.reason=host_operation_reason;
  out.transport=host_operation_transport;out.active=host_operation_active;
  out.storage_ok=host_operation_storage_ok;out.remaining_ms=host_operation_remaining_ms;
  out.revision=host_link_revision_value;return out;
}
bool request_operation(link_operation::Kind,Transport transport,const char *id,uint32_t revision,link_operation::Reason &reason,uint8_t){
  ++host_link_requests;host_link_transport=uint8_t(transport);host_link_revision=revision;
  std::snprintf(host_link_id,sizeof(host_link_id),"%s",id?id:"");
  if(host_link_refuse){reason=link_operation::Reason::Busy;return false;}
  return true;
}
bool cancel_operation(const char *,link_operation::Reason &){return false;}
bool select(Transport,bool,uint32_t){++host_link_selections;return !host_link_select_refuse;}
}
void ota_boot_begin(){}
void ota_service(uint32_t,bool,bool,bool){}
bool host_ota_locked=false,host_ota_paused=false;
bool ota_locked(){return host_ota_locked;}
bool ota_paused(){return host_ota_paused;}
bool survey_service_ready(){return true;}
bool web_service_ready(){return true;}
void peer_update_label(char *out,size_t){out[0]=0;}
void diagnostic_begin() {}
void diagnostic_service(uint32_t,bool,IPAddress,bool) {}
bool diagnostic_busy() {return host_diagnostic_busy;}
bool diagnostic_request(const char *) {return false;}
bool diagnostic_snapshot(char *,size_t) {return false;}
void survey_begin(bool) {}
void survey_update(const survey::Fix &) {}
bool survey_take_base(survey::BaseRequest &) {return false;}
bool survey_sd_lock() {return true;}
void survey_sd_unlock() {}
void survey_revoke_control() {}
'''
generated.write_text('#include "host_hardware.h"\n' + source + stubs + '\n#include "firmware_cases.h"\n', encoding='utf-8')
def adapted(name):
    text = (root / 'src' / name).read_text(encoding='utf-8')
    text = re.sub(r'^#include <' + hardware + r'>\n', '', text, flags=re.M)
    return '#include "host_hardware.h"\n' + text

for name in ('ui_display.cpp', 'ui_screens.cpp', 'wifi_transport.cpp',
             'network_service.cpp'):
    (root / f'.pio/host_{name}').write_text(adapted(name))
subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Isrc',
                'test/radio_framing_cases.cpp', '-o', '.pio/test_radio_framing.exe'], cwd=root, check=True)
subprocess.run([str(root / '.pio/test_radio_framing.exe')], cwd=root, check=True)
subprocess.run(['g++', '-std=c++11', '-DTOPORTK_UNIT_ID=2', '-DTOPORTK_DISPLAY_ROTATION=0',
                '-Itest', '-Isrc', '-I.pio/libdeps/unit_b/GFX Library for Arduino/src',
                '-I.pio/libdeps/unit_a/ArduinoJson/src', 'test/host_hardware.cpp',
                'src/survey_engine.cpp', 'src/gnss_parser.cpp',
                'src/touch_input.cpp', 'src/link_operation.cpp',
                '.pio/host_ui_display.cpp', '.pio/host_ui_screens.cpp',
                '.pio/host_wifi_transport.cpp', '.pio/host_network_service.cpp',
                str(generated), '-o', '.pio/test_firmware.exe'], cwd=root, check=True)
subprocess.run([str(root / '.pio/test_firmware.exe')], cwd=root, check=True)
for name in ('ready', 'stale'):
    snapshot = json.loads((root / f'.pio/status-{name}.json').read_text())
    assert snapshot['api_version'] == 1 and snapshot['device']['role'] == 'ROVER'
    assert snapshot['state']['ready'] == (name == 'ready')
    # Actual whole-frame COM2 admissions: two radio-path frames plus the two
    # admitted Wi-Fi v3 frames in the transport-identity case.
    assert snapshot['link']['rtcm_received_frames'] == 4
print('PASS: JSON snapshots, unavailable values, bounded response, read-only state generation')

def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

for ppm in (root / '.pio').glob('ui-*.ppm'):
    header, dimensions, maximum, data = ppm.read_bytes().split(b'\n', 3)
    assert header == b'P6' and maximum == b'255'
    width, height = map(int, dimensions.split())
    rows = b''.join(b'\0' + data[y*width*3:(y+1)*width*3] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    ppm.with_suffix('.png').write_bytes(png)
print('UI previews: .pio/ui-0.png through ui-4.png, ui-link-*.png and ui-base-selection.png')
