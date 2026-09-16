#pragma once
// Web status surface (R10a slice 6), extracted from main.cpp. Owns the status
// JSON the HTTP layer serves and the loop's publication path: the same fields in
// the same order with the same values and the same 250 ms window.
//
// It formats only. The status facets, the correction age and health state, the
// fix label and the accuracy text come from their owners (instrument_status,
// ui_presenter) and the published service snapshots, with no rendering and no
// hardware; the HTTP owner receives one copied snapshot, exactly as before.

#include <cstddef>
#include <cstdint>
#include "instrument_status.h"

namespace status_surface {

// The publication window: true at most once per 250 ms, recording the time it
// accepted. The loop asks before it composes an evaluation.
bool due(uint32_t now_ms);

// The status JSON for one evaluation, identical to the bytes main.cpp served.
// 0 when it does not fit in `capacity`, which the publisher then sends empty.
size_t format(char *output, size_t capacity,
              const instrument_status::Inputs &inputs);

// Formats and hands the JSON to the HTTP owner's published snapshot.
void publish(const instrument_status::Inputs &inputs);

}  // namespace status_surface
