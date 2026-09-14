#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kWebStatusCapacity = 2048;
#define TOPORTK_FIRMWARE_VERSION "0.11.5"
constexpr const char *kWebUiVersion = TOPORTK_FIRMWARE_VERSION;

// Called from the main loop. The HTTP task only reads a copied snapshot.
void publish_web_status(const char *json, size_t length, uint32_t now, bool rover);
uint32_t web_boot_id();
