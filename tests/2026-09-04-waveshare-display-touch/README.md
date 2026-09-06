# Test: Waveshare display and touch bring-up

- **Date:** 2026-09-04
- **Operator:** Project owner / Codex
- **Location:** Development PC bench
- **Purpose:** Validate the first connected Waveshare ESP32-S3-Touch-LCD-3.5 board, display, touch controller, and USB serial interface.
- **Firmware:** `firmware/waveshare-board-demo/`

## Pre-flash Identification

| Check | Result |
|---|---|
| Windows serial port | `COM4` |
| USB VID:PID | `303A:1001` |
| Chip | ESP32-S3 QFN56 revision 0.2 |
| Embedded PSRAM | 8 MB reported |
| Crystal | 40 MHz |
| USB mode | USB-Serial/JTAG |

## Expected Result and Pass Criteria

See `firmware/waveshare-board-demo/README.md`.

## Results

| Check | Result | Evidence/notes |
|---|---|---|
| Firmware builds | PASS | PlatformIO completed successfully; 322,457 bytes flash and 19,236 bytes static RAM |
| Firmware flashes | PASS | `esptool.py` wrote and hash-verified all images on `COM4` |
| Serial boot completes | PASS | Repeating heartbeat received after flash |
| TCA9554 responds | PASS | LCD reset and display operation confirmed on hardware |
| ST7796 initializes | PASS | TopoRTK demo page displayed correctly |
| Display image/colors correct | PASS | Project owner confirmed correct page and color bars |
| FT6336 responds | PASS | Touch markers and coordinate updates confirmed |
| Touch locations correct | PASS | Project owner confirmed touch behavior on the panel |
| One-minute stability | PASS | Heartbeats observed through 110 seconds; free heap remained 365,596 bytes |

## Conclusion

- **Result:** PASS
- **Conclusion:** The first board's ESP32-S3, native USB serial, TCA9554-controlled LCD reset, ST7796 display, backlight, and FT6336 touch input operate with the project demo.
- **Next action:** Select and validate only one additional onboard peripheral or external interface.
