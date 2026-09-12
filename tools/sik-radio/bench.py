"""Explicit-port SiK bench operations; requires pyserial. Never discovers and writes devices."""
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import time

import serial
from serial.tools import list_ports


def read_for(port, seconds=0.6):
    end = time.monotonic() + seconds
    result = bytearray()
    while time.monotonic() < end:
        result.extend(port.read(port.in_waiting or 1))
    return bytes(result)


def command(port, value, seconds=0.6):
    port.write((value + "\r").encode("ascii"))
    port.flush()
    return read_for(port, seconds).decode("ascii", errors="replace").strip()


def enter(port):
    time.sleep(1.2)
    port.reset_input_buffer()
    port.write(b"+++")
    port.flush()
    response = read_for(port, 1.4).decode("ascii", errors="replace")
    if "OK" not in response:
        command(port, "")  # Clear a partial line if the radio was already in command mode.
        response += command(port, "AT")
    if "OK" not in response:
        raise RuntimeError(f"{port.port}: no SiK command-mode acknowledgement: {response!r}")


def snapshot(port):
    responses = {name: command(port, name) for name in ("ATI", "ATI2", "ATI3", "ATI4", "ATI5", "ATI7")}
    if "SiK" not in responses["ATI"]:
        raise RuntimeError(f"{port.port}: not identified as SiK")
    params = {}
    for register, name, value in re.findall(r"S(\d+):\s*(\w+)=(-?\d+)", responses["ATI5"]):
        params[name] = {"register": int(register), "value": int(value)}
    if len(params) < 10:
        raise RuntimeError(f"{port.port}: incomplete parameter report")
    return {"responses": responses, "parameters": params}


def apply_bench(port, data, backup, destination, target_baud=None):
    """Apply the bench profile or change only serial baud, preserving other parameters."""
    if data["usb"] != backup["usb"] or data["parameters"] != backup["parameters"]:
        raise RuntimeError("Device identity or settings changed since backup; take a new snapshot")
    for key in ("ATI", "ATI2", "ATI3", "ATI4"):
        if data["responses"][key] != backup["responses"][key]:
            raise RuntimeError("Radio identity differs from backup")
    expected = {key: dict(value) for key, value in data["parameters"].items()}
    baseline = {"SERIAL_SPEED": {115200: 115, 57600: 57}[port.baudrate], "AIR_SPEED": 64, "ECC": 0, "RTSCTS": 0,
                "MAX_WINDOW": 131}
    if target_baud is not None:
        baseline.update({"MAVLINK": 0, "TXPOWER": 1})
    if any(expected.get(key, {}).get("value") != value for key, value in baseline.items()):
        raise RuntimeError("Baseline differs from reviewed bench profile; review before changing")
    data["commands"] = []
    data["verification"] = "in_progress"

    def journal():
        destination.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    journal()
    changes = ({"SERIAL_SPEED": {115200: 115, 57600: 57}[target_baud]}
               if target_baud is not None else {"TXPOWER": 1, "MAVLINK": 0})
    for key, value in changes.items():
        item = expected[key]
        response = command(port, f"ATS{item['register']}={value}")
        data["commands"].append({"command": f"ATS{item['register']}={value}", "response": response})
        journal()
        if "OK" not in response.splitlines():
            raise RuntimeError(f"{key} was not acknowledged")
        item["value"] = value
        if snapshot(port)["parameters"] != expected:
            raise RuntimeError(f"Readback differs after setting {key}")
    for value in ("AT&W", "ATZ"):
        response = command(port, value, 1.0)
        data["commands"].append({"command": value, "response": response})
        journal()
        if value == "AT&W" and "OK" not in response.splitlines():
            raise RuntimeError("Saving settings was not acknowledged")
    time.sleep(2)
    if target_baud is not None:
        port.baudrate = target_baud
        data["host_baud_after_restart"] = target_baud
    enter(port)
    after = snapshot(port)
    data["after_restart"] = after
    data["verification"] = "pass" if after["parameters"] == expected else "fail"
    journal()
    if data["verification"] != "pass":
        raise RuntimeError("Saved settings differ after restart")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=["snapshot", "apply-bench", "set-baud"])
    parser.add_argument("--ports", nargs=2, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--baud", type=int, choices=[115200, 57600], default=115200)
    parser.add_argument("--target-baud", type=int, choices=[115200, 57600])
    args = parser.parse_args()
    if args.operation == "set-baud" and (args.target_baud is None or args.target_baud == args.baud):
        parser.error("set-baud requires a different --target-baud")
    if args.operation != "set-baud" and args.target_baud is not None:
        parser.error("--target-baud requires set-baud")
    args.output.mkdir(parents=True, exist_ok=True)
    inventory = {p.device: {"description": p.description, "hwid": p.hwid,
                           "serial_number": p.serial_number} for p in list_ports.comports()}
    if len(set(args.ports)) != 2:
        parser.error("Two distinct ports are required")
    backups = {}
    for name in args.ports:
        if (args.output / (name + ".json")).exists():
            raise RuntimeError("Output already exists; choose a new output directory")
        if args.operation != "snapshot":
            if args.backup is None:
                parser.error("Setting changes require --backup")
            backups[name] = json.loads((args.backup / (name + ".json")).read_text(encoding="utf-8"))
            if backups[name]["usb"] != inventory.get(name):
                raise RuntimeError(f"{name}: USB identity differs from backup")
    if backups:
        left, right = (backups[name] for name in args.ports)
        if left["parameters"] != right["parameters"] or left["responses"]["ATI"] != right["responses"]["ATI"]:
            raise RuntimeError("Pair differs in version or settings; review before changing")
    for name in args.ports:
        destination = args.output / (name + ".json")
        if destination.exists():
            raise RuntimeError(f"Refusing to overwrite {destination}")
        if name not in inventory:
            raise RuntimeError(f"{name} is absent")
        with serial.Serial(name, args.baud, timeout=0.05, write_timeout=2,
                           rtscts=False, xonxoff=False, dsrdtr=False) as port:
            entered = False
            try:
                enter(port)
                entered = True
                data = {"utc": dt.datetime.now(dt.timezone.utc).isoformat(), "port": name,
                        "usb": inventory[name], "host_baud": args.baud, **snapshot(port)}
                if args.operation != "snapshot":
                    data = apply_bench(port, data, backups[name], destination, args.target_baud)
                else:
                    destination.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
                print(json.dumps({"port": name, "record": str(destination),
                                  "version": data["responses"]["ATI"],
                                  "parameters": data.get("after_restart", data)["parameters"],
                                  "verification": data.get("verification", "snapshot")}), flush=True)
            finally:
                if entered:
                    command(port, "ATO")


if __name__ == "__main__":
    main()
