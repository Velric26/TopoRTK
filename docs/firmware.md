# Firmware

> Status: The display/GNSS demo, safe UM980 command relay, per-unit builds, and test-only ESP-to-ESP Wi-Fi transport have been validated on project hardware. RTCM forwarding is not yet implemented.

## Supported Hardware

List exact ESP32-S3 board revisions and connected peripherals.

## Development Environment

Record toolchain, framework, dependencies, board settings, and required host tools.

## Build and Flash

Document commands, cable/port requirements, recovery procedure, and how to confirm the flashed version.

## Architecture

Describe tasks/modules for GNSS control, radio monitoring, logging, local UI, networking, and mobile UI.

Current implementation: [UM980 display demo](../firmware/um980-display-demo/README.md).

## Configuration and Storage

Document defaults, persistent settings, configuration migrations, log locations, and safe shutdown behavior.

## Verified Releases

| Version/commit | Device | Test record | Result |
|---|---|---|---|
| Uncommitted prototype, 2026-09-06 | Units A and B | [ESP32 Wi-Fi link](../tests/2026-09-06-esp32-wifi-link/README.md) | PASS: handshake and checksummed/sequenced packets; RTCM not carried |
