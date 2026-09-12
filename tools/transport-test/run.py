"""Run one endpoint on each field host: UDP over Wi-Fi or serial over SiK USB/TTL."""
import argparse
import csv
import datetime as dt
import json
from pathlib import Path
import socket
import time

from protocol import CRC, HEADER, Decoder, Tracker, encode


def address(value):
    host, port = value.rsplit(":", 1)
    return socket.gethostbyname(host), int(port)


class UDP:
    datagrams = True
    def __init__(self, bind, peer):
        self.peer = address(peer)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(address(bind))
        self.socket.setblocking(False)
        self.foreign_datagrams = 0

    def send(self, data):
        return self.socket.sendto(data, self.peer)

    def receive(self):
        try:
            data, source = self.socket.recvfrom(65535)
        except BlockingIOError:
            return None
        if source != self.peer:
            self.foreign_datagrams += 1
            return b""
        return data

    def close(self):
        self.socket.close()


class Serial:
    datagrams = False
    def __init__(self, port, baud, identity):
        import serial
        from serial.tools import list_ports
        devices = {p.device: p for p in list_ports.comports()}
        if port not in devices or devices[port].serial_number != identity:
            raise ValueError("Serial device/USB identity mismatch; inspect ports before transmitting")
        self.port = serial.Serial(port, baud, timeout=0, write_timeout=2,
                                  rtscts=False, xonxoff=False, dsrdtr=False)

    def send(self, data):
        return self.port.write(data)

    def receive(self):
        count = self.port.in_waiting
        return self.port.read(min(count, 4096)) if count else None

    def close(self):
        self.port.close()


def execute(args, link):
    decoder = Decoder()
    tracker = Tracker(args.run_id, 1 - args.node, args.peer_count, args.payload)
    started = time.monotonic()
    sent = wire_rx = 0
    next_send = args.start_delay
    result = {"schema": 1, "utc": dt.datetime.now(dt.timezone.utc).isoformat(),
              "configuration": vars(args).copy(), "status": "running"}
    result["configuration"]["output"] = str(args.output)
    args.output.mkdir(parents=True, exist_ok=False)
    report_file = args.output / "report.json"
    report_file.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    try:
        with (args.output / "events.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["local_elapsed_seconds", "event", "sequence", "wire_bytes"])
            while time.monotonic() - started < args.seconds:
                now = time.monotonic() - started
                if sent < args.count and now >= next_send:
                    frame = encode(args.run_id, args.node, sent, args.count, args.payload)
                    if link.send(frame) != len(frame):
                        raise RuntimeError("Short transport write")
                    writer.writerow([round(now, 6), "sent", sent, len(frame)])
                    sent += 1
                    # Do not burst to catch up after an OS scheduling delay.
                    next_send = now + len(frame) / args.rate
                for _ in range(64):
                    data = link.receive()
                    if data is None:
                        break
                    wire_rx += len(data)
                    before = (decoder.crc_errors, decoder.framing_errors)
                    decoded = decoder.datagram(data) if link.datagrams else decoder.feed(data)
                    arrival = time.monotonic() - started
                    for frame in decoded:
                        event = tracker.accept(frame, arrival)
                        writer.writerow([round(arrival, 6), event, frame[2], len(frame[4])+HEADER.size+CRC.size])
                    if before != (decoder.crc_errors, decoder.framing_errors):
                        writer.writerow([round(arrival, 6), "decode_error", "", len(data)])
                stream.flush()
                time.sleep(.001)
        result["status"] = "completed"
    except (Exception, KeyboardInterrupt) as exc:
        result["status"] = "interrupted_or_failed"
        result["error"] = str(exc) or type(exc).__name__
    finally:
        elapsed = time.monotonic() - started
        result.update({"elapsed_seconds": elapsed, "sent_frames": sent,
                       "received_wire_bytes": wire_rx, "crc_errors": decoder.crc_errors,
                       "framing_errors": decoder.framing_errors,
                       "discarded_bytes": decoder.discarded_bytes,
                       "trailing_partial_bytes": len(decoder.buffer),
                       "foreign_datagrams": getattr(link, "foreign_datagrams", 0),
                       "receive": tracker.report(elapsed)})
        result["strict_integrity_pass"] = (result["status"] == "completed" and sent == args.count
            and len(tracker.seen) == args.peer_count and not any((tracker.invalid, tracker.foreign,
            tracker.duplicates, tracker.reordered, decoder.crc_errors, decoder.framing_errors,
            decoder.discarded_bytes, len(decoder.buffer), getattr(link, "foreign_datagrams", 0))))
        report_file.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--transport", choices=["udp", "serial"], required=True)
    p.add_argument("--node", type=int, choices=[0, 1], required=True)
    p.add_argument("--run-id", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--count", type=int, default=200)
    p.add_argument("--peer-count", type=int, default=200)
    p.add_argument("--payload", type=int, default=256)
    p.add_argument("--rate", type=float, default=1000, help="Offered framed bytes/sec, not UART baud")
    p.add_argument("--seconds", type=float, default=90)
    p.add_argument("--start-delay", type=float, default=15)
    p.add_argument("--bind")
    p.add_argument("--peer")
    p.add_argument("--port")
    p.add_argument("--usb-serial")
    p.add_argument("--baud", type=int, choices=[57600, 115200], default=57600)
    p.add_argument("--distance-m", type=float)
    p.add_argument("--notes", default="", help="Antenna/power/obstruction/firmware/test-stage notes")
    a = p.parse_args()
    if a.output.exists():
        p.error("Output exists; use a new station/direction/run folder")
    if not (0 <= a.count <= 100000 and 0 <= a.peer_count <= 100000 and a.count+a.peer_count > 0
            and 1 <= a.payload <= 1024 and 0 < a.rate <= 1000000
            and 0 <= a.start_delay < a.seconds <= 3600):
        p.error("Invalid or unbounded run parameters")
    if a.start_delay + a.count*(HEADER.size+a.payload+CRC.size)/a.rate >= a.seconds:
        p.error("Capture too short for the configured sending schedule; include a drain margin")
    if a.transport == "udp":
        if not a.bind or not a.peer:
            p.error("UDP requires --bind and --peer host:port")
        if address(a.bind)[1] == 22345 or address(a.peer)[1] == 22345:
            p.error("22345 is the instrument production port; use isolated diagnostic port 22346")
        link = UDP(a.bind, a.peer)
    else:
        if not a.port or not a.usb_serial:
            p.error("Serial requires an explicit --port and --usb-serial identity")
        link = Serial(a.port, a.baud, a.usb_serial)
    try:
        result = execute(a, link)
    finally:
        link.close()
    print(json.dumps({"output": str(a.output), "strict_integrity_pass": result["strict_integrity_pass"],
                      "receive": result["receive"]}, indent=2))
    raise SystemExit(0 if result["strict_integrity_pass"] else 1)


if __name__ == "__main__":
    main()
