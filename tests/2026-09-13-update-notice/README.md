# Update-notice protocol foundation

Date: 2026-09-13. Baseline: `d6dd846` plus this protocol core. No firmware flashing, receiver/radio configuration or field data changes.

Checkpoint reason: five-hour allowance reached 9% remaining. Implementation paused; documentation, source and evidence were prepared for commit/push. Weekly usage was not used as the stopping criterion.

Run `python firmware/um980-display-demo/test/run_update_tests.py` from the repository root. Requires Python and g++ on PATH; does not require connected hardware. The runner compiles `update_notice_cases.cpp` with C++17, `-Wall -Wextra -Werror -O2`, then executes it.

Result: **PASS**. Coverage includes all 320 single-bit packet corruptions, malformed/reserved fields, sender retry budget, wrong session/unit/role/attempt/phase acknowledgements, notice loss and reordering, cancellation preceding prepare, duplicate deadline preservation, timer wrap, and recovery requiring fresh identity and quality. One thousand deterministic fault schedules finish overdue instead of remaining stuck Updating. A regression found and fixed acceptance of a late duplicate after recovery; closed recovered attempts now reject it.

This is a portable codec/state-machine test, not a test of an ESP32 update, live peer notification or RF performance. The core has no flash/UART/receiver calls or heap allocation. Runtime integration, authenticated pairing, acknowledgement scheduling, image validation, update exclusivity, boot acceptance, rollback and hardware validation remain outstanding. Current deployed firmware remains 0.10.4; 0.10.5 passive Debug source remains unflashed. Upload is still disabled.

See the [implementation checkpoint and next steps](../../docs/debug-and-ota.md).
