#pragma once

#include <cstdint>
#include <cstring>

// UM980 short command/MODE replies include the leading $/# in their XOR.
// NMEA excludes $, and BESTNAVA uses CRC-32; keep these paths separate.
inline bool valid_receiver_reply(const char *line) {
  if (!line || (line[0] != '$' && line[0] != '#')) return false;
  const char *star = std::strchr(line, '*');
  if (!star || std::strlen(star + 1) != 2) return false;
  uint8_t checksum = 0;
  for (const char *cursor = line; cursor < star; ++cursor) checksum ^= *cursor;
  static const char digits[] = "0123456789ABCDEF";
  const char high = star[1] >= 'a' && star[1] <= 'f' ? star[1] - 'a' + 'A' : star[1];
  const char low = star[2] >= 'a' && star[2] <= 'f' ? star[2] - 'a' + 'A' : star[2];
  return high == digits[checksum >> 4] && low == digits[checksum & 15];
}
