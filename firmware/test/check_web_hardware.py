"""Read-only bench acceptance. Requires pyserial and an already-flashed Rover.

Usage: python test/check_web_hardware.py http://192.168.100.20 COM4 COM10
No role, network, receiver or storage settings are changed.
"""
import json
import os
import socket
import sys
import threading
import time
import urllib.error
import urllib.request
from urllib.parse import urlparse
from pathlib import Path
import serial

url, *ports = sys.argv[1:]
url = url.rstrip('/')
record = Path(__file__).resolve().parents[3] / os.environ.get('TOPORTK_TEST_RECORD','tests/2026-09-10-rover-web-status')
record.mkdir(parents=True, exist_ok=True)
stop = threading.Event()
lines = {port: [] for port in ports}
devices = [serial.Serial(port, 115200, timeout=.1) for port in ports]
def collect(device):
    while not stop.is_set():
        line = device.readline().decode(errors='replace').strip()
        # Exclude raw receiver messages, identifiers and Wi-Fi credentials.
        if line.startswith(('CONFIG:', 'ESP32>', 'PROFILE ', 'SD:', 'RTCM STATUS:')):
            lines[device.port].append(line)
threads = [threading.Thread(target=collect, args=(d,)) for d in devices]
for thread in threads: thread.start()
def query_config():
    for d in devices: d.write(b'config?\nrtcm?\n')
    time.sleep(.5)
def get(path):
    with urllib.request.urlopen(url + path, timeout=5) as response:
        assert response.headers['Cache-Control'] == 'no-store'
        return response.read()
def status():
    s = json.loads(get('/api/v1/status'))
    assert s['device']['role'] == 'ROVER' and s['device']['profile'] == 'VERIFIED'
    assert s['link']['connected'] and s['link']['transport'] == 'LOCAL ROUTER'
    return s
try:
    # Opening USB can coincide with startup/recovery; check the precondition
    # before beginning the measured refresh window and config comparison.
    for attempt in range(15):
        try:
            status()
            break
        except (AssertionError, OSError):
            if attempt == 14: raise
            time.sleep(1)
    query_config()
    before = status()
    assert get('/') == get('/ui/v1/')
    rejections = {}
    for method, path, expected in [('POST','/api/v1/status',405), ('PUT','/api/v1/status',405),
                                   ('DELETE','/',405), ('GET','/api/v1/config',404)]:
        request = urllib.request.Request(url+path, method=method)
        try:
            urllib.request.urlopen(request, timeout=5)
            raise AssertionError(f'{method} {path} accepted')
        except urllib.error.HTTPError as error:
            assert error.code == expected
            if expected == 405: assert error.headers['Allow'] == 'GET'
            rejections[method+' '+path] = {'status':error.code, 'body':json.loads(error.read())}
    samples = []
    for _ in range(30):
        assert b'TopoRTK' in get('/')
        samples.append(status())
        time.sleep(1)
    # An incomplete HTTP request occupies the HTTP worker until its timeout;
    # meanwhile firmware sampling and the UDP peer heartbeat must continue.
    host = urlparse(url).hostname
    with socket.create_connection((host,80), timeout=5) as slow:
        slow.sendall(b'GET /api/v1/status HTTP/1.1\r\nHost: rover\r\n')
        time.sleep(3)
    after = status()
    query_config()
    assert after['boot_id'] == before['boot_id'], 'unexpected reboot'
    assert after['uptime_ms'] > before['uptime_ms'] + 30000
    assert after['link']['received_packets'] > before['link']['received_packets'] + 20
    for s in samples + [after]:
        assert s['link']['sequence_gaps'] == before['link']['sequence_gaps']
        assert s['link']['invalid_packets'] == before['link']['invalid_packets']
    assert len({s['uptime_ms'] for s in samples}) == len(samples)
    for port in ports:
        configs = [line for line in lines[port] if line.startswith('CONFIG:')]
        assert len(configs) == 2 and configs[0] == configs[1], (port, configs)
        assert not any(line.startswith(('ESP32>', 'PROFILE ')) for line in lines[port])
    result = {'result':'PASS', 'page_refreshes':30, 'write_rejections':rejections,
              'slow_request_seconds':3, 'before':before, 'after':after, 'serial':lines}
    (record/'hardware-results.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
finally:
    stop.set()
    for thread in threads: thread.join()
    for d in devices: d.close()
