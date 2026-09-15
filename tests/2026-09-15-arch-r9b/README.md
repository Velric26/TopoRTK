# R9b — quick tests on the Settings page (0.11.30-arch-r9c, 2026-09-15)

The Settings page now offers the paired quick tests the R9a service provides: two warned buttons, the operator-specified fault profile, a live countdown and the durable results the instrument stores per medium.

## What the page does

- **Quick tests section** with `Test Radio (SiK)` and `Test Wi-Fi`, plus an `Inject faults (radio only)` checkbox. The copy states what a test is: both instruments transmit and count for about half a minute, correction work pauses while it runs, and generated frames are only exchanged between the two instruments — never sent to the survey receiver.
- **A warning before anything is sent.** Clicking a test opens the same confirmation step a route switch uses, with test-specific wording (run length, corrections paused, collection should be idle), the injected profile's consequence spelled out ("expect failures to be reported — that is the point of this profile"), and an explicit statement that the current route is unchanged. `Keep the current route` sends **nothing at all**.
- **Confirm** posts `{id, revision, op:'link.test', transport, confirm:true}` with a fresh 32-hex id and the revision the page last read, adding `{"profile":"injected"}` only for the radio test with the box checked. `202` is reported as accepted, never as a result.
- **Countdown** for the run phase uses the operation's own 60 s window, so the phase text ("up to N s left in this phase") and the bar track a test as well as a switch.
- **Outcome copy** is kind-aware and reason-aware: a test reads preparing/running/passed, and a failure distinguishes `test_unavailable` ("could not start … Nothing was tested") from `peer_unreachable` ("ended without the other instrument reporting its own counters, so it is not a pass") from any other reason ("did not pass"). None of them claims the route changed.
- **Durable results**: `#testResults` renders each medium's stored report — verdict, frames received, frames sent, errors, run id, state/reason — which is what an operator reads after reconnecting, because it comes from the instrument, not the browser session.

## The defect this step exposed

The live run rendered `Wi-Fi: passed … (run 610475, d_rt/ssuite_)` — the verdict was right but the stored strings were mangled. Root cause, found and reproduced off-target by the engine agent: `refresh_tests_summary` parsed each medium's body into a `DynamicJsonDocument` declared **inside the loop** and assigned its `const char*` fields into the summary. ArduinoJson *links* a `const char*` rather than copying it, so the first medium's entry pointed into a destroyed document — and the second parse reused the same freed block. Integers and booleans survived because they are stored by value, which is exactly the shape of the corruption observed (counters and `pair_pass` intact, `state`/`reason`/`role` scrambled).

Fixed by assigning those fields through `String(...)` (copied storage), raising the summary pools now that the strings are real copies, and adding a tolerant read path: a stored body is only published if it is a whole report (terminal state and a valid role), otherwise that medium reads `null`, its NVS slot is removed and the raw snapshot stops showing fragments. The NVS bodies were never damaged — only the derived summary — so the instruments self-healed on the next refresh after flashing, which the verification below confirms by reading clean strings and a consistent sender/receiver split.

## Verified

**Deterministic check** (`check_settings_browser.cjs`, 31 cases, ~32 s): nothing-stored state; stored verdicts with frame/error counts and run ids, including a stored failure reading as not-passed; the warning naming both counting instruments and the unchanged route with cancellation sending zero POSTs; one `link.test` POST with a fresh id, the fixture revision and no profile; the injected profile on the radio test and a `503` refusal reading as unavailable; the countdown (≈60 s left with an empty bar at `phase_remaining_ms:59000`, near-full at `1000`); the three failure copies reading distinctly; and a running test surviving a takeover while the earlier page goes view-only and cannot start another — with a reload retrieving the same operation and no request of its own.

**Live, driven from the page in a browser on the Rover** (`quick-test-from-page.json`, screenshot `settings-quick-test.png`, `0.11.30-arch-r9c`):

| Case | Observed |
|---|---|
| Results on load | `Radio: not passed, 12 frames received, 0 sent, 0 errors (run 978395, done/complete)` · `Wi-Fi: …` — clean strings, sender and receiver both shown |
| Warning cancellation | `Run the quick test` label, warning names the run and the unchanged route, operation section stayed hidden, route Wi-Fi before and after, **no request sent** |
| A real quick test from the page | countdown observed from `up to 60 s` down to the verdict; final copy **"Quick test passed on Wi-Fi. The selected route is unchanged."** with `succeeded/applied`; route Wi-Fi before and after; the results line updated to `Wi-Fi: passed, 117 frames received` |
| Stored strings after the fix | All four entries on both units read `done/complete` or `failed/operation ended` with the right role; Base sent 117, Rover received 117 |

Both units run `0.11.30-arch-r9c`, Wi-Fi selected, revision 10, jobs untouched.

## Acceptance rows now covered

- Cancel warning sends no command; a confirmed test runs the specified profile through the device-owned phases with a countdown; the stored result is restored after reconnecting.
- A test survives browser lease loss and takeover; only the current controller can cancel (the API's bearer check plus the page's `operation.id` matching).
- A test on either medium never changes the selected route, and its frames never reach the receiver (proved in R9a: 0 of 32 Debug-log entries on receiver channels).

Still open from the R9 table: peer-reboot during an admitted test, and the unselected-medium case with no preparation on the old medium driven from the UI (the service side of both was exercised in R9a).
