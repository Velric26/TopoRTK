#pragma once
#include <cstddef>
#include <cstdint>
#include "update_package.h"
void ota_boot_begin();
void ota_service(uint32_t now,bool rover,bool profile_busy,bool local_healthy);
bool ota_locked();
bool ota_paused();
bool ota_request(const char *json,const char *token);
bool ota_status(char *out,size_t capacity);
// Synchronous HTTP upload, after main-loop admission and explicit confirmation.
bool ota_upload_begin(const char *token,size_t content_length);
bool ota_upload_write(const uint8_t *bytes,size_t size);
bool ota_upload_finish();
void ota_upload_abort(const char *reason);
// Main-loop adapter: clears forwarding/partial GNSS buffers at both boundaries.
void ota_reset_corrections();
bool survey_service_ready();
bool web_service_ready();
