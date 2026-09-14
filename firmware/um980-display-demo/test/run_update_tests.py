"""Portable update foundations; no connected instrument or firmware flashing."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'update-tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'update_notice.exe'
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                str(root / 'test/update_notice_cases.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
