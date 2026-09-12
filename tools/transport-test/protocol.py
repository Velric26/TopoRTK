"""Transport-independent, bounded framing and integrity accounting (not RTCM)."""
import hashlib
import struct
import zlib

MAGIC = b"TRTK"
HEADER = struct.Struct("<4sBB16sIIH")
CRC = struct.Struct("<I")
MAX_PAYLOAD = 1024


def tag(run_id):
    return hashlib.sha256(run_id.encode("utf-8")).digest()[:16]


def payload(node, sequence, size):
    return bytes((node + sequence + i) % 256 for i in range(size))


def encode(run_id, node, sequence, count, size):
    body = HEADER.pack(MAGIC, 1, node, tag(run_id), sequence, count, size)
    body += payload(node, sequence, size)
    return body + CRC.pack(zlib.crc32(body))


class Decoder:
    """Resynchronizes streams after insertions/deletions; CRC protects the header too."""
    def __init__(self):
        self.buffer = bytearray()
        self.crc_errors = self.framing_errors = self.discarded_bytes = 0

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 4:
            position = self.buffer.find(MAGIC)
            if position < 0:
                self.discarded_bytes += len(self.buffer) - 3
                del self.buffer[:-3]
                break
            if position:
                self.discarded_bytes += position
                del self.buffer[:position]
            if len(self.buffer) < HEADER.size:
                break
            _, version, node, run, seq, count, size = HEADER.unpack_from(self.buffer)
            if version != 1 or node not in (0, 1) or not 1 <= size <= MAX_PAYLOAD or not 1 <= count <= 100000 or seq >= count:
                self.framing_errors += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            length = HEADER.size + size + CRC.size
            if len(self.buffer) < length:
                break
            body = bytes(self.buffer[:length - CRC.size])
            if CRC.unpack_from(self.buffer, length - CRC.size)[0] != zlib.crc32(body):
                self.crc_errors += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            frames.append((node, run, seq, count, body[HEADER.size:]))
            del self.buffer[:length]
        return frames

    def datagram(self, data):
        # UDP boundaries must not splice two truncated datagrams into one frame.
        frames = self.feed(data)
        if self.buffer:
            self.framing_errors += 1
            self.discarded_bytes += len(self.buffer)
            self.buffer.clear()
        return frames


class Tracker:
    def __init__(self, run_id, peer, count, size):
        self.run = tag(run_id)
        self.peer, self.count, self.size = peer, count, size
        self.seen = set()
        self.duplicates = self.reordered = self.foreign = self.invalid = 0
        self.highest = -1
        self.arrivals = []

    def accept(self, frame, now):
        node, run, sequence, count, data = frame
        if run != self.run or node != self.peer:
            self.foreign += 1
            return "foreign"
        if count != self.count or len(data) != self.size or sequence >= self.count:
            self.invalid += 1
            return "configuration_mismatch"
        if data != payload(node, sequence, self.size):
            self.invalid += 1
            return "payload_mismatch"
        if sequence in self.seen:
            self.duplicates += 1
            return "duplicate"
        self.reordered += sequence < self.highest
        self.highest = max(self.highest, sequence)
        self.seen.add(sequence)
        self.arrivals.append(now)
        return "valid"

    def report(self, elapsed):
        gaps = [b - a for a, b in zip(self.arrivals, self.arrivals[1:])]
        ordered = sorted(gaps)
        missing = [i for i in range(self.count) if i not in self.seen]
        return {"expected_frames": self.count, "valid_unique_frames": len(self.seen),
                "missing_frames": len(missing), "missing_sequence_sample": missing[:100],
                "loss_percent": 100 * len(missing) / self.count if self.count else None,
                "duplicates": self.duplicates, "reordered": self.reordered,
                "foreign_frames": self.foreign, "invalid_frames": self.invalid,
                "valid_payload_bytes": len(self.seen) * self.size,
                "payload_goodput_bytes_per_second_over_capture": len(self.seen) * self.size / elapsed,
                "first_valid_seconds": self.arrivals[0] if self.arrivals else None,
                "tail_without_valid_seconds": elapsed - self.arrivals[-1] if self.arrivals else elapsed,
                "max_interarrival_seconds": max(gaps) if gaps else None,
                "p95_interarrival_seconds": ordered[int((len(ordered)-1)*.95)] if ordered else None}
