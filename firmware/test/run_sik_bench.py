#!/usr/bin/env python3
"""SiK bench driver: radio probes, one device-owned paired radio test requested
through the settings API, automatic live SiK pairing and correction-counter
observation over the instrument HTTP APIs. No firmware flashing, no receiver
commands outside the existing profiles, and no test code typed into two pages.

Usage:
  python run_sik_bench.py --base http://192.168.100.20 --rover http://192.168.100.19 \
      [--seconds 120] [--skip-probe] [--skip-pairtest] [--pair-seconds 30] [--profile clean]
"""
import argparse, json, os, random, time, urllib.request, urllib.error
from datetime import datetime, timezone

def req(base, path, method="GET", body=None, token=None, timeout=5):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(base + path, data=data, method=method)
    if body is not None: r.add_header("Content-Type", "application/json")
    if token: r.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        try: return e.code, json.loads(e.read().decode())
        except Exception: return e.code, {}

def claim(base):
    st, d = req(base, "/api/v1/control", "POST", {"client": "%032x" % random.getrandbits(128)})
    assert st == 200, f"claim failed on {base}: {st} {d}"
    return d["token"]

def probe(base, token):
    st, _ = req(base, "/api/v1/diagnostic", "POST", {"op": "probe", "confirm": True}, token)
    assert st == 202, f"probe rejected on {base}: {st}"
    start = time.time()
    # The probe sequence runs ~4.8 s on the instrument; 'radio_probe' can show a
    # final-looking value from a partial capture, so wait out the full window.
    while time.time() - start < 6:
        st, d = req(base, "/api/v1/diagnostic")
        if d.get("radio_probe") == "checking" or time.time() - start < 5.5:
            time.sleep(0.5); continue
        return d.get("radio_probe"), d.get("radio_probe_response", "")
    return "timeout", ""

def snapshot(base):
    st, d = req(base, "/api/v1/diagnostic")
    cor = d.get("corrections") or {}
    st2, dbg = req(base, "/api/v1/debug")
    cor["peer_status"] = dbg.get("peer_status", "")
    return cor

def select_pair(base, base_token, rover, rover_token, transport):
    for name, url, token in (("base", base, base_token), ("rover", rover, rover_token)):
        status, result = req(url, "/api/v1/diagnostic", "POST",
                             {"op": "corrections", "transport": transport, "confirm": True}, token)
        assert status == 202, f"{name} local {transport} selection rejected: {status} {result}"
    deadline = time.monotonic() + 20
    latest = {}
    while time.monotonic() < deadline:
        for name, url in (("base", base), ("rover", rover)):
            status, result = req(url, "/api/v1/diagnostic")
            assert status == 200, f"{name} pairing snapshot failed: {status} {result}"
            latest[name] = result.get("corrections") or {}
        b, r = latest["base"], latest["rover"]
        if (all(c.get("transport") == transport and c.get("peer_connected") is True
                for c in (b, r)) and b.get("session", 0) >= 1000000
                and b["session"] == r.get("session")):
            return latest
        time.sleep(0.5)
    raise AssertionError(f"Automatic {transport} pair not established: {latest}")

def pairtest_report(base):
    st, d = req(base, "/api/v1/diagnostic")
    return {"state": d.get("state"), "reason": d.get("reason")}, d.get("last_report") or {}

def settings(base):
    st, d = req(base, "/api/v1/settings")
    assert st == 200, f"settings snapshot failed on {base}: {st} {d}"
    return d

def request_test(base, token, seconds, profile):
    """One instrument asks for the paired radio test. Both peers adopt the
    operation and run it; nothing is shared between two pages by hand."""
    body = {"id": "%032x" % random.getrandbits(128), "revision": settings(base).get("revision"), "op": "link.test",
            "transport": "sik", "seconds": seconds, "confirm": True}
    # The paired RTCM engine has no rate or direction knob, and only it injects faults.
    if profile == "injected": body["profile"] = "injected"
    st, d = req(base, "/api/v1/settings", "POST", body, token)
    assert st == 202, f"link.test refused on {base}: {st} {d}"
    return body["id"]

