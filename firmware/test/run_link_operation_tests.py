"""Compile the portable production link-operation owner; no hardware access."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'link-operation-tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'link_operation.exe'
subprocess.run([
    'g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O2',
    '-I' + str(root / 'src'), str(root / 'src/link_operation.cpp'),
    str(root / 'test/link_operation_cases.cpp'), '-o', str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
