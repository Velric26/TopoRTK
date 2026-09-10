"""Read-only HTTP/USB presence soak; no serial open, writes, or credentials.
Usage: platformio-python test/check_survey_hardware.py [seconds, default 360]
"""
import json,sys,time,urllib.request,urllib.error
from pathlib import Path
from serial.tools import list_ports
out=Path(__file__).resolve().parents[3]/'tests/2026-09-10-survey-workflow'
out.mkdir(parents=True,exist_ok=True)
start=time.monotonic();duration=int(sys.argv[1]) if len(sys.argv)>1 else 360
samples=[];errors=[];rejections={}
def get(ip,path):
    with urllib.request.urlopen('http://'+ip+path,timeout=3) as r:
        assert r.headers['Cache-Control']=='no-store'
        return r.read()
for ip in ['192.168.100.20','192.168.100.19']:
    for path,origin,expected in [('/api/v1/command',None,401),('/api/v1/command','http://untrusted.invalid',403),('/api/v1/control','http://untrusted.invalid',403)]:
        request=urllib.request.Request('http://'+ip+path,data=b'{}',headers={'Content-Type':'application/json',**({'Origin':origin} if origin else {})})
        try:urllib.request.urlopen(request,timeout=3);raise AssertionError('unauthorized write accepted')
        except urllib.error.HTTPError as e:assert e.code==expected;rejections[ip+path+str(origin)]=e.code
while time.monotonic()-start<duration:
    row={'elapsed_s':round(time.monotonic()-start,1),'ports':[p.device for p in list_ports.comports() if p.device in ['COM4','COM10']]}
    for label,ip in [('A','192.168.100.20'),('B','192.168.100.19')]:
        try:
            state=json.loads(get(ip,'/api/v1/survey'))
            row[label]={k:state[k] for k in ['boot_id','uptime_ms','storage_ready','reset_reason','free_heap','min_heap','free_psram','records_used']}
            row[label]['profile_verified']=state['gnss']['profile_verified']
            assert state['storage_ready'] and state['gnss']['profile_verified']
            assert state['free_heap']>50000
            assert b'TopoRTK' in get(ip,'/survey')
            if label=='A':
                status=json.loads(get(ip,'/api/v1/status'));row[label]['packets']=status['link']['received_packets'];row[label]['connected']=status['link']['connected']
        except Exception as e:errors.append({'elapsed_s':row['elapsed_s'],'unit':label,'error':type(e).__name__})
    samples.append(row)
    if len(samples)%12==0:print(f"Soak {row['elapsed_s']:.0f}s: ports={row['ports']}, HTTP errors={len(errors)}",flush=True)
    time.sleep(5)
report={'samples':samples,'errors':errors,'write_rejections':rejections}
(out/'hardware-soak.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'samples':len(samples),'errors':len(errors),'ports_continuously_present':all(len(s['ports'])==2 for s in samples),'boot_ids':{u:sorted(set(s[u]['boot_id'] for s in samples if u in s)) for u in ['A','B']}}),flush=True)
