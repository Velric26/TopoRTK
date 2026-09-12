import argparse
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest
import zlib

from protocol import CRC, HEADER, Decoder, Tracker, encode
from run import UDP, execute
from compare import compare


class FramingTests(unittest.TestCase):
    def test_chunking_and_recovery_after_deleted_inserted_and_flipped_bytes(self):
        frames = [encode("faults", 1, i, 5, 32) for i in range(5)]
        flipped = bytearray(frames[1]); flipped[-6] ^= 0x80
        deleted = frames[2][:40] + frames[2][41:]
        inserted = frames[3][:40] + b"!" + frames[3][40:]
        wire = frames[0] + flipped + deleted + inserted + frames[4]
        decoder = Decoder(); found = []
        for index in range(0, len(wire), 7):
            found.extend(decoder.feed(wire[index:index+7]))
        self.assertEqual([frame[2] for frame in found], [0, 4])
        self.assertEqual(decoder.crc_errors, 3)
        self.assertGreater(decoder.discarded_bytes, 0)

    def test_udp_does_not_join_truncated_datagrams(self):
        frame = encode("udp", 0, 0, 1, 16)
        decoder = Decoder()
        self.assertEqual(decoder.datagram(frame[:20]), [])
        self.assertEqual(decoder.datagram(frame[20:]), [])
        self.assertEqual(len(decoder.datagram(frame)), 1)
        self.assertGreater(decoder.framing_errors, 0)

    def test_bad_header_and_noise_remain_bounded(self):
        decoder = Decoder()
        self.assertEqual(decoder.feed(b"z" * 100000), [])
        self.assertLessEqual(len(decoder.buffer), 3)
        bad = bytearray(encode("header", 0, 0, 1, 32))
        bad[4] = 99
        good = encode("header", 0, 0, 1, 32)
        self.assertEqual(len(decoder.feed(bad + good)), 1)
        self.assertGreater(decoder.framing_errors, 0)

    def test_loss_duplicates_reorder_foreign_and_recomputed_crc_corruption(self):
        decoder = Decoder(); tracker = Tracker("accounting", 1, 5, 32)
        events = []
        for index, seq in enumerate((0, 2, 2, 1, 4)):
            events.append(tracker.accept(decoder.feed(encode("accounting", 1, seq, 5, 32))[0], index+1))
        self.assertEqual(events.count("duplicate"), 1)
        tracker.accept(decoder.feed(encode("old-run", 1, 3, 5, 32))[0], 6)
        corrupt = bytearray(encode("accounting", 1, 3, 5, 32)); corrupt[HEADER.size] ^= 1
        corrupt[-4:] = CRC.pack(zlib.crc32(corrupt[:-4]))
        self.assertEqual(tracker.accept(decoder.feed(corrupt)[0], 7), "payload_mismatch")
        result = tracker.report(8)
        self.assertEqual(result["missing_sequence_sample"], [3])
        self.assertEqual((result["duplicates"], result["reordered"], result["foreign_frames"], result["invalid_frames"]), (1, 1, 1, 1))

    def test_configuration_mismatch_does_not_count_as_received(self):
        tracker = Tracker("config", 0, 2, 16)
        frame = Decoder().feed(encode("config", 0, 0, 3, 16))[0]
        self.assertEqual(tracker.accept(frame, 1), "configuration_mismatch")
        self.assertEqual(tracker.report(2)["missing_frames"], 2)


class FaultyUDP(UDP):
    def send(self, data):
        sequence = HEADER.unpack_from(data)[4]
        if sequence == 1:
            return len(data)  # Controlled loss after the sender's transport accepted it.
        if sequence == 3:
            data = bytearray(data); data[-6] ^= 1
        return super().send(data)


class EndpointTests(unittest.TestCase):
    def pair(self, fault=False):
        with tempfile.TemporaryDirectory() as temp:
            # Bind ephemeral ports before the threads start, avoiding port-selection races.
            left = (FaultyUDP if fault else UDP)("127.0.0.1:0", "127.0.0.1:1")
            right = UDP("127.0.0.1:0", "127.0.0.1:1")
            left.peer = right.socket.getsockname(); right.peer = left.socket.getsockname()
            results, errors = {}, []
            def worker(node, link):
                try:
                    args = argparse.Namespace(run_id="loopback", node=node, count=8, peer_count=8,
                        payload=64, start_delay=.15, rate=2000, seconds=1.2,
                        output=Path(temp) / str(node), transport="udp")
                    results[node] = execute(args, link)
                    saved = json.loads((args.output / "report.json").read_text())
                    self.assertEqual(saved["status"], "completed")
                    self.assertTrue((args.output / "events.csv").read_text().startswith("local_elapsed_seconds"))
                except BaseException as exc:
                    errors.append(exc)
                finally:
                    link.close()
            threads = [threading.Thread(target=worker, args=(0, left)), threading.Thread(target=worker, args=(1, right))]
            for thread in threads: thread.start()
            for thread in threads: thread.join(5)
            self.assertFalse(any(t.is_alive() for t in threads))
            if errors: raise errors[0]
            return results

    def test_two_udp_endpoints_and_durable_reports(self):
        results = self.pair()
        self.assertTrue(all(item["strict_integrity_pass"] for item in results.values()))
        self.assertEqual(results[0]["receive"]["valid_unique_frames"], 8)
        self.assertTrue(compare(results[0], results[1])["strict_pair_integrity_pass"])
        results[0]["configuration"]["run_id"] = "different-run"
        self.assertFalse(compare(results[0], results[1])["strict_pair_integrity_pass"])

    def test_fault_injection_cannot_false_pass(self):
        results = self.pair(fault=True)
        self.assertTrue(results[0]["strict_integrity_pass"])
        self.assertFalse(results[1]["strict_integrity_pass"])
        self.assertEqual(results[1]["receive"]["missing_sequence_sample"], [1, 3])
        self.assertEqual(results[1]["crc_errors"], 1)
        self.assertFalse(compare(results[0], results[1])["strict_pair_integrity_pass"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
