"""Bench the Rover phone AP while switching both units' correction transport.

Requires COM4=Rover, COM10=Base, initially Local Router, and pyserial.
Restores Local Router in finally; never changes receiver roles or passwords.
"""
import json
import threading
import time
import urllib.request
from pathlib import Path
import serial

root = Path(__file__).resolve().parents[3]
record = root/'tests/2026-09-10-rover-phone-wifi'
record.mkdir(parents=True,exist_ok=True)
ports = [serial.Serial(p,115200,timeout=.1) for p in ('COM4','COM10')]
stop = threading.Event()
stage = 'initial'
lines = []
def collect(port):
    while not stop.is_set():
        line = port.readline().decode(errors='replace').strip()
        if line.startswith(('CONFIG:', 'PHONE WIFI:', 'WIFI ROVER:', 'WIFI BASE:', 'PROFILE ', 'ESP32>')):
            lines.append({'stage':stage,'port':port.port,'text':line})
threads = [threading.Thread(target=collect,args=(p,)) for p in ports]
for thread in threads: thread.start()
def commands(command):
    for p in ports: p.write((command+'\n').encode())
def snapshot():
    with urllib.request.urlopen('http://192.168.100.20/api/v1/status',timeout=5) as r:
        return json.load(r)
try:
    time.sleep(6) # Allow USB-open/reset or an in-progress profile to settle.
    commands('config?')
    before = snapshot()
    assert before['device']['profile']=='VERIFIED' and before['phone_wifi']['available']
    stage = 'direct'
    commands('wifi direct')
    time.sleep(14)
    commands('config?'); ports[0].write(b'phone?\n')
    time.sleep(1)
    rover = [x['text'] for x in lines if x['stage']=='direct' and x['port']=='COM4']
    assert any('transport=DIRECT LINK link=UP' in x and 'bad=0' in x and 'gap=0' in x for x in rover)
    assert any('PHONE WIFI: READY' in x and 'IP=192.168.8.1' in x for x in rover)
    stage = 'restored'
    commands('wifi local')
    time.sleep(14)
    commands('config?'); ports[0].write(b'phone?\n')
    after = snapshot()
    assert after['link']['connected'] and after['link']['transport']=='LOCAL ROUTER'
    assert after['phone_wifi']['available'] and after['phone_wifi']['ssid']==before['phone_wifi']['ssid']
    assert after['device']==before['device']
    assert not any(x['text'].startswith(('PROFILE ','ESP32>')) for x in lines if x['stage']!='initial')
    result={'result':'PASS','before':before,'after':after,'serial':lines}
    (record/'transport-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: Direct Link with Rover phone AP; restored Local Router; no receiver reconfiguration')
finally:
    commands('wifi local')
    stop.set()
    for thread in threads: thread.join()
    for p in ports: p.close()
