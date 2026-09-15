#pragma once
#include "debug_core.h"
// HTTP calls and the main-loop observer synchronize only fixed-size copies.
bool debug_enabled();
void debug_enable_local(bool enabled);
bool debug_disable();
void debug_observe(debugmode::Channel channel,const char *text);
void debug_frame(debugmode::Channel channel,const uint8_t *frame,size_t size);
bool debug_status(char *out,size_t capacity);
bool debug_logs(char *out,size_t capacity);
