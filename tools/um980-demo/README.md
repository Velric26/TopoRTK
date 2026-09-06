# UM980 USB Serial Demo

Safe bring-up tool for identifying a UM980 and sampling live GGA without saving receiver settings.

## Requirements

- UM980 carrier connected through USB serial.
- Python 3 with `pyserial` from `requirements.txt`.
- Default receiver baud: 115200.

The current development PC can run it with PlatformIO's Python:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" .\um980_demo.py
```

## Read-only identification

```powershell
python .\um980_demo.py --port COM7
```

This listens passively and sends only `VERSION` and `CONFIG`.

## Temporary live GGA test

```powershell
python .\um980_demo.py --port COM7 --live-gga --gga-seconds 10
```

The live test is allowed only when the port has no existing output. It temporarily sends `GPGGA 1`, captures GGA for the requested duration, and sends `UNLOG` in a `finally` block. It never sends `SAVECONFIG`, so nothing is intentionally written to receiver flash.

`UNLOG` clears all volatile output on the current port. Do not use `--live-gga` on a configured working receiver unless its current output state has been recorded and clearing it is acceptable.

## Pass Criteria

- The receiver acknowledges `VERSION` and `CONFIG` with `response: OK`.
- The version response identifies `UM980`.
- The optional live test captures GGA messages near the requested 1 Hz rate.
- The receiver acknowledges `UNLOG` after the test.
- A valid position is a separate test and requires a suitable powered antenna with sky view.

