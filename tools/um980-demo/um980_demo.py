#!/usr/bin/env python3
"""Safe serial bring-up demo for a Unicore UM980 receiver.

By default the script only listens and sends read-only VERSION and CONFIG queries.
The optional --live-gga test temporarily enables GGA at 1 Hz, then sends UNLOG.
Nothing is written to receiver flash because SAVECONFIG is never sent.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass

import serial
from serial.tools import list_ports


DEFAULT_BAUD = 115200
WCH_VID = 0x1A86
CH340_PID = 0x7523


@dataclass
class GgaFix:
    talker: str
    utc: str
    latitude: str
    longitude: str
    quality: int
    satellites: int | None
    hdop: float | None


QUALITY_NAMES = {
    0: "invalid/no fix",
    1: "standalone",
    2: "differential",
    4: "RTK fixed",
    5: "RTK float",
    6: "INS",
    7: "fixed/base",
}


def detect_port() -> str:
    candidates = [
        port.device
        for port in list_ports.comports()
        if port.vid == WCH_VID and port.pid == CH340_PID
    ]
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise RuntimeError("No CH340 USB serial device was detected; pass --port explicitly.")
    raise RuntimeError(
        "Multiple CH340 devices were detected; pass --port explicitly: "
        + ", ".join(candidates)
    )


def read_for(port: serial.Serial, seconds: float) -> bytes:
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(port.read(4096))
    return bytes(data)


def send_command(port: serial.Serial, command: str, wait_seconds: float = 1.5) -> str:
    print(f"> {command}")
    port.write((command + "\r\n").encode("ascii"))
    port.flush()
    response = read_for(port, wait_seconds).decode("ascii", errors="replace")
    print(response, end="" if response.endswith("\n") else "\n")
    return response


def parse_gga(line: str) -> GgaFix | None:
    line = line.strip()
    if not line.startswith("$") or "GGA," not in line:
        return None
    body = line.split("*", 1)[0]
    fields = body.split(",")
    if len(fields) < 9 or not fields[0].endswith("GGA"):
        return None

    def integer(value: str) -> int | None:
        return int(value) if value else None

    def decimal(value: str) -> float | None:
        return float(value) if value else None

    quality = integer(fields[6])
    return GgaFix(
        talker=fields[0][1:3],
        utc=fields[1],
        latitude=(fields[2] + fields[3]) if fields[2] else "",
        longitude=(fields[4] + fields[5]) if fields[4] else "",
        quality=quality if quality is not None else 0,
        satellites=integer(fields[7]),
        hdop=decimal(fields[8]),
    )


def require_ok(command: str, response: str) -> None:
    if "response: OK" not in response:
        raise RuntimeError(f"Receiver did not acknowledge {command!r} with OK.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial port; auto-detects one CH340 when omitted")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--live-gga",
        action="store_true",
        help="temporarily output GGA at 1 Hz, then restore no-output state with UNLOG",
    )
    parser.add_argument("--gga-seconds", type=float, default=10.0)
    args = parser.parse_args()

    port_name = args.port or detect_port()
    print(f"Opening {port_name} at {args.baud} baud")

    with serial.Serial(port_name, args.baud, timeout=0.25) as receiver:
        receiver.reset_input_buffer()
        passive = read_for(receiver, 2.0)
        print(f"Passive bytes in 2 s: {len(passive)}")
        if passive:
            print(passive.decode("ascii", errors="replace"))

        version = send_command(receiver, "VERSION", 2.0)
        require_ok("VERSION", version)
        config = send_command(receiver, "CONFIG", 2.0)
        require_ok("CONFIG", config)

        if not args.live_gga:
            print("PASS: receiver identified; no output configuration changed")
            return 0

        if passive:
            raise RuntimeError(
                "Refusing temporary GGA test because this port already had output. "
                "Use the read-only demo or restore/record the existing configuration manually."
            )

        gga_data = b""
        try:
            response = send_command(receiver, "GPGGA 1", 1.5)
            require_ok("GPGGA 1", response)
            gga_data = read_for(receiver, args.gga_seconds)
            print(gga_data.decode("ascii", errors="replace"), end="")
        finally:
            restore = send_command(receiver, "UNLOG", 1.5)
            require_ok("UNLOG", restore)

        fixes = [
            fix
            for line in gga_data.decode("ascii", errors="replace").splitlines()
            if (fix := parse_gga(line)) is not None
        ]
        if not fixes:
            raise RuntimeError("No GGA messages were captured.")

        latest = fixes[-1]
        quality = QUALITY_NAMES.get(latest.quality, f"unknown ({latest.quality})")
        print(
            f"PASS: captured {len(fixes)} GGA messages; latest quality={quality}, "
            f"satellites={latest.satellites}, HDOP={latest.hdop}"
        )
        if latest.quality == 0:
            print("NOTE: serial/demo path passed, but GNSS had no valid position fix.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, serial.SerialException, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

