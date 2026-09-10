"""Native storage/collection tests and independent PROJ projection comparison.
Install the optional oracle with: python -m pip install --target .pio/proj-test pyproj
"""
from pathlib import Path
import subprocess, tempfile, sys, csv, io, math
root=Path(__file__).resolve().parents[1]
subprocess.run(['g++','-std=c++11','-Isrc','-I.pio/libdeps/unit_a/ArduinoJson/src','test/test_survey.cpp','src/survey_engine.cpp','src/survey_store.cpp','-o','.pio/test_survey.exe'],cwd=root,check=True)
with tempfile.TemporaryDirectory(prefix='survey-',dir=root/'.pio') as folder:
    subprocess.run([str(root/'.pio/test_survey.exe'),folder],cwd=root,check=True)
sys.path.insert(0,str(root/'.pio/proj-test'))
from pyproj import Transformer
rows=subprocess.check_output([str(root/'.pio/test_survey.exe'),'--utm'],text=True)
worst=0
for zone,lat,lon,e,n in csv.reader(io.StringIO(rows)):
    zone=int(zone);lat,lon,e,n=map(float,(lat,lon,e,n))
    expected=Transformer.from_crs(4326,(32700 if lat<0 else 32600)+zone,always_xy=True).transform(lon,lat)
    error=math.hypot(e-expected[0],n-expected[1]);worst=max(worst,error)
    assert error<.002,(zone,lat,lon,error)
print(f'PASS: 96 independent PROJ UTM examples, north/south/zone edges; worst difference {worst*1000:.3f} mm')
