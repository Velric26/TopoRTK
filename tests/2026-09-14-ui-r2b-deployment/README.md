# R2b sunlight-readable UI deployment — 0.11.8-ui-r2b (2026-09-14)

Deployment of the [architecture review](../../docs/architecture-review.md) R2b checkpoint on the R2a module seam: the light high-contrast palette, the size-2 typography floor, two-pixel control outlines, and two-page GPS/Link detail layouts. Day/Auto/Night backlight behavior, persisted brightness, and all hit-test semantics for untouched controls are unchanged.

## Contract implemented

- Palette tokens in `ui_theme.h` per the review table (white cards/black primary text 21.0:1, secondary 10.2:1, green READY banner with black text 15.3:1, grey NOT READY 9.1:1, navy selected control with white text 15.8:1, ready-state text 7.7:1, error text 8.1:1, amber warning panel with black text 19.6:1). Green appears only for ready/connected/fixed states.
- `draw_fitted_text` never shrinks below size 2; primary card readings render at size 3; status-policy colors (fix quality, link quality) remapped to white-background-legible tokens; severity remains in words.
- GPS and Link details are two pages of six 52-pixel rows (size-2 label/value on separate lines) with PREV/NEXT at y=380 (44 px tall); the Phone entry lives on Link page 2. All twelve GPS fields and full coordinate precision are preserved across the two pages.
- Concise equivalent strings replace truncated size-1 text; the word-wrap helper keeps warning details on two readable lines. Header subtitles shortened to the 25-character size-2 budget (`LINK CONNECTED/DOWN`, `ROLE & BRIGHTNESS`, `PASSIVE MONITOR`, `WEB UI <version>`, `READY 23:18 -DBG` style), with Debug shown by a compact `-DBG` suffix instead of an appended sentence.
- New/changed hit regions (`kDetailPrev`, `kDetailNext`, `kPhoneLink`) are defined in `touch_layout.h` and consumed for both drawing and input; `touch_action` takes the detail page. Minimum action size stays ≥44 px; swipes and rotations are unchanged.

## Software verification

- Host tests: all 7 blocks PASS, including production touch navigation, UI bounds (the host display assert rejects any off-screen glyph) and the repaint cache; new regression asserts cover PREV/NEXT pagination on both detail pages and the relocated Phone entry.
- Rendered previews reviewed for every page; two truncation defects found and fixed during review (settings status/brightness line, Android note wrap).
- `pio run -e unit_a -e unit_b` — both SUCCESS; packages identity-verified before flashing.

## OTA deployment (scripted runner)

Both units: Update complete, new boot verified, Debug On after restart, saved survey state unchanged, no browser errors. Post-update API state: firmware `0.11.8-ui-r2b`, idle, receiver profile verified, corrections on Wi-Fi, storage intact (A: 2 records; B: 0).

## Outstanding

- Operator confirmation of the new layout on both panels (pagination buttons, relocated Phone entry, readable text indoors).
- Direct-sunlight readability acceptance on both physical instruments remains the R2b human gate — indoor bench checks cannot certify it.

## Field-feedback revision — 0.11.9-ui-r2b2 (2026-09-14)

Operator feedback on the first R2b panels: the two GPS pager buttons were redundant, and the Link-page Phone button misbehaved. Changes, deployed to both units with the scripted runner (verified boots, unchanged saved state):

- GPS details use one full-width button that flips between the two pages (`PAGE 1`/`PAGE 2` label shows the destination).
- The Phone screen is now the **third Link page** (rows → counters → phone); the standalone Phone button and page are gone. Link keeps PREV/NEXT across its three pages, `ui_cycle_detail_page` wraps per page count, and tapping the active tab returns to its first page.
- Host tests updated for the new navigation (GPS flip, Link three-page cycle, phone actions on page 3) and all blocks PASS with no off-screen glyph.

## Field-feedback revision — 0.11.10-ui-r2b3 (2026-09-14)

Operator feedback: the Link phone page had no way back. The phone page's lower section was compressed (browser address plus a two-line note) so **PREV/NEXT now appear on all three Link pages**, including the phone page. Deployed to both units with the scripted runner (verified boots, unchanged saved state); host tests all pass with no off-screen glyph. A full two-page split was evaluated and rejected: the ten detail rows plus the phone content cannot fit two pages without dropping operational counters.

## Link/key overlap repair — 0.11.11-ui-r2b4 (2026-09-14)

The operator reported overlapping SHOW KEY/NEW KEY controls. The shared rectangles were already separate; PREV/NEXT changed only the detail-page index and performed an incremental render. Old counter rows, key controls and cached regions therefore survived transitions between different layouts. The old standalone Phone preview bypassed this path and did not establish that Link-to-Phone navigation was clean.

- Detail navigation now clears the previous layout and invalidates its cache once. Periodic unchanged frames remain cached. The three Link pages and existing key/pager hit rectangles are preserved.
- Leaving Phone by paging or active-tab reset hides the key and cancels pending replacement, just as leaving the tab does. The 30-second reveal and 10-second confirmation limits remain unchanged.
- Phone role/router/address fields are now built when Phone is actually visible, not only after visiting Setup. Base displays its own address and disallows Rover key actions. The obsolete standalone Phone screen and preview path are removed.
- The new framebuffer regression failed before the fix (`display->pixels == navigated_pixels`) and passes afterward. Existing host checks pass all seven blocks, now covering all six directed Link transitions, key states, cancellation, GPS paging, drawing bounds and unchanged-render caching.
- A throwaway production touchscreen smoke exercised raw touch input for Base and Rover at rotations 0 and 2: all four combinations passed, including the gap between key buttons, SHOW/NEW/CONFIRM, wrap/back navigation and clean rendering. Relevant host-rendered previews were visually inspected. The smoke source/executables were removed; private previews remain under `.pio/ui-smoke-*.png`.
- Both PlatformIO environments built successfully; each packaged image is 1,241,104 bytes and carries the correct hardware identity and version `0.11.11-ui-r2b4`.

These are host-raster and automated touch checks, not a physical LCD capture. Operator confirmation of the corrected display and the separate sunlight acceptance remain outstanding.

### Final display-availability guard — 0.11.12-ui-r2b4

Both units first accepted `0.11.11-ui-r2b4` through guarded OTA. Before closing the repair, the new static redraw was also guarded by `display_ready`, matching existing tab navigation: if display initialization fails, paging must not issue hardware drawing calls. A regression covers that condition. All seven host blocks, both PlatformIO builds and the four Base/Rover × rotation-0/2 smoke combinations passed again on `0.11.12-ui-r2b4`. Final package image size: 1,241,120 bytes per unit. The temporary smoke programs were removed.

Final guarded OTA completed sequentially on both instruments, with acknowledged peer preparation, no unconfirmed override, accepted new boots, Debug On after restart, unchanged saved survey snapshots and no browser JavaScript errors:

| Unit / role | Final firmware | Previous boot | Accepted boot | Private OTA evidence directory |
|---|---|---:|---:|---|
| A / Base | `0.11.12-ui-r2b4` | 3731550340 | 1083173989 | `.pio/ota-live-1789440816715` |
| B / Rover | `0.11.12-ui-r2b4` | 426530613 | 1599058143 | `.pio/ota-live-1789440870464` |

Evidence directories are relative to `firmware/um980-display-demo/` and contain `ota-evidence.json`, the pre-update survey snapshot, and review/completion browser screenshots. Post-update survey APIs report both receiver profiles verified, storage ready and collection idle; records remain A=2, B=0. Physical touchscreen/solar-readability acceptance is not claimed by these OTA checks.
