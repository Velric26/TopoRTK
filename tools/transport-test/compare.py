"""Reconcile both endpoint reports; a transmitter-only pass cannot prove delivery."""
import argparse
import json
from pathlib import Path


def compare(left, right):
    errors = []
    a, b = left["configuration"], right["configuration"]
    if a["node"] == b["node"] or {a["node"], b["node"]} != {0, 1}:
        errors.append("Reports must represent different nodes 0 and 1")
    for name in ("run_id", "transport", "payload"):
        if a[name] != b[name]:
            errors.append(f"Different {name}")
    for sender, receiver in ((left, right), (right, left)):
        planned = sender["configuration"]["count"]
        if receiver["configuration"]["peer_count"] != planned:
            errors.append("Peer count differs from sender plan")
        if sender["sent_frames"] != planned:
            errors.append("Sender did not finish its planned traffic")
        if receiver["receive"]["valid_unique_frames"] != sender["sent_frames"]:
            errors.append("Received unique frames differ from actual sent frames")
    if not all(item["strict_integrity_pass"] for item in (left, right)):
        errors.append("At least one endpoint failed strict integrity")
    return {"run_id": a["run_id"], "strict_pair_integrity_pass": not errors,
            "errors": errors, "qualification": "Transport test only; not RTK/field accuracy acceptance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs=2, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output exists")
    result = compare(*(json.loads(p.read_text(encoding="utf-8")) for p in args.reports))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["strict_pair_integrity_pass"] else 1)


if __name__ == "__main__":
    main()
