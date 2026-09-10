# Survey workflow — UI 0.3

This implements the initial scope of roadmap ranks **2–5**, requested together by the user: durable jobs, coordinate/height setup, base/antenna setup and stationary point collection. New jobs receive a Zapopan starter profile, but remain unconfigured until the operator reviews the values, enters the actual antenna measurements and control information, and explicitly saves the setup.

## Use

1. Open the Rover's address and choose **Open survey jobs**, or open `/survey` directly. On the bench, Unit A is `http://192.168.100.20/survey`; on its phone AP use `http://192.168.8.1/survey` (check the touchscreen for the current address).
2. On that instrument, open **Link → Phone / Tablet → Show key**. Use its six-digit **WEB CONTROL PIN** in the browser to take control. This is separate from the saved eight-digit Wi-Fi password with a middle period.
3. Create or open a job. Jobs, the active selection, configuration revisions and committed points live on Rover SD. Browser storage retains only the current pairing/request state.
4. Complete **Setup**: review the Zapopan starter values, confirm the WGS84 source and epoch, enter both antenna reference measurements, identify the RTCM base/station, choose known control or explicitly accept a temporary base, and review the occupation/quality limits. Save only after the complete configuration is correct.
5. In **Collect**, enter a unique point ID, optional code and description. Hold the pole still and level. The occupation must remain RTK FIXED and within all limits. A success message appears only after the point record is written, flushed and read back.
6. The last committed point is shown with coordinates, ground height, configuration revision and control status. Full point review/plot and export are the next roadmap items; they are not included in this release.

Multiple devices may view the instrument. Only one paired browser may write; control expires after 120 seconds without an authenticated request. Releasing control, rebooting, changing role/network or replacing the phone Wi-Fi key revokes the session. Five incorrect PIN attempts within a minute temporarily block pairing. The PIN is hidden on the display after 30 seconds and is absent from status/USB/SD logs.

The Base has its own `/survey` page (also its root page), reached through its existing network. Unit B's bench address is `http://192.168.100.19/survey`; addresses are DHCP and may change. Pair using **the Base's** display PIN. Applying fixed coordinates or temporary survey-in requires confirmation on this page and interrupts corrections while the profile is verified. The Rover job checks the received base reference; it does not remotely command the Base.

## Coordinates and antenna references

### Zapopan starter profile

When a new job has no saved configuration, the browser pre-fills the following
convenience values for the project's expected work area (approximately
20.77° N, 103.41° W):

| Field | Starter value | Why it is safe to prefill |
|---|---|---|
| Coordinate format | WGS84 / UTM | The current prototype implements WGS84 UTM without datum transformation |
| UTM zone / hemisphere | 13 / Northern | Zapopan lies in UTM zone 13N |
| Units | Metres | Survey setup and quality limits are defined in metres |
| Reference frame | WGS84, no transformation | The only implemented frame; still requires source confirmation |
| Coordinate epoch | 2026.69 (approximate 2026-09-10) | A current-date metadata starter; replace it with the epoch documented for the control coordinates |
| Height reference | Ellipsoidal ground height | Does not invent a local geoid separation |
| Antenna model | GNSS HA-609 for Base and Rover | These are the antennas currently installed in the prototype bodies |
| Antenna method | Vertical | Assumes a level pole; measured height and offset are still required |
| Base source | Temporary / unverified | No surveyed control point was supplied; explicit acceptance is still required |
| Occupation / quality limits | 5 s, 5 epochs, H 0.03 m, V 0.05 m, correction age 3 s | Conservative prototype defaults that remain editable |

The starter does **not** check any confirmation boxes and does not guess
antenna heights, offsets, control coordinates, or station identity. Existing
saved job configurations are never overwritten by these defaults.

### Values still requiring operator input

- Confirm that the correction source/control coordinates are WGS84 at the
  chosen epoch. A `FIXED` solution does not prove the datum or epoch.
- Measure Base and Rover antenna reference heights from the survey mark/pole
  reference to the antenna reference point, and enter any signed offset and
  slant radius. These values directly affect reported heights and cannot be
  inferred from `HA-609`.
- Confirm the RTCM station ID shown from the live Base reference. It is a
  transmitter identity, not a Zapopan coordinate, and may change with Base
  configuration.
- For survey-grade work, provide the known control name, latitude, longitude,
  and **ground ellipsoidal** height. If unavailable, use Temporary only for
  local/relative testing; points remain explicitly unverified.
- Provide a locally validated geoid separation and validity area only if
  orthometric elevations are required. Otherwise keep Ellipsoidal selected.
- Adjust occupation duration, minimum epochs, horizontal/vertical uncertainty,
  and correction-age limits to the client's acceptance criteria. The defaults
  are not a legal or absolute-accuracy guarantee.

- Initial supported frame: **WGS84**, without datum/epoch transformation. An entered epoch is metadata, not a velocity correction. Other reference frames, site localization and geoid grid files are not implemented.
- Initial projection: standard six-degree WGS84 UTM zones 1–60, within ±3° of the central meridian and latitude −80° to +84°. Hemisphere must match. Norway/Svalbard widened zones and out-of-zone extensions are not supported. Geographic WGS84 output is also available.
- Coordinates are **grid**, without grid-to-ground scaling. Units are metres or international feet (exactly 0.3048 m); setup measurements and uncertainty limits always use metres.
- BESTNAV supplies latitude, longitude, MSL height and undulation. Receiver ellipsoidal height is `h = MSL + undulation`. Ground ellipsoidal height subtracts the entered Rover antenna reference height.
- Vertical output is either ground ellipsoidal height or `H = h_ground − N` using an explicitly named, locally validated constant geoid separation. Its centre and radius (maximum 10 km) are required; collection is blocked outside that area. This is not a general geoid model or a national vertical datum transformation.
- Antenna setup records a model/reference description, measured height, vertical/slant method, horizontal slant radius and signed reference offset. Effective vertical reference height is `measured + offset`, or `sqrt(slant² − radius²) + offset`. No calibration is inferred from the model name.
- Known base control uses ground **ellipsoidal** height plus the entered Base antenna reference height. This expected ECEF reference must match the received RTCM 1005/1006 reference within the configured tolerance. A temporary base requires explicit acceptance; its points retain `base_control_verified: false`.
- The Base receiver's fixed setup takes **receiver-reference** ellipsoidal height, including antenna height/offset, rather than ground elevation. Settings persist in ESP32 NVS and are reapplied at startup. Fixed setup completes only when the profile is verified and the broadcast reference agrees within 2 cm.