def wait_for_test(base, rover, timeout):
    """The operation is the device-owned outcome; the reports are the counters."""
    deadline = time.time() + timeout
    breport = rreport = {}
    operation = {}
    while time.time() < deadline:
        bstate, breport = pairtest_report(base)
        rstate, rreport = pairtest_report(rover)
        operation = settings(rover).get("operation") or {}
        if operation.get("state") in ("succeeded", "failed", "cancelled", "interrupted", "recovery_required"):
            if isinstance(breport, dict) and breport.get("state") == "done":
                break
        time.sleep(3)
    return operation, breport, rreport

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--rover", required=True)
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--pair-seconds", type=int, default=30)
    ap.add_argument("--profile", default="clean", choices=["clean", "injected"])
    ap.add_argument("--skip-probe", action="store_true")
    ap.add_argument("--skip-pairtest", action="store_true")
    a = ap.parse_args()
    out = {"started": datetime.now(timezone.utc).isoformat(), "polls": []}

    bt, rt = claim(a.base), claim(a.rover)
    print("claimed control on both")

    if not a.skip_probe:
        # The wiring probe reads UART2, so Radio must not be carrying the
        # correction link while it runs. No test is gated on a selected medium.
        select_pair(a.base, bt, a.rover, rt, "wifi")
        for name, url, tok in (("base", a.base, bt), ("rover", a.rover, rt)):
            state, resp = probe(url, tok)
            out[f"{name}_radio_probe"] = {"state": state, "response": resp}
            print(f"{name} radio probe: {state} :: {resp}")
            time.sleep(1)

    if not a.skip_pairtest:
        request = request_test(a.rover, rt, a.pair_seconds, a.profile)
        print(f"paired test requested from the rover: {request}, {a.pair_seconds}s, profile {a.profile}")
        # The rover is the coordinator; the base adopted the same operation.
        base_operation = settings(a.base).get("operation") or {}
        assert base_operation.get("kind") == "test" and base_operation.get("transport") == "sik", \
            f"base did not adopt the operation: {base_operation}"
        operation, bs, rs = wait_for_test(a.base, a.rover, a.pair_seconds + 120)
        # The workflow claim is that both peers ran the one requested operation and
        # each stored its own report; the packet verdict stays a recorded measurement.
        assert isinstance(bs, dict) and bs.get("state") == "done" and isinstance(rs, dict) and rs.get("state") == "done", \
            f"both reports never completed: {bs} {rs}"
        out["pairtest"] = {"request": request, "operation": operation,
                           "base_report": bs if isinstance(bs, dict) else str(bs)[:400],
                           "rover_report": rs if isinstance(rs, dict) else str(rs)[:400]}
        print("pairtest operation:", {k: operation.get(k) for k in ("state", "reason", "transport", "seconds", "profile")})
        print("pairtest base:", {k: bs.get(k) for k in ("state", "expected_tx", "sent", "received", "errors", "local_pass", "pair_pass")} if isinstance(bs, dict) else bs)
        print("pairtest rover:", {k: rs.get(k) for k in ("state", "expected_rx", "received", "errors", "integrity_violations", "local_pass", "pair_pass")} if isinstance(rs, dict) else rs)
        time.sleep(2)

    # Fresh leases: the pairtest wait can exceed the 120-second control lease.
    bt, rt = claim(a.base), claim(a.rover)
    print("re-claimed control on both for local Radio selection")
    paired = select_pair(a.base, bt, a.rover, rt, "sik")
    out["pair_established"] = paired
    out["session"] = paired["base"]["session"]
    print(f"automatic Radio pair connected: session {out['session']}")

    print(f"observing both units for {a.seconds}s ...")
    snaps = []
    end = time.time() + a.seconds
    while time.time() < end:
        row = {"t": datetime.now(timezone.utc).isoformat()}
        for name, url in (("base", a.base), ("rover", a.rover)):
            row[name] = snapshot(url)
        snaps.append(row)
        time.sleep(5)
    out["observation_seconds"] = a.seconds
    out["snapshots"] = snaps
    first, last = snaps[0], snaps[-1]
    for name in ("base", "rover"):
        keys = ("submitted", "envelopes", "received", "complete", "output_rejected", "wire_errors",
                "assembly_expired", "wrong_session", "replays", "rtcm_errors", "tx_wait", "fault")
        delta = {k: (last[name].get(k, 0), first[name].get(k, 0)) for k in keys if last[name].get(k) != first[name].get(k)}
        print(f"{name} delta:", delta)
        fwd_first = (first[name].get("output") or {}).get("forwarded", 0)
        fwd_last = (last[name].get("output") or {}).get("forwarded", 0)
        if fwd_first != fwd_last: print(f"{name} output.forwarded: {fwd_first} -> {fwd_last}")
    out_name = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), f".pio/sik-bench-{int(time.time())}.json")
    open(out_name, "w").write(json.dumps(out, indent=1))
    print("evidence saved:", out_name)

if __name__ == "__main__":
    main()
