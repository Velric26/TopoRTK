#!/usr/bin/env python3
"""SiK bench driver: radio probes, paired synthetic radio test, automatic live
SiK pairing and correction-counter observation over the instrument HTTP APIs.
No firmware flashing, no receiver commands outside the existing profiles.

Usage:
  python run_sik_bench.py --base http://192.168.100.20 --rover http://192.168.100.19 \
      [--seconds 120] [--skip-probe] [--skip-pairtest] [--run N] [--pair-seconds 30]
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

def arm_pairtest(base, token, rover_base, rover_token, run, seconds, profile):
    body = {"op": "pairtest", "run": run, "seconds": seconds, "profile": profile, "confirm": True}
    st, _ = req(base, "/api/v1/diagnostic", "POST", body, token)
    assert st == 202, f"base pairtest arm failed: {st}"
    st, _ = req(rover_base, "/api/v1/diagnostic", "POST", body, rover_token)
    assert st == 202, f"rover pairtest arm failed: {st}"
    # A silent drop is possible while the rover finishes a probe; verify both
    # engines actually armed and re-arm the rover once if needed.
    for _ in range(3):
        st, rd = req(rover_base, "/api/v1/diagnostic")
        if rd.get("state") in ("armed", "running"): return
        time.sleep(1)
        st, _ = req(rover_base, "/api/v1/diagnostic", "POST", body, rover_token)
        assert st == 202, f"rover pairtest re-arm failed: {st}"
    assert False, "rover pairtest never armed"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--rover", required=True)
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--run", type=int, default=random.randint(100000, 999999))
    ap.add_argument("--pair-seconds", type=int, default=30)
    ap.add_argument("--profile", default="clean", choices=["clean", "injected"])
    ap.add_argument("--skip-probe", action="store_true")
    ap.add_argument("--skip-pairtest", action="store_true")
    a = ap.parse_args()
    out = {"started": datetime.now(timezone.utc).isoformat(), "run": a.run, "polls": []}

    bt, rt = claim(a.base), claim(a.rover)
    print(f"claimed control on both (run {a.run})")
    if not a.skip_probe or not a.skip_pairtest:
        # Advanced synthetic diagnostics retain their Wi-Fi-selected admission gate.
        select_pair(a.base, bt, a.rover, rt, "wifi")

    if not a.skip_probe:
        for name, url, tok in (("base", a.base, bt), ("rover", a.rover, rt)):
            state, resp = probe(url, tok)
            out[f"{name}_radio_probe"] = {"state": state, "response": resp}
            print(f"{name} radio probe: {state} :: {resp}")
            time.sleep(1)

    if not a.skip_pairtest:
        arm_pairtest(a.base, bt, a.rover, rt, a.run, a.pair_seconds, a.profile)
        print(f"paired synthetic test armed: run {a.run}, {a.pair_seconds}s, profile {a.profile}")
        deadline = time.time() + a.pair_seconds + 100
        breport = rreport = {}
        while time.time() < deadline:
            bstate, breport = pairtest_report(a.base)
            rstate, rreport = pairtest_report(a.rover)
            bdone = isinstance(breport, dict) and breport.get("state") == "done"
            rdone = isinstance(rreport, dict) and rreport.get("state") == "done"
            if bdone and rdone: break
            time.sleep(3)
        out["pairtest"] = {"base_report": breport if isinstance(breport, dict) else str(breport)[:400],
                           "rover_report": rreport if isinstance(rreport, dict) else str(rreport)[:400]}
        bs, rs = out["pairtest"]["base_report"], out["pairtest"]["rover_report"]
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