Each saved point retains its complete configuration and revision, unadjusted averaged receiver position, derived ground/grid position, antenna setup, base reference, UTC labels, number of distinct epochs, duration, minimum satellite count, worst reported H/V uncertainty and worst correction age. Earlier points are never reinterpreted after setup changes. These records are not raw carrier-phase/RINEX observations.

## Quality and interruption behavior

The collector requires a verified Rover profile, checksum-valid BESTNAV with a valid GPS epoch and WGS84 datum, RTK-fixed BESTNAV and GGA, fresh GNSS data, matching solution/RTCM station IDs, a fresh base reference, correction link, and configured uncertainty/correction-age limits. Correction age uses the greater of transport age and receiver-reported differential age. Zero differential age means no differential correction and cannot qualify.

Only distinct increasing GNSS epochs contribute to the ECEF mean. Loss of fix, a stale or reversed epoch, a gap over 1.5 seconds, changed station/reference, failed quality limits or leaving the selected projection/geoid area aborts the occupation. Cancellation does not save a point. Duplicate point IDs in a job are rejected. Reboot interrupts active occupations and receiver-setup requests; it never resumes them silently.

The browser locks writes on connection loss, preserves edits during polling, and retries an uncertain request with the **same ID and payload**. Replaying a committed request returns its previous result without duplicating the action.

## Storage and API

`/TOPO-RTK/SURVEY` on the SD card contains a sequential append-only journal. Each record has a version, command ID, payload fingerprint, typed operation, result, JSON body, CRC and commit trailer. A new record is written to a temporary file, flushed/synced, renamed to its final name and read back before success. Committed files are never overwritten. An incomplete temporary file is ignored; a missing committed sequence, invalid CRC, truncated record or invalid event disables writes. Recovery replays valid committed records. Back up a faulty card before manual repair; recovery does not erase it.

Prototype bounds: **16 jobs, 512 distinct durable command IDs and 1,024 journal records**. Occupations reserve capacity for their final/cancellation records. No job deletion, archive/rotation, import, export or full point list yet. Changing these bounds needs memory/latency validation. The SD journal is authoritative; do not remove the card while an operation is active. Flush/readback cannot eliminate every consumer SD controller or FAT power-loss failure; physical power-cut/card-removal tests remain a separate checkpoint.

| Endpoint | Purpose |
|---|---|
| `GET /api/v1/status` | Existing read-only Rover dashboard; Base returns 409 |
| `GET /api/v1/survey` | Jobs/current setup, collection/result, quality block, base reference and boot/memory diagnostics; `X-Controller` indicates current ownership |
| `POST /api/v1/control` | Claim a lease using `{pin, client}`; returns a bearer token |
| `POST /api/v1/control/release` | Release the authenticated lease |
| `POST /api/v1/command` | Typed, authenticated command; 202 means queued, not completed |

Commands are `job.create`, `job.open`, `job.configure`, `collect.start`, `collect.cancel`, `base.apply` and `storage.recover`. Every command requires a 32-character lowercase hexadecimal ID. Configuration writes also require the expected job revision and confirmation. No arbitrary receiver passthrough exists. POST bodies are bounded JSON, cross-origin browser writes are rejected, responses disable caching, and the pages contain no external assets. Coordinate/job data are readable to connected viewers; control credentials are not included in snapshots.

The HTTP task queues commands. A separate survey worker owns the engine and journal. Main-loop typed requests own receiver/NVS changes; GNSS/RTCM handling continues independently. A mutex serializes SD use, and diagnostic CSV writes skip a busy card rather than blocking the GNSS loop. Larger allocations use board PSRAM. Survey snapshots older than two seconds return 503.

## Validation and remaining acceptance

See [test record](../tests/2026-09-10-survey-workflow/README.md). Native tests exercise production collection/storage/control logic and real journal files, including ambiguous writes, corruption, replay, duplicate requests, cancellation, quality loss, coordinate math and base verification. The production browser page is tested against the production C++ engine at phone, tablet and desktop sizes. Ninety-six UTM examples agree with independent PROJ within 0.8 mm.

Open-sky collection against known independent control, correct antenna reference interpretation on the actual antennas, fixed-base broadcast confirmation on hardware, real SD power-loss/card-removal behavior and phone/tablet field use still require validation. These are not established by simulated GNSS epochs or a successful firmware flash.

Technical references: [Unicore N4 command reference, BESTNAV and MODE BASE](https://en.unicore.com/uploads/file/20241219/Unicore_Reference_Commands_Manual_For_N4_High_Precision_Products_V2_EN_R1.4.pdf), [PROJ UTM](https://proj.org/en/stable/operations/projections/utm.html), [RTKLIB RTCM reference-station encoding](https://github.com/tomojitakasu/RTKLIB/blob/master/src/rtcm3e.c).
