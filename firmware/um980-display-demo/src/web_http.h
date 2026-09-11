#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kWebStatusCapacity = 2048;
constexpr const char *kWebUiVersion = "0.8.0";

// Called from the main loop. The HTTP task only reads a copied snapshot.
void publish_web_status(const char *json, size_t length, uint32_t now, bool rover);
uint32_t web_boot_id();
