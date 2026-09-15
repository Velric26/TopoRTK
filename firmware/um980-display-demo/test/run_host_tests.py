"""Run firmware against hardware doubles; render the actual UI into PNG previews.

Run from firmware/um980-display-demo after a PlatformIO unit_b build.
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
bool correction_radio_active(){return host_radio_active;}
bool correction_radio_linked(uint32_t){return false;}
bool correction_radio_submit(const uint8_t *,size_t,uint32_t){return true;}
void correction_radio_stop(){host_radio_active=false;}
void correction_radio_clear_pending(){}
bool correction_radio_needs_rejoin(){return false;}
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
                'src/touch_input.cpp', '.pio/host_ui_display.cpp', '.pio/host_ui_screens.cpp',
                '.pio/host_wifi_transport.cpp', '.pio/host_network_service.cpp',
                str(generated), '-o', '.pio/test_firmware.exe'], cwd=root, check=True)
subprocess.run([str(root / '.pio/test_firmware.exe')], cwd=root, check=True)
for name in ('ready', 'stale'):
    snapshot = json.loads((root / f'.pio/status-{name}.json').read_text())
    assert snapshot['api_version'] == 1 and snapshot['device']['role'] == 'ROVER'
    assert snapshot['state']['ready'] == (name == 'ready')
    assert snapshot['link']['rtcm_received_frames'] == 2  # Two actual whole-frame admissions in firmware_cases.
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
