# Temporary implementation: UM980 COM1 transmit fault

Recorded 2026-09-11. The owner plans to order a replacement UM980/carrier. This note records the interim recommendation; it does not change receiver roles, firmware, wiring or saved configuration.

## Recommended assignment

Use the **known-good UM980 as Base** and the **suspect UM980 as Rover**, temporarily for prototype testing. This is an engineering recommendation for containing risk, not a manufacturer-approved use of a damaged receiver. The Base supplies the reference and observations used by the Rover, so keeping the healthier board there avoids deliberately making the suspect receiver the shared reference source. This does not make the suspect Rover's results trustworthy or establish that its fault is isolated.

Physical receiver-to-ESP32 Unit A/B identity is not established by these measurements. Do not infer identity from a Windows COM number or historical Base/Rover labels; identify the suspect carrier physically before changing the assignment.

## Reported evidence

With USB power, external ESP32/radio wiring removed, and COM1 output stopped using `UNLOG COM1`, the owner reported:

| Signal | Voltage relative to board ground |
|---|---:|
| TTL_TXD1, measured at the connector solder pad | +0.0215 V |
| TTL_TXD2 | +3.28 V |
| RS232_TX1 | +5.96 V |
| RS232_TX2 | -5.45 V |

USB communication works and identifies `UM980 R4.10Build13504`. A temporary `GPGGA COM1 1` command was accepted but did not restore a valid TTL_TXD1 level. No configuration save was made during that test. No obvious hard short was detected by the owner's resistance/continuity check.

The signed TTL/RS232 measurements are consistent with channel 1 being low and channel 2 idling high, assuming corresponding carrier channel labels share the same UART. They do not distinguish a module output failure from carrier loading, protection/interface damage, or an upstream connection fault. The seller manual contains a board layout and connector assignments, not a complete schematic; no internal net or converter part number has been verified. RXD1 functionality remains untested.

## Current temporary connection

Keep COM1 disconnected on the suspect board. Use COM2 bidirectionally between each UM980 and its ESP32, and retain the existing ESP32 Wi-Fi correction bridge:

```text
Known-good Base UM980 COM2 <-> Base ESP32
                                 |
                      Existing Wi-Fi RTCM link
                                 |
Suspect Rover UM980 COM2 <-> Rover ESP32
```

On the Waveshare board, TTL_TXD2 connects to GPIO44/RX and TTL_RXD2 to GPIO43/TX, with a common ground. Follow the existing [UART/power plan](esp32-uart-integration.md), including leaving the 5 V interconnection absent when both devices have USB power. The earlier COM2/Wi-Fi RTK tests establish a working architecture, not a fresh acceptance test of the newly reported fault state or proposed receiver assignment.

Before continuing outdoor prototype measurements, verify COM2 command/response and continuous output, absence of resets or abnormal heating, correction reception and an RTK fixed solution. Use repeated observations of an independent check point to assess the session. Stop using the suspect board if additional instability appears. Replacement remains the plan before trusted field operation.

## SiK implications and correction to earlier blanket advice

The direct-radio plan uses COM1 for corrections and COM2 for the ESP32. The essential one-way correction path is **Base TXD1 -> Base radio -> Rover radio -> Rover RXD1**. A failed Rover TXD1 therefore does not by itself rule out that direct receive path. However, the suspect board's RXD1 and shared carrier circuitry have not been qualified, so **this is a possible later test, not the current approved wiring**. Radio logic levels and any required reverse traffic must also be checked.

The subsequently selected approach is a separate ESP32 UART for each SiK radio, with firmware forwarding RTCM between that UART and UM980 COM2. TX GPIO17 / RX GPIO18 are selected with the camera socket unused. The radios have since been USB-configured and backed up; see the [radio checkpoint](../../radio.md). The existing Wi-Fi bridge does not mean the SiK UART bridge is implemented or validated. COM1 remains disconnected in this approach.

## Replacement and closeout

- Order the replacement; receipt/purchase is not yet confirmed.
- Identify the faulty carrier physically and retain this fault history.
- Verify replacement USB communication, COM1/COM2 idle levels and bidirectional data before radio integration.
- Reconfirm role, base coordinates, antenna measurements and correction profile after swapping a receiver. Preserve job records and do not treat historical tests as validation of the replacement.
- Retire this temporary exception after replacement and successful hardware checks.

References: [carrier manual](references/bdrtk-980-board-manual.pdf), [UM980 manual](references/um980-user-manual-r1.10.pdf), and [recorded GNSS/RTCM tests](../../gnss-rtcm.md). Unicore documents three UARTs and the TXD/RXD directions; it does not establish the reliability of this suspect board or prescribe its temporary role.
