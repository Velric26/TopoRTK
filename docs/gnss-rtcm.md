# GNSS and RTCM

> Status: Unit B is confirmed in `MODE ROVER SURVEY`. Unit A is confirmed in temporary `MODE BASE TIME 60 2.5 3.5`. On 2026-09-06 the HA-609 open-sky test produced a rover `RTK FIXED` solution using RTCM carried over the ESP32 Wi-Fi bridge, with zero displayed RTCM or NMEA checksum errors. Absolute accuracy and repeatability remain untested.

## Receiver Identification

Both tested receivers identify as UM980 with firmware `R4.10Build13504`. Published records omit unique receiver serial numbers. The validated BDRTK carrier mapping is USB/CH340 to UM980 COM3 and the 8-pin TTL channel to COM2, with all tested ports at 115200 baud.

Current module record and first USB test: [Unicore UM980](hardware/unicore-um980/README.md).

## Operating-Role Commands

Source: [Unicore N4 High Precision GNSS Commands and Logs Reference, R1.15](hardware/unicore-um980/references/n4-commands-reference-r1.15.pdf), section 3.

| Purpose | Command | Project status |
|---|---|---|
| Query current role | `MODE` | PASS through the ESP32 on Unit A; PASS on Unit B |
| Precision-survey rover | `MODE ROVER SURVEY` | PASS on Unit B; command acknowledged and role read back |
| Fixed-coordinate base | `MODE BASE [ID] <latitude> <longitude> <altitude>` | Manufacturer documented; project test blocked until base coordinates and height meaning are controlled |
| Self-optimizing base | `MODE BASE [ID] TIME <seconds> [distance]` | Manufacturer documented; not approved for casual testing because the manual says the optimized position is saved in flash |
| Default 60-second averaged base | `MODE BASE` | PASS on Unit A for relay and role switching only; read back as `MODE BASE TIME 60 2.5 3.5`; not accepted for survey control |

The manual says entering a new `MODE` command switches the receiver to the latest requested role and that rover mode is the default. It also says the receiver automatically recognizes RTCM format. Available modes can depend on receiver authorization.

The unit letter and operating role are separate concepts. `A` and `B` identify physical instruments; either instrument may be commanded to `BASE` or `ROVER` later.

Current temporary assignment:

- Unit A: temporary self-optimizing base; live RTCM generation and transport passed, but its autonomous coordinate is not survey control.
- Unit B: confirmed rover in `MODE ROVER SURVEY`; achieved `RTK FIXED` in the first documented HA-609 open-sky Wi-Fi test.
- Future: select and verify either role from the ESP32 interface without reflashing.

### Role-Switching Rules

1. Query and record `MODE` before every role change.
2. Never construct a fixed-base command from a casual standalone GGA position.
3. For a fixed base, validate latitude, longitude, altitude/height interpretation, datum, epoch, antenna reference point, and antenna height before sending the command.
4. Do not use `MODE BASE TIME` until its flash behavior and recovery procedure are deliberately included in a test.
5. Switching to base mode alone does not define the required RTCM message set, rates, or output port; configure and validate those separately.
6. Do not send `SAVECONFIG` during initial role-switch tests.
7. After each change, query `MODE`, inspect acknowledgements, and confirm expected behavior on real hardware.

### ESP32 Command Relay Checkpoint

On 2026-09-06, Unit A's native USB console received `role base-test`. The ESP32 translated this allowlisted command into `MODE BASE`, sent it over TTL Channel 2, then sent `MODE` for verification. The UM980 acknowledged both and reported `MODE BASE TIME 60 2.5 3.5`; the project owner confirmed that the physical display showed `BASE` beneath `UM980 OK`.

This validates the ESP32-to-UM980 configuration path and runtime role switching without reflashing either device. It does not validate base coordinates, completion of position averaging, RTCM generation, RTK accuracy, or persistence across a power cycle. No `SAVECONFIG` command was sent. See the [Unit A command-relay test](../tests/2026-09-06-unit-a-base-relay/README.md).

A later power/reconnection cycle returned Unit A to the receiver's default `MODE ROVER SURVEY`, confirming that the temporary role was not retained. Unit A was then deliberately returned to `MODE BASE` and verified again before the RTCM bench checkpoint.

## Base Configuration

Record the complete commands/settings for controlled base coordinates, observation mode, RTCM messages, rates, and output UART. The first project base test must use either a deliberately temporary test coordinate or a documented control coordinate and must include recovery to `MODE ROVER SURVEY`.

### Base GGA State

The UM980 NMEA GGA table defines quality 1 as single-point positioning, 4 as RTK integer fixed, 5 as RTK float, and 7 as manual input mode. During the 2026-09-06 self-optimizing base test, Unit A first showed `GPS FIX` while it averaged its standalone position, then GGA quality 7 after selecting and holding the base coordinate. The role-aware firmware now renders these base states as `BASE SURVEY` and `BASE LOCKED` while retaining raw quality information in USB diagnostics.

An RTK base is the correction reference and is not expected to report `RTK FLOAT` or `RTK FIXED` unless it is itself consuming corrections from another reference. Its critical states are instead whether its coordinate has been established correctly and whether it is outputting the expected RTCM stream.

## Rover Configuration

Use `MODE ROVER SURVEY` for the intended survey-rover dynamics. Record RTCM input, solution output, raw observations, and status messages separately.

## Horizontal Accuracy Estimate

The ESP32 enables `BESTNAVA COM2 1` and validates each Unicore ASCII CRC-32 before using the record. UM980 `BESTNAV` fields 9 and 10 are latitude and longitude standard deviations in metres. The display reports their root-sum-square as `H-ACC` (1DRMS):

```text
H-ACC = sqrt(latitude_sigma^2 + longitude_sigma^2)
```

Values are formatted as mm, cm, or m. This is a live receiver uncertainty estimate, not a guaranteed error bound and not proof of absolute survey accuracy. The display suppresses it without a valid fix and after an autonomous base is locked, because that base coordinate's true control accuracy is not established by its held solution.

## RTCM Profile

| Message | Rate | Purpose | Measured bytes/s | Validated |
|---|---:|---|---:|---|
| RTCM 1006 | 0.1 Hz | Base antenna reference point with height | Not isolated by type | Included in field-validated COM2 profile |
| RTCM 1033 | 0.1 Hz | Receiver and antenna descriptors | Not isolated by type | Included in field-validated COM2 profile |
| RTCM 1074 | 1 Hz | GPS MSM4 observations | Not isolated by type | Included in field-validated COM2 profile |
| RTCM 1084 | 1 Hz | GLONASS MSM4 observations | Not isolated by type | Included in field-validated COM2 profile |
| RTCM 1094 | 1 Hz | Galileo MSM4 observations | Not isolated by type | Included in field-validated COM2 profile |
| RTCM 1124 | 1 Hz | BeiDou MSM4 observations | Not isolated by type | Included in field-validated COM2 profile |

The initial command set follows the manufacturer's base-station example. All commands were sent without `SAVECONFIG`. The antenna-less bench checkpoint validated framing and transport but not useful correction content. The subsequent HA-609 open-sky checkpoint produced `RTK FIXED` at Unit B while the RTCM counters increased at both ends with zero displayed bridge errors. See the [Wi-Fi RTCM bridge bench test](../tests/2026-09-06-wifi-rtcm-bridge-bench/README.md) and [HA-609 Wi-Fi RTK open-sky test](../tests/2026-09-06-ha609-wifi-rtk-open-sky/README.md).

## Known-Good Configuration

Link the exact exported configuration and the test record that validated it.

## Recovery

Document reset, factory-default, firmware-update, and lost-communication procedures.
