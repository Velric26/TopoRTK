"""Portable update foundations; no connected instrument or firmware flashing."""
from pathlib import Path
import subprocess
import re

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'update-tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'update_notice.exe'
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                str(root / 'test/update_notice_cases.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

source = (root / 'src/ota_service.cpp').read_text(encoding='utf-8')
source = re.sub(r'^#include <(?:Arduino.h|Preferences.h|esp_ota_ops.h|esp_task_wdt.h|mbedtls/sha256.h|freertos/semphr.h)>\n', '', source, flags=re.M)
generated = output / 'ota_service.cpp'
generated.write_text('#include "ota_hardware.h"\n' + source + '\n#include "ota_service_cases.h"\n', encoding='utf-8')
exe = output / 'ota_service.exe'
subprocess.run(['g++', '-std=c++17', '-DTOPORTK_UNIT_ID=1', '-I'+str(root/'test'), '-I'+str(root/'src'),
                '-I'+str(root/'.pio/libdeps/unit_a/ArduinoJson/src'), str(generated), '-o', str(exe)], check=True)
cases = ['stage_deadline', 'stale_success_clock', 'stale_prepare_clock', 'stale_start_clock', 'wrong_target', 'debug_off', 'busy', 'cancel', 'takeover', 'no_ack', 'begin_failure',
         'disconnect', 'timeout', 'write_failure', 'changed_header', 'changed_identity', 'owner_lost',
         'digest_failure', 'end_failure', 'nvs_failure', 'select_failure', 'boot_health', 'boot_failure']
for case in cases:
    subprocess.run([str(exe), case], check=True)
print(f'PASS: production OTA service, {len(cases)} admission/upload/ownership/failure/boot cases (hardware and SHA primitives doubled)')

source = (root / 'src/peer_update.cpp').read_text(encoding='utf-8')
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
generated = output / 'peer_service.cpp'
parts = ['#include "peer_hardware.h"\n#include <string>\n']
for unit, namespace in [(1, 'A'), (2, 'B')]:
    parts += [f'#define TOPORTK_UNIT_ID {unit}\nnamespace {namespace} {{\n',
              f'uint32_t time_ms=100;uint32_t millis(){{return time_ms;}}\n',
              source, '\n}\n#undef TOPORTK_UNIT_ID\n']
generated.write_text(''.join(parts)+'#include "peer_service_cases.h"\n', encoding='utf-8')
exe=output/'peer_service.exe'
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'test'),'-I'+str(root/'src'),str(generated),str(root/'src/pair_session.cpp'),'-o',str(exe)],check=True)
for medium in ('wifi', 'radio'):
    subprocess.run([str(exe), medium], check=True)
