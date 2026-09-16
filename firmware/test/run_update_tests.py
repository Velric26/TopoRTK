"""Portable update foundations; no connected instrument or firmware flashing.

Both services are compiled from src/ as they are: the platform headers
src/ota_service.cpp and src/peer_update.cpp include resolve through
test/ota_adapter/ and test/peer_adapter/, whose headers forward to this
harness's doubles. Nothing rewrites the text under review.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'update-tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'update_notice.exe'
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                str(root / 'test/update_notice_cases.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

# The service is included, not copied: src/ota_service.cpp keeps its own
# #includes and test/ota_adapter/Arduino.h and its siblings satisfy the platform
# ones by forwarding to this harness's OTA double.
generated = output / 'ota_service.cpp'
generated.write_text('#include "ota_hardware.h"\n'
                     '#include "../../src/ota_service.cpp"\n'
                     '#include "ota_service_cases.h"\n', encoding='utf-8')
exe = output / 'ota_service.exe'
subprocess.run(['g++', '-std=c++17', '-DTOPORTK_UNIT_ID=1',
                '-I'+str(root/'test/ota_adapter'), '-I'+str(root/'test'), '-I'+str(root/'src'),
                '-I'+str(root/'.pio/libdeps/unit_a/ArduinoJson/src'), str(generated), '-o', str(exe)], check=True)
cases = ['stage_deadline', 'stale_success_clock', 'stale_prepare_clock', 'stale_start_clock', 'wrong_target', 'debug_off', 'busy', 'cancel', 'takeover', 'no_ack', 'begin_failure',
         'disconnect', 'timeout', 'write_failure', 'changed_header', 'changed_identity', 'owner_lost',
         'digest_failure', 'end_failure', 'nvs_failure', 'select_failure', 'boot_health', 'boot_failure']
for case in cases:
    subprocess.run([str(exe), case], check=True)
print(f'PASS: production OTA service, {len(cases)} admission/upload/ownership/failure/boot cases (hardware and SHA primitives doubled)')

# One process runs both units, so src/peer_update.cpp is included twice, once per
# namespace, and its platform includes are satisfied at global scope first (a
# guarded header entered from inside a namespace would declare its contents in
# that namespace). `prelude` is that list, `prepared` also names the source's own
# headers, which peer_hardware.h brings in, and the assert keeps the list
# complete when src/peer_update.cpp gains an include.
prelude = ('<Arduino.h>', '<cstdio>', '<cstring>')
prepared = set(prelude) | {'"peer_update.h"', '"pair_session.h"'}
peer_source = (root / 'src/peer_update.cpp').read_text(encoding='utf-8')
declared = {line.strip()[len('#include '):] for line in peer_source.splitlines()
            if line.strip().startswith('#include')}
assert declared <= prepared, (
    f'add {sorted(declared - prepared)} to the prelude: an include opened inside '
    'the namespace would declare its contents there')
generated = output / 'peer_service.cpp'
parts = ['#include "peer_hardware.h"\n',
         ''.join(f'#include {name}\n' for name in prelude), '#include <string>\n']
for unit, namespace in [(1, 'A'), (2, 'B')]:
    parts += [f'#define TOPORTK_UNIT_ID {unit}\nnamespace {namespace} {{\n',
              f'uint32_t time_ms=100;uint32_t millis(){{return time_ms;}}\n',
              '#include "../../src/peer_update.cpp"\n', '\n}\n#undef TOPORTK_UNIT_ID\n']
generated.write_text(''.join(parts)+'#include "peer_service_cases.h"\n', encoding='utf-8')
exe=output/'peer_service.exe'
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'test/peer_adapter'),'-I'+str(root/'test'),'-I'+str(root/'src'),str(generated),str(root/'src/pair_session.cpp'),'-o',str(exe)],check=True)
for medium in ('wifi', 'radio'):
    subprocess.run([str(exe), medium], check=True)
