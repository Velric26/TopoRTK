"""Focused link-service cases; no hardware access, no flashing.

Compiles the unmodified production link service (plus the portable pair and
operation cores) against hardware doubles, following run_update_tests.py.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'link-service-tests'
output.mkdir(parents=True, exist_ok=True)
arduino_json = root / '.pio' / 'libdeps' / 'unit_a' / 'ArduinoJson' / 'src'

source = (root / 'src/link_service.cpp').read_text(encoding='utf-8')
source = re.sub(r'^#include <(?:Arduino|Preferences|esp_system)\.h>\n', '', source, flags=re.M)
generated = (output / 'link_service_service.cpp')
generated.write_text('#include "link_service_hardware.h"\n' + source, encoding='utf-8')

# The adapter headers take IPAddress from <IPAddress.h> when the toolchain has
# one; the double stands in for it so the service compiles unmodified.
(output / 'IPAddress.h').write_text('#pragma once\n#include "link_service_hardware.h"\n', encoding='utf-8')

exe = output / 'link_service.exe'
# -Wno-misleading-indentation: src/update_package.h and src/survey_math.h, pulled
# in by the service's own includes, carry pre-existing one-line statements that
# this harness has no business rewriting.
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-O1',
                '-DTOPORTK_UNIT_ID=1',
                '-I' + str(output), '-I' + str(root / 'test'), '-I' + str(root / 'src'),
                '-I' + str(arduino_json),
                str(generated), str(root / 'src/link_operation.cpp'), str(root / 'src/pair_session.cpp'),
                str(root / 'test/link_service_cases.cpp'), '-o', str(exe)], check=True)

cases = ['missing_record_keeps_wifi_default',
         'legacy_radio_migration',
         'invalid_record:confirmed:corrupt', 'invalid_record:confirmed:truncated',
         'invalid_record:confirmed:oversized',
         'invalid_record:selection:corrupt', 'invalid_record:selection:truncated',
         'invalid_record:selection:oversized',
         'invalid_record:pending:corrupt', 'invalid_record:pending:truncated',
         'invalid_record:pending:oversized',
         'valid_pending_masks_nothing',
         'migration_write_failure',
         'radio_reservation_published',
         'cross_medium_ingress',
         'incompatible_version',
         'radio_fault_isolation',
         'test_shape_published']
for case in cases:
    subprocess.run([str(exe), *case.split(':')], check=True)
print(f'PASS: production link service, {len(cases)} stored-selection/ingress/version/fault cases '
      '(hardware, NVS and both portable cores)')
