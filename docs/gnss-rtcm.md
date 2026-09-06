# GNSS and RTCM

> Status: Unit B is confirmed in `MODE ROVER SURVEY`. Unit A is confirmed in temporary `MODE BASE TIME 60 2.5 3.5`. The recommended COM2 RTCM MSM4 commands were accepted, and a CRC-validated Wi-Fi bridge is installed, but the antenna-less bench setup produced no RTCM frames. A usable base position and live RTCM forwarding remain untested.

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

- Unit A: temporary base-test role confirmed through the ESP32; no valid averaged position or RTCM validation yet.
- Unit B: confirmed rover in `MODE ROVER SURVEY`.
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

## Rover Configuration

Use `MODE ROVER SURVEY` for the intended survey-rover dynamics. Record RTCM input, solution output, raw observations, and status messages separately.

## RTCM Profile

| Message | Rate | Purpose | Measured bytes/s | Validated |
|---|---:|---|---:|---|
| RTCM 1006 | 0.1 Hz | Base antenna reference point with height | Pending live stream | Command accepted on Unit A COM2 |
| RTCM 1033 | 0.1 Hz | Receiver and antenna descriptors | Pending live stream | Command accepted on Unit A COM2 |
| RTCM 1074 | 1 Hz | GPS MSM4 observations | Pending live stream | Command accepted on Unit A COM2 |
| RTCM 1084 | 1 Hz | GLONASS MSM4 observations | Pending live stream | Command accepted on Unit A COM2 |
| RTCM 1094 | 1 Hz | Galileo MSM4 observations | Pending live stream | Command accepted on Unit A COM2 |
| RTCM 1124 | 1 Hz | BeiDou MSM4 observations | Pending live stream | Command accepted on Unit A COM2 |

The initial command set follows the manufacturer's base-station example. All commands were sent without `SAVECONFIG`. During the antenna-less bench test, both bridge counters remained zero because Unit A had no valid base position or satellite observations. See the [Wi-Fi RTCM bridge bench test](../tests/2026-09-06-wifi-rtcm-bridge-bench/README.md).

## Known-Good Configuration

Link the exact exported configuration and the test record that validated it.

## Recovery

Document reset, factory-default, firmware-update, and lost-communication procedures.
