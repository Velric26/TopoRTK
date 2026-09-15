"""Validate browser downloads with Python's independent CSV and CRC readers."""
import csv
import os
import json
from pathlib import Path
import zlib
import importlib.util
import tempfile

folder = Path(__file__).resolve().parents[3] / os.environ.get('TOPORTK_TEST_RECORD', 'tests/2026-09-10-roadmap-6-12')
backup = json.loads((folder / 'fixture-backup.json').read_text(encoding='utf-8'))
assert backup['format'] == 'TopoRTK job journal' and backup['version'] == 1
events = []
previous = 0
for record in backup['records']:
    assert previous < record['sequence'] <= backup['at']
    previous = record['sequence']
    assert zlib.crc32(record['json'].encode('utf-8')) == record['crc32']
    event = json.loads(record['json'])
    assert event['job'] == backup['job']
    events.append(event)
with (folder / 'fixture-points.csv').open(encoding='utf-8', newline='') as stream:
    points = list(csv.DictReader(stream))
assert len(points) == 1
row = points[0]
point = json.loads(row['observation_json'])
assert row['code'] == point['code'] == 'BM-EDIT'
assert point['description'] == '=1+1, "survey note"'
assert row['description'] == "'" + point['description']
assert float(row['height']) == point['height']
assert row['units'] == point['configuration']['units'] == 'm'
assert point['configuration']['zone'] == 14
original = next(e['point'] for e in events if e['op'] == 'point.saved')
def equivalent(a, b):
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, (float, int)) and not isinstance(a, bool):
        return abs(a - b) <= 1e-8
    return a == b
for key in ('easting', 'northing', 'height', 'configuration', 'samples', 'fix'):
    assert equivalent(original[key], point[key])
assert point['edit_revision'] == 4 and point['deleted'] is False
print('PASS: independent CSV quoting/formula protection, full observation round-trip, journal CRC and audit history')
spec = importlib.util.spec_from_file_location('backup_verifier', Path(__file__).resolve().parents[1] / 'tools/verify_survey_backup.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
assert verifier.verify(folder / 'fixture-backup.json')['metadata_edits'] == 3
for change in ('crc', 'order', 'revision', 'missing_creation'):
    bad = json.loads(json.dumps(backup))
    if change == 'crc': bad['records'][0]['crc32'] ^= 1
    if change == 'order': bad['records'][1]['sequence'] = bad['records'][0]['sequence']
    if change == 'revision': bad['revision'] += 1
    if change == 'missing_creation': bad['records'].pop(0)
    with tempfile.TemporaryDirectory() as temporary:
        candidate = Path(temporary) / 'bad.json'
        candidate.write_text(json.dumps(bad), encoding='utf-8')
        try: verifier.verify(candidate)
        except ValueError: pass
        else: raise AssertionError('Accepted invalid backup: ' + change)
print('PASS: backup verifier rejects corruption, reordering, revision mismatch and missing creation')

report = folder / 'fixture-checks.csv'
if report.exists():
    with report.open(encoding='utf-8', newline='') as stream:
        rows = list(csv.DictReader(stream))
    checks = [r for r in rows if r['comparison_purpose']]
    assert len(checks) == 3
    assert {r['comparison_purpose'] for r in checks} == {'check', 'repeat', 'stake'}
    for row in checks:
        comp = json.loads(row['observation_json'])['comparison']
        assert float(row['delta_h']) == comp['delta_h']
        assert row['within_tolerance'] == 'true'
    print('PASS: readable check/repeat report matches saved comparison records')

line_report = folder / 'fixture-lines.csv'
if line_report.exists():
    with line_report.open(encoding='utf-8', newline='') as stream:
        vertices = [r for r in csv.DictReader(stream) if r['line_id']]
    assert len(vertices) == 4
    chain = [r for r in vertices if r['line_id'] == 'LINE1']
    assert [r['line_action'] for r in chain] == ['start', 'continue', 'end']
    assert [r['line_previous'] for r in chain] == ['', 'LV1', 'LV2']
    assert all(r['line_state'] == 'broken' and r['line_code'] == 'EDGE' for r in vertices)
    assert all(r['deleted'] == 'false' for r in vertices)
    for row in vertices:
        saved = json.loads(row['observation_json'])
        assert row['line_id'] == saved['line_id'] and row['line_action'] == saved['line_action']
    line_backup = json.loads((folder / 'fixture-lines-backup.json').read_text(encoding='utf-8'))
    events = [json.loads(r['json']) for r in line_backup['records']]
    assert len([e for e in events if e['op'] == 'collect.stop' and e.get('line', {}).get('id') == 'GAP']) == 1
    verifier.verify(folder / 'fixture-lines-backup.json')
    print('PASS: line CSV topology, retained original vertices, persistent breaks and gap audit backup')
