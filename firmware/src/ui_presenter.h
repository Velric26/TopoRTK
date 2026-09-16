#pragma once
// Presentation mapping (R10a slice 6), extracted from main.cpp. Owns the one
// UiFrame the renderers draw: every page string, region value and color, plus
// the text the LCD, the web status JSON and the CSV row share.
//
// It renders nothing, touches no hardware and makes no decision that belongs to
// a service: it reads the published service snapshots, the screen-owned page
// state and the single status evaluation `instrument_status::Inputs` the
// composition root composes. The renderers and ui_display's region repaint
// cache are untouched.

#include <cstddef>
#include <cstdint>
#include "instrument_status.h"
#include "ui_screens.h"

namespace ui_presenter {

// The receiver accuracy text the dashboard, the GPS page, the web status and the
// CSV row show ("---", "N/A (BASE)", mm, cm, m).
void horizontal_accuracy(char *output, size_t output_size, uint32_t now_ms);

// The persisted brightness label the LCD and the console show.
const char *brightness_label();

// The Link-mode page's refusal line. The gesture dispatch owns the decision; the
// page's copy is presentation state, so it lives here. `set` stores the text
// upper-cased exactly as the root used to, and an empty text clears it.
void set_link_hint(const char *text);
void clear_link_hint();

// Builds the frame for the screen's current page. `inputs` carries the
// evaluation time and the status facets the header, cards and JSON agree on.
void build(UiFrame &frame, const instrument_status::Inputs &inputs);

}  // namespace ui_presenter
