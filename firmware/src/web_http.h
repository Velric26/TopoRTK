#pragma once
#include <cstddef>
#include <cstdint>

constexpr size_t kWebStatusCapacity = 2048;
// Deliberate rollback-acceptance builds carry distinct version strings; inert
// in normal builds, where the version is only defined here.
#if defined(TOPORTK_ROLLBACK_TEST_FAIL_HEALTH)
#define TOPORTK_FIRMWARE_VERSION "0.11.5-rollback-test"
#elif defined(TOPORTK_ROLLBACK_TEST_HANG)
#define TOPORTK_FIRMWARE_VERSION "0.11.5-hang-test"
#endif
#ifndef TOPORTK_FIRMWARE_VERSION
#define TOPORTK_FIRMWARE_VERSION "0.11.32-arch-r06r2"
#endif
constexpr const char *kWebUiVersion = TOPORTK_FIRMWARE_VERSION;
// Called from the main loop. The HTTP task only reads a copied snapshot.
void publish_web_status(const char *json, size_t length, uint32_t now, bool rover);
uint32_t web_boot_id();
