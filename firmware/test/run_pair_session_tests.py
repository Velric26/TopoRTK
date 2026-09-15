"""Compile the portable production pair owner, without firmware or hardware access."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / '.pio' / 'pair-session-tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'pair_session.exe'
subprocess.run([
    'g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O2',
    '-I' + str(root / 'src'), str(root / 'src/pair_session.cpp'),
    str(root / 'test/pair_session_cases.cpp'), '-o', str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
