# Waveshare Board Demo

Minimal bring-up firmware for the **Waveshare ESP32-S3-Touch-LCD-3.5** (non-B model).

## Scope

This first incremental test covers only:

- ESP32-S3 startup and USB serial output.
- External 16 MB flash and embedded 8 MB PSRAM reporting.
- TCA9554 I/O expander response.
- ST7796 320 x 480 display initialization, backlight, text, and colors.
- FT6336/FT6X36 capacitive-touch detection and coordinates.

It does not test Wi-Fi, Bluetooth, microSD, RTC, onboard QMI8658 IMU, audio, camera, BNO085, GNSS, or telemetry radio.

## Build

From this directory:

```powershell
pio run
```

## Flash

The current development board is configured as `COM4`:

```powershell
pio run --target upload
```

If Windows assigns another port, update `upload_port` and `monitor_port` in `platformio.ini` after verifying its USB VID/PID.

## Monitor

```powershell
pio device monitor
```

Expected serial results include:

```text
TCA9554 I/O expander at 0x20: PASS
FT6336 touch controller at 0x38: PASS
ST7796 display initialization: PASS
BOOT COMPLETE
```

The screen must show the TopoRTK test page, red/green/blue bars, and `Touch: READY`. Touching the screen must place a yellow marker and update coordinates on both the screen and serial monitor.

## Pass Criteria

- Build and flash complete without errors.
- Serial reports the expected ESP32-S3, flash, and PSRAM sizes.
- Expander, touch controller, and display initialization report `PASS`.
- Display content and all three color bars appear correctly.
- At least five touches across the screen report plausible coordinates and appear at the touched location.
- The device remains responsive for at least one minute.

## Source Basis

Pin assignments, display driver, touch controller, and LCD reset sequence follow Waveshare's official examples. See `docs/hardware/waveshare-esp32-s3-touch-lcd-3.5/README.md`.
