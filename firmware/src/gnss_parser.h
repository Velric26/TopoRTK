#pragma once
// Pure UM980 ASCII sentence parsing: fixed local buffers, no Arduino, display,
// storage or logging dependency. Callers supply the sample time and own the
// error statistics so the receiver owner keeps overflow accounting.
#include <cstdint>
#include "survey_math.h"

struct GgaData {
  bool received = false;
  int quality = 0;
  int satellites = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  double hdop = 0.0;
  double altitude = 0.0;
  char utc[16] = "---";
};

struct HorizontalAccuracyData {
  bool received = false;
  double latitude_sigma_m = 0.0;
  double longitude_sigma_m = 0.0;
  double horizontal_1drms_m = 0.0;
  uint32_t received_ms = 0;
  double vertical_sigma_m = 0;
  uint32_t differential_age_ms=UINT32_MAX;
  uint16_t solution_station=65535;
  survey::Position position;
  uint64_t epoch = 0;
  bool position_valid = false;
  bool rtk_fixed = false;
};

struct GnssTimeData {
  bool received = false;
  bool valid = false;
  bool navigation_valid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t day = 0;
  uint8_t month = 0;
  uint16_t year = 0;
  uint32_t received_ms = 0;
};

struct GnssParseStats {
  uint32_t checksum_errors = 0;
};

// Checksum helpers shared with receiver reply validation and test fixtures.
uint32_t ascii_crc32(const char *begin, const char *end);
bool valid_unicore_ascii_crc(const char *line);

bool parse_gga(const char *line, GgaData &result, GnssParseStats &stats);
bool parse_rmc_time(const char *line, uint32_t now_ms,
                    GnssTimeData &result, GnssParseStats &stats);
bool parse_bestnav_accuracy(const char *line, uint32_t now_ms,
                            HorizontalAccuracyData &result, GnssParseStats &stats);
