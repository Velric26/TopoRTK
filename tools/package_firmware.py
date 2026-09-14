"""Package a TopoRTK application image for local Debug OTA; no signing claim."""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

def package(data):
    marker = b'TOPORTK_FW_V1'.ljust(16, b'\0')
    hits = [i for i in range(len(data)) if data.startswith(marker, i)]
    if len(hits) != 1:
        raise ValueError('Expected exactly one embedded TopoRTK identity')
    offset = hits[0]
    identity = data[offset:offset+64]
    if len(identity) != 64 or identity[16] not in (1, 2) or identity[17] != 1:
        raise ValueError('Unsupported unit or hardware identity')
    version = identity[20:52]
    if not version[0] or 0 not in version or any(identity[i] for i in [18, 19, *range(52,64)]):
        raise ValueError('Malformed embedded identity')
    if data[0] != 0xE9 or not 256 <= len(data) <= 0x640000:
        raise ValueError('Expected an ESP32 application image within the OTA slot')
    header = bytearray(128)
    header[:8] = b'TPK1' + bytes([1, identity[16], 1, 0])
    struct.pack_into('<II', header, 8, len(data), offset)
    header[16:48] = hashlib.sha256(data).digest()
    header[48:80] = version
    struct.pack_into('<I', header, 124, zlib.crc32(header[:124]))
    return bytes(header) + data

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = package(args.image.read_bytes())
    args.output.write_bytes(result)
    print(f'Packaged Unit {chr(64+result[5])}: {len(result)-128} image bytes -> {args.output}')
