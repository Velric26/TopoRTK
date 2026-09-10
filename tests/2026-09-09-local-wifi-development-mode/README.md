# Local Wi-Fi Development Mode

> Status: **PASS — transport switching and peer discovery.** Power-cycle
> persistence remains to be included in the next normal field power-cycle.

## Purpose

Validate the reversible local-router Wi-Fi transport without changing the
UM980 role, receiver profile, or field-capable direct-link fallback.

## Preconditions

- Unit A and Unit B flashed with this checkpoint.
- Both use the same configured 2.4 GHz local router credentials in the
  Git-ignored `src/wifi_credentials.h`.
- PC is connected to that same router network.
- One unit is selected as Base and the other as Rover.

## Procedure

1. On each unit's native USB console, issue `wifi local`.
2. Wait up to 20 seconds, then issue `wifi?` on each unit.
3. Record each non-zero router-assigned IP address and confirm the Link screen
   reads `MODE LOCAL ROUTER` and the configured SSID.
4. Confirm Unit B reports packet receive activity and Unit A reports a peer;
   the Base must reply to the Rover's UDP broadcast discovery.
5. Confirm the instruments' selected roles and profile status remain unchanged
   with `config?`.
6. Issue `wifi direct` on both units. Confirm the Base returns to
   `192.168.4.1`, the Rover reconnects, and packet activity resumes.
7. Power-cycle both units and confirm `wifi?` reports the most recently chosen
   transport. Restore `wifi direct` after the test unless the next browser
   checkpoint specifically requires local router mode.

## Pass Criteria

- Both instruments join the router in `LOCAL ROUTER` mode and exchange valid
  peer packets.
- Their assigned router IP addresses are visible without showing a password.
- `DIRECT LINK` remains functional after toggling and after a power cycle.
- No UM980 role/profile change, new receiver command, or SD write failure is
  caused by the Wi-Fi toggle.

## Result — 2026-09-09

| Check | Result |
|---|---|
| `LOCAL ROUTER` Base | Unit B / COM10 joined router at `192.168.100.19`, RSSI `-51 dBm`, UDP ready, Rover peer discovered at `192.168.100.20` |
| `LOCAL ROUTER` Rover | Unit A / COM4 joined router at `192.168.100.20`, RSSI about `-58 dBm`, UDP ready; 28 valid sequenced packets received with zero gaps and zero invalid packets |
| `DIRECT LINK` fallback | Base AP started at `192.168.4.1`; Rover joined at `192.168.4.2`; 31 valid packets received at `-20 dBm`, zero gaps and zero invalid packets |
| Return to `LOCAL ROUTER` | Both rejoined and resumed valid peer traffic; Base peer `192.168.100.20`, Rover 28 packets, zero gaps/errors |
| Receiver/configuration | Base and Rover roles remained selected; `config?` reported saved storage and verified profile; no Wi-Fi toggle sent a UM980 command |

Both instruments were left in `LOCAL ROUTER` mode for browser-interface
development. The console reported successful NVS saves. A separate physical
power-cycle check will confirm restoration of the last selected transport.

## Next Step

Implement and validate the Rover's read-only browser status page at the
Rover's local-router address. Start with a responsive `GET /` page and a
versioned `GET /api/v1/status` snapshot. Confirm from the PC that refreshes
show current link state, GNSS fix state, horizontal uncertainty, Wi-Fi quality,
and warnings while the peer packets continue. Do not introduce a write endpoint
or reconfigure the UM980 in this checkpoint.
