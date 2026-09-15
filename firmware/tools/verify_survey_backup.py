"""Verify a downloaded TopoRTK job journal without modifying it or an instrument."""
import argparse
import json
from pathlib import Path
import re
import zlib


def verify(path):
    if path.stat().st_size > 20 * 1024 * 1024:
        raise ValueError('Backup exceeds the prototype size limit')
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('format') != 'TopoRTK job journal' or data.get('version') != 1:
        raise ValueError('Unsupported backup format/version')
    job = data.get('job', '')
    if not re.fullmatch('[0-9a-f]{32}', job):
        raise ValueError('Invalid job ID')
    records, at = data.get('records'), data.get('at')
    if not isinstance(records, list) or not 1 <= len(records) <= 1024 or type(at) is not int or not 1 <= at <= 1024:
        raise ValueError('Invalid record count or snapshot sequence')
    previous, observations, edits, last_revision = 0, 0, 0, 0
    for index, row in enumerate(records):
        sequence, raw, checksum = row.get('sequence'), row.get('json'), row.get('crc32')
        if type(sequence) is not int or not previous < sequence <= at:
            raise ValueError('Record sequence is unordered or outside the snapshot')
        if not isinstance(raw, str) or not 1 <= len(raw.encode('utf-8')) <= 8192:
            raise ValueError('Invalid record size')
        if type(checksum) is not int or zlib.crc32(raw.encode('utf-8')) != checksum:
            raise ValueError(f'Checksum mismatch in record {sequence}')
        event = json.loads(raw)
        if event.get('job') != job or event.get('version') != 1:
            raise ValueError('Record belongs to another job/version')
        if index == 0 and (event.get('op') != 'job.create' or event.get('id') != job):
            raise ValueError('Backup does not start with its job creation')
        if event.get('op') == 'job.configure':
            if event.get('revision') != last_revision + 1:
                raise ValueError('Configuration revision gap')
            last_revision += 1
        observations += event.get('op') == 'point.saved'
        edits += event.get('op') == 'point.edit'
        previous = sequence
    if last_revision != data.get('revision'):
        raise ValueError('Backup header does not match its configuration history')
    return {'job': job, 'records': len(records), 'observations': observations,
            'metadata_edits': edits, 'configuration_revision': last_revision}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.backup), indent=2))
        print('Checksums and journal envelope verified. No instrument data changed.')
    except (OSError, ValueError, TypeError, AttributeError) as error:
        parser.exit(1, f'Backup verification failed: {error}\n')
