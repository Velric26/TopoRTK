"""Package the dated SiK evidence and tools, verifying every archived payload hash."""
import hashlib
import json
from pathlib import Path
import zipfile


root = Path(__file__).resolve().parents[2]
record = root / "tests/2026-09-11-sik-radio-configuration"
archive = record / "sik-radio-backup.zip"
paths = [p for p in record.rglob("*") if p.is_file() and p.suffix in (".json", ".md", ".bin")]
paths += [p for p in Path(__file__).parent.iterdir() if p.suffix in (".py", ".txt")]
paths += [root / "docs/radio.md"]
checksums = {}
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
    for path in sorted(paths):
        name = path.relative_to(root).as_posix()
        data = path.read_bytes()
        checksums[name] = hashlib.sha256(data).hexdigest()
        output.writestr(name, data)
    output.writestr("SHA256SUMS.json", json.dumps(checksums, indent=2) + "\n")
with zipfile.ZipFile(archive) as saved:
    if saved.testzip() is not None:
        raise RuntimeError("ZIP integrity verification failed")
    for name, digest in json.loads(saved.read("SHA256SUMS.json")).items():
        if hashlib.sha256(saved.read(name)).hexdigest() != digest:
            raise RuntimeError(f"Archived checksum mismatch: {name}")
(record / "sik-radio-backup.sha256").write_text(
    hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n", encoding="ascii")
print(f"Verified {len(paths)} payload files in {archive}")
