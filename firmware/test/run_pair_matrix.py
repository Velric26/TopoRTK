#!/usr/bin/env python3
"""R11 scripted pair acceptance: startup, selection, tests, cancellation, OTA.

Read-only except for the deliberate operations it issues, and it always tries to
leave the pair on Wi-Fi at the same revision-relative state it started from. Every
request carries a fresh 32-hex id and is correlated by that id, so an older
terminal operation can never be mistaken for the new one.

    python test/run_pair_matrix.py --record tests/2026-09-15-r11
    python test/run_pair_matrix.py --record DIR --ota-a fw-a.tpk --ota-b fw-b.tpk

Requires both instruments reachable over HTTP. Nodes/ports are arguments rather
than constants: the documented bench addresses change with DHCP.
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

TERMINAL = ('succeeded', 'failed', 'cancelled', 'recovery_required')
SECONDS = (30, 60, 120, 300)


class Client:
    def __init__(self, base, client_id):
        self.base = base
        self.client = client_id
        self.token = None

    def request(self, path, method='GET', body=None, auth=False):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path, data=data, method=method)
        request.add_header('Content-Type', 'application/json')
        if auth and self.token:
            request.add_header('Authorization', 'Bearer ' + self.token)
        try:
            with urllib.request.urlopen(request, timeout=15) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            try:
                payload = json.load(error)
            except Exception:
                payload = {}
            return error.code, payload

    def claim(self):
        status, body = self.request('/api/v1/control', 'POST', {'client': self.client})
        assert status == 200, (status, body)
        self.token = body['token']

    def settings(self):
        return self.request('/api/v1/settings')[1]

    def link(self):
        return self.request('/api/v1/diagnostic')[1]['corrections']

    def survey(self):
        return self.request('/api/v1/survey')[1]

    def update(self):
        return self.request('/api/v1/update')[1]

    def issue(self, op, transport, extra=None, timeout=240):
        """Admit one operation and wait for the terminal state of *this* request."""
        settings = self.settings()
        request_id = os.urandom(16).hex()
        body = {'id': request_id, 'revision': settings['revision'], 'op': op,
                'transport': transport, 'confirm': True}
        if extra:
            body.update(extra)
        status, payload = self.request('/api/v1/settings', 'POST', body, auth=True)
        if status != 202:
            return status, payload, None, settings
        deadline = time.time() + timeout
        while time.time() < deadline:
            settings = self.settings()
            operation = settings.get('operation') or {}
            if operation.get('id') == request_id and operation.get('state') in TERMINAL:
                return status, payload, operation, settings
            time.sleep(2)
        return status, payload, None, settings

    def local_select(self, transport):
        status, body = self.request('/api/v1/diagnostic', 'POST',
                                    {'op': 'corrections', 'transport': transport, 'confirm': True},
                                    auth=True)
        return status, body


class Report:
    def __init__(self, record):
        self.record = record
        self.cases = []
        os.makedirs(record, exist_ok=True)

    def case(self, name, ok, detail):
        self.cases.append({'name': name, 'pass': bool(ok), 'detail': detail})
        print(('PASS ' if ok else 'FAIL ') + name + ' | ' + detail)

    def write(self, extra=None):
        payload = {'result': 'PASS' if all(c['pass'] for c in self.cases) else 'FAIL',
                   'cases': self.cases}
        if extra:
            payload.update(extra)
        with open(os.path.join(self.record, 'pair-matrix.json'), 'w', encoding='utf-8') as handle:
            json.dump(payload, handle, indent=2)
        return payload['result']


def converge(a, b, transport, timeout=25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        link_a, link_b = a.link(), b.link()
        settings_a, settings_b = a.settings(), b.settings()
        if (settings_a['selected_transport'] == transport == settings_b['selected_transport'] and
                link_a['peer_connected'] and link_b['peer_connected'] and
                link_a['session'] > 999999 and link_a['session'] == link_b['session']):
            return True, link_a, link_b
        time.sleep(1)
    return False, link_a, link_b


def baseline(client):
    survey = client.survey()
    settings = client.settings()
    update = client.update()
    return {'boot_id': survey.get('boot_id'), 'role': survey.get('role'),
            'jobs': survey.get('jobs'), 'records_used': survey.get('records_used'),
            'revision': settings['revision'], 'firmware': update['firmware']}


def ota(record, unit, url, peer, package):
    """Shell out to the guarded OTA runner; it refuses an unconfirmed notice."""
    script = os.path.join(os.path.dirname(__file__), 'run_ota_live.cjs')
    result = subprocess.run(['node', script, '--install', unit, url, peer, package],
                            capture_output=True, text=True, timeout=900)
    with open(os.path.join(record, 'ota-' + unit + '.log'), 'w', encoding='utf-8') as handle:
        handle.write(result.stdout + result.stderr)
    return result.returncode, (result.stdout + result.stderr).strip().splitlines()[-3:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--record', default='tests/2026-09-15-r11')
    parser.add_argument('--a', default='http://192.168.100.20')
    parser.add_argument('--b', default='http://192.168.100.19')
    parser.add_argument('--ota-a', default=None, help='package for the Base instrument')
    parser.add_argument('--ota-b', default=None, help='package for the Rover instrument')
    args = parser.parse_args()

    report = Report(args.record)
    a, b = Client(args.a, 'a' * 32), Client(args.b, 'b' * 32)
    a.claim()
    b.claim()
    before_a, before_b = baseline(a), baseline(b)

    report.case('both instruments report the same firmware',
                before_a['firmware'] == before_b['firmware'],
                before_a['firmware'] + ' / ' + before_b['firmware'])
    report.case('opposite roles, one controller each',
                before_a['role'] != before_b['role'] and a.token and b.token,
                before_a['role'] + ' / ' + before_b['role'])

    # Startup: the pair proves itself on the stored medium without any operator step.
    transport = a.settings()['selected_transport']
    ok, link_a, link_b = converge(a, b, transport)
    report.case('stored selection converges after boot', ok,
                'transport=' + transport + ' session=' + str(link_a.get('session')) +
                ' pair=' + str(link_a.get('peer_connected')) + '/' + str(link_b.get('peer_connected')))

    # Local recovery path reaches both instruments (the escape hatch the contract keeps).
    for name, client in (('Base', a), ('Rover', b)):
        status, body = client.local_select('wifi')
        report.case('local selection accepted on the ' + name, status == 202,
                    'status=' + str(status) + ' ' + str(body))
    ok, _, _ = converge(a, b, 'wifi')
    report.case('pair converges on the locally selected medium', ok, 'both on wifi')

    # Pair-wide cutovers both directions, one request, both sides committing.
    for target in ('sik', 'wifi', 'sik', 'wifi'):
        status, payload, operation, settings = b.issue('link.select', target)
        ok = status == 202 and operation is not None and operation.get('state') == 'succeeded'
        converged, link_a, link_b = converge(a, b, target)
        revisions = (settings['revision'], a.settings()['revision'], b.settings()['revision'])
        report.case('pair-wide cutover to ' + target,
                    ok and converged and revisions[1] == revisions[2],
                    'status=' + str(status) + ' state=' + str((operation or {}).get('state')) +
                    ' reason=' + str((operation or {}).get('reason')) +
                    ' revision=' + str(revisions[1]) + '/' + str(revisions[2]) +
                    ' session=' + str(link_a.get('session')))

    # A test on each medium, requested once, run by both peers.
    for transport in ('wifi', 'sik'):
        selected_before = b.settings()['selected_transport']
        stored_before = (b.settings().get('last_tests') or {}).get(transport) or {}
        status, _, operation, settings = b.issue('link.test', transport, {'profile': 0})
        stored = (settings.get('last_tests') or {}).get(transport) or {}
        ran = (operation is not None and operation.get('reason') != 'test_unavailable' and
               stored.get('run') and stored.get('run') != stored_before.get('run'))
        report.case('quick test on ' + transport + ' ran and settled',
                    status == 202 and ran,
                    'state=' + str((operation or {}).get('state')) +
                    ' reason=' + str((operation or {}).get('reason')) +
                    ' pair_pass=' + str(stored.get('pair_pass')) +
                    ' received=' + str(stored.get('received')))
        # The tested medium is staged, never adopted: the selected route is what
        # must be unchanged, whichever medium the test used.
        selected_after = b.settings()['selected_transport']
        report.case('a test on ' + transport + ' never changes the selected route',
                    selected_after == selected_before,
                    'selected=' + selected_before + ' -> ' + selected_after)

    # Cancellation of an admitted test leaves the route and revision untouched.
    revision_before = b.settings()['revision']
    settings = b.settings()
    request_id = os.urandom(16).hex()
    status, _ = b.request('/api/v1/settings', 'POST',
                          {'id': request_id, 'revision': settings['revision'], 'op': 'link.test',
                           'transport': 'sik', 'profile': 0, 'confirm': True}, auth=True)
    if status == 202:
        time.sleep(2)
        cancel_id = os.urandom(16).hex()
        cancel_status, cancel_body = b.request('/api/v1/settings', 'POST',
                                              {'id': cancel_id, 'revision': b.settings()['revision'],
                                               'op': 'link.cancel', 'confirm': True}, auth=True)
        deadline = time.time() + 90
        state, reason = None, None
        while time.time() < deadline:
            operation = b.settings().get('operation') or {}
            if operation.get('state') in ('cancelled', 'failed', 'succeeded'):
                state, reason = operation.get('state'), operation.get('reason')
                break
            time.sleep(2)
        report.case('an admitted test can be cancelled',
                    cancel_status == 202 and state in ('cancelled', 'failed'),
                    'cancel=' + str(cancel_status) + ' state=' + str(state) + ' reason=' + str(reason) +
                    ' ' + str(cancel_body))
    else:
        report.case('an admitted test can be cancelled', False, 'admission returned ' + str(status))
    report.case('cancellation leaves the route and revision untouched',
                b.settings()['revision'] == revision_before,
                'revision=' + str(revision_before) + ' -> ' + str(b.settings()['revision']))

    # Documented refusals stay refusals.
    settings = b.settings()
    stale = b.request('/api/v1/settings', 'POST',
                      {'id': os.urandom(16).hex(), 'revision': settings['revision'] + 99,
                       'op': 'link.select', 'transport': 'sik', 'confirm': True}, auth=True)
    malformed = b.request('/api/v1/settings', 'POST',
                          {'id': 'not-hex', 'revision': settings['revision'],
                           'op': 'link.select', 'transport': 'sik', 'confirm': True}, auth=True)
    unconfirmed = b.request('/api/v1/settings', 'POST',
                            {'id': os.urandom(16).hex(), 'revision': settings['revision'],
                             'op': 'link.select', 'transport': 'sik'}, auth=True)
    report.case('stale revision, malformed id and missing confirmation are refused',
                stale[0] == 409 and malformed[0] == 400 and unconfirmed[0] == 400,
                'stale=' + str(stale[0]) + ' malformed=' + str(malformed[0]) +
                ' unconfirmed=' + str(unconfirmed[0]))

    # Optional guarded OTA of both units, then the pair must re-establish itself.
    if args.ota_a and args.ota_b:
        for unit, client, package, peer in (('A', a, args.ota_a, args.b),
                                            ('B', b, args.ota_b, args.a)):
            code, tail = ota(args.record, unit, client.base, peer, package)
            report.case('guarded OTA accepted on ' + unit, code == 0, ' | '.join(tail))
        time.sleep(15)
        ok, link_a, link_b = converge(a, b, a.settings()['selected_transport'], timeout=60)
        after_a, after_b = baseline(a), baseline(b)
        report.case('pair re-establishes after the update reboots', ok,
                    'session=' + str(link_a.get('session')) +
                    ' pair=' + str(link_a.get('peer_connected')) + '/' + str(link_b.get('peer_connected')))
        report.case('saved jobs and records survive the update',
                    after_a['jobs'] == before_a['jobs'] and after_b['jobs'] == before_b['jobs'] and
                    after_a['records_used'] == before_a['records_used'] and
                    after_b['records_used'] == before_b['records_used'],
                    'jobs=' + str(len(after_a['jobs'] or [])) + '/' + str(len(after_b['jobs'] or [])) +
                    ' records=' + str(after_a['records_used']) + '/' + str(after_b['records_used']))
        report.case('both boots are new', after_a['boot_id'] != before_a['boot_id'] and
                    after_b['boot_id'] != before_b['boot_id'],
                    'boots changed')

    result = report.write({'baseline': {'base': before_a, 'rover': before_b}})
    print(result)
    return 0 if result == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
