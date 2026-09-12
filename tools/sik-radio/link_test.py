"""Bounded binary radio test on two explicitly selected, previously backed-up USB devices."""
import argparse
from contextlib import ExitStack
import datetime as dt
import hashlib
import json
from pathlib import Path
import struct
import time

import serial
from serial.tools import list_ports


def frames(direction, count=200):
    return [b"TopoRTK!" + struct.pack("<II", direction, index)
            + bytes((value + index + direction) % 256 for value in range(256))
            for index in range(count)]


def phase(ports, name, rates, count, output):
    outgoing = [frames(index + 1, count) if rate else [] for index, rate in enumerate(rates)]
    expected = [b"".join(outgoing[1]), b"".join(outgoing[0])]
    received = [bytearray(), bytearray()]
    sent = [0, 0]
    start = time.monotonic()
    last_activity = start
    duration = max((len(outgoing[i]) * 272 / rate if rate else 0)
                   for i, rate in enumerate(rates))
    deadline = start + duration + 10
    while time.monotonic() < deadline:
        now = time.monotonic()
        for index, port in enumerate(ports):
            if sent[index] < len(outgoing[index]) and now - start >= sent[index] * 272 / rates[index]:
                payload = outgoing[index][sent[index]]
                written = port.write(payload)
                if written != len(payload):
                    raise RuntimeError("Incomplete USB serial write")
                sent[index] += 1
                last_activity = now
            incoming = port.read(port.in_waiting)
            if incoming:
                received[index].extend(incoming)
                last_activity = now
        if all(sent[i] == len(outgoing[i]) and len(received[i]) >= len(expected[i]) for i in (0, 1)):
            if now - last_activity >= 2:
                break
        time.sleep(0.002)
    results = []
    for index, port in enumerate(ports):
        actual = bytes(received[index])
        capture = output.with_name(output.stem + "-" + name + "-" + port.port + ".bin")
        if actual != expected[index]:
            capture.write_bytes(actual)
        results.append({"receiver": port.port, "sent_frames": sent[1-index],
                        "expected_bytes": len(expected[index]), "received_bytes": len(actual),
                        "expected_sha256": hashlib.sha256(expected[index]).hexdigest(),
                        "received_sha256": hashlib.sha256(actual).hexdigest(),
                        "exact_match": actual == expected[index],
                        "failed_capture": capture.name if actual != expected[index] else None})
    return {"name": name, "offered_bytes_per_second": dict(zip([p.port for p in ports], rates)),
            "elapsed_seconds": round(time.monotonic() - start, 3),
            "pass": all(item["exact_match"] for item in results), "directions": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ports", nargs=2, required=True)
    parser.add_argument("--identity-backup", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rate", type=int, default=3000, help="Aggregate offered bytes/second")
    parser.add_argument("--frames", type=int, default=200)
    args = parser.parse_args()
    if args.output.exists() or len(set(args.ports)) != 2:
        parser.error("Require new output file and two distinct ports")
    if not 100 <= args.rate <= 5000 or not 1 <= args.frames <= 2000:
        parser.error("Rate must be 100..5000 and frames 1..2000")
    inventory = {p.device: p for p in list_ports.comports()}
    for name in args.ports:
        backup = json.loads((args.identity_backup / (name + ".json")).read_text(encoding="utf-8"))
        if name not in inventory or inventory[name].hwid != backup["usb"]["hwid"]:
            raise RuntimeError(f"{name}: USB identity differs from backup")
    result = {"utc": dt.datetime.now(dt.timezone.utc).isoformat(), "serial_baud": 115200,
              "frame_bytes": 272, "frames_per_active_direction": args.frames,
              "status": "running", "phases": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    save()
    try:
        with ExitStack() as stack:
            ports = [stack.enter_context(serial.Serial(name, 115200, timeout=0,
                     write_timeout=2, rtscts=False, xonxoff=False, dsrdtr=False)) for name in args.ports]
            time.sleep(10)  # Allow the already configured radios to establish their link.
            for port in ports:
                unsolicited = port.read(port.in_waiting)
                if unsolicited:
                    raise RuntimeError(f"{port.port}: unexpected initial data: {unsolicited[:80]!r}")
            for name, rates in (("first_to_second", [args.rate, 0]),
                                ("second_to_first", [0, args.rate]),
                                ("simultaneous_bidirectional", [args.rate / 2, args.rate / 2])):
                print(f"Starting {name}", flush=True)
                item = phase(ports, name, rates, args.frames, args.output)
                result["phases"].append(item)
                save()
                print(json.dumps(item), flush=True)
            if not all(item["pass"] for item in result["phases"]):
                raise RuntimeError("One or more binary transfer phases failed")
        result["status"] = "pass"
    except Exception as exc:
        result["status"] = "fail"
        result["error"] = str(exc)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
