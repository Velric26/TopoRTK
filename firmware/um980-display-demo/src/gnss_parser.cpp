// UM980 ASCII sentence parsing moved verbatim from main.cpp (R1). Parsing
// policy, fixed local buffers and accepted/rejected inputs are unchanged;
// millis() became the now_ms parameter and the global checksum counter became
// the caller-owned GnssParseStats.
#include "gnss_parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

bool valid_nmea_checksum(const char *line) {
  if (line == nullptr || line[0] != '$') return false;
  const char *star = std::strchr(line, '*');
  if (star == nullptr || star[1] == '\0' || star[2] == '\0') return false;

  uint8_t calculated = 0;
  for (const char *cursor = line + 1; cursor < star; ++cursor) {
    calculated ^= static_cast<uint8_t>(*cursor);
  }

  const int high = hex_value(star[1]);
  const int low = hex_value(star[2]);
  return high >= 0 && low >= 0 && calculated == ((high << 4) | low);
}

double nmea_coordinate(const char *value, char hemisphere) {
  if (value == nullptr || value[0] == '\0') return 0.0;
  const double raw = std::strtod(value, nullptr);
  const double degrees = std::floor(raw / 100.0);
  double decimal = degrees + (raw - degrees * 100.0) / 60.0;
  if (hemisphere == 'S' || hemisphere == 'W') decimal = -decimal;
  return decimal;
}

}  // namespace

uint32_t ascii_crc32(const char *begin, const char *end) {
  uint32_t crc = 0;
  for (const char *cursor = begin; cursor < end; ++cursor) {
    crc ^= static_cast<uint8_t>(*cursor);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320UL : 0);
    }
  }
  return crc;
}

bool valid_unicore_ascii_crc(const char *line) {
  if (line == nullptr || line[0] != '#') return false;
  const char *star = std::strrchr(line, '*');
  if (star == nullptr || std::strlen(star + 1) != 8) return false;

  char *end = nullptr;
  const unsigned long expected = std::strtoul(star + 1, &end, 16);
  if (end == nullptr || *end != '\0') return false;
  return static_cast<uint32_t>(expected) == ascii_crc32(line + 1, star);
}

bool parse_gga(const char *line, GgaData &result, GnssParseStats &stats) {
  if (line == nullptr ||
      (std::strncmp(line, "$GNGGA,", 7) != 0 &&
       std::strncmp(line, "$GPGGA,", 7) != 0)) {
    return false;
  }
  if (!valid_nmea_checksum(line)) {
    ++stats.checksum_errors;
    return false;
  }

  char copy[256] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strchr(copy, '*');
  if (star != nullptr) *star = '\0';

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = copy;
  for (char *cursor = copy; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 11) return false;

  result.received = true;
  result.quality = std::atoi(fields[6]);
  result.satellites = std::atoi(fields[7]);
  result.hdop = std::strtod(fields[8], nullptr);
  result.altitude = std::strtod(fields[9], nullptr);
  result.latitude = nmea_coordinate(fields[2], fields[3][0]);
  result.longitude = nmea_coordinate(fields[4], fields[5][0]);

  if (fields[1][0] == '\0') {
    std::strcpy(result.utc, "---");
  } else if (std::strlen(fields[1]) >= 6) {
    std::snprintf(result.utc, sizeof(result.utc), "%.2s:%.2s:%.2s",
                  fields[1], fields[1] + 2, fields[1] + 4);
  } else {
    std::strncpy(result.utc, fields[1], sizeof(result.utc) - 1);
  }
  return true;
}

bool parse_rmc_time(const char *line, uint32_t now_ms,
                    GnssTimeData &result, GnssParseStats &stats) {
  if (line == nullptr ||
      (std::strncmp(line, "$GNRMC,", 7) != 0 &&
       std::strncmp(line, "$GPRMC,", 7) != 0)) {
    return false;
  }
  if (!valid_nmea_checksum(line)) {
    ++stats.checksum_errors;
    return false;
  }

  char copy[256] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strchr(copy, '*');
  if (star != nullptr) *star = '\0';

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = copy;
  for (char *cursor = copy; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 10 || std::strlen(fields[1]) < 6 ||
      std::strlen(fields[9]) != 6) {
    return false;
  }
  for (uint8_t index = 0; index < 6; ++index) {
    if (fields[1][index] < '0' || fields[1][index] > '9' ||
        fields[9][index] < '0' || fields[9][index] > '9') {
      return false;
    }
  }

  const int hour = (fields[1][0] - '0') * 10 + fields[1][1] - '0';
  const int minute = (fields[1][2] - '0') * 10 + fields[1][3] - '0';
  const int second = (fields[1][4] - '0') * 10 + fields[1][5] - '0';
  const int day = (fields[9][0] - '0') * 10 + fields[9][1] - '0';
  const int month = (fields[9][2] - '0') * 10 + fields[9][3] - '0';
  const int year = 2000 + (fields[9][4] - '0') * 10 + fields[9][5] - '0';
  const bool digits_valid =
      hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
      second >= 0 && second <= 60 && day >= 1 && day <= 31 &&
      month >= 1 && month <= 12 && year >= 2020 && year <= 2099;

  result.received = true;
  // RMC status V means the navigation solution is invalid; the receiver can
  // still provide a valid GNSS-disciplined clock and date while indoors.
  result.valid = digits_valid;
  result.navigation_valid = fields[2][0] == 'A';
  result.hour = static_cast<uint8_t>(hour);
  result.minute = static_cast<uint8_t>(minute);
  result.second = static_cast<uint8_t>(second);
  result.day = static_cast<uint8_t>(day);
  result.month = static_cast<uint8_t>(month);
  result.year = static_cast<uint16_t>(year);
  result.received_ms = now_ms;
  return true;
}

bool parse_bestnav_accuracy(const char *line, uint32_t now_ms,
                            HorizontalAccuracyData &result, GnssParseStats &stats) {
  if (line == nullptr || std::strncmp(line, "#BESTNAVA,", 10) != 0) {
    return false;
  }
  if (!valid_unicore_ascii_crc(line)) {
    ++stats.checksum_errors;
    return false;
  }

  char copy[512] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strrchr(copy, '*');
  if (star != nullptr) *star = '\0';
  char *body = std::strchr(copy, ';');
  if (body == nullptr) return false;
  *body='\0';
  ++body;

  char *header[10]={copy};size_t header_count=1;
  for(char *cursor=copy;*cursor && header_count<10;++cursor)if(*cursor==','){*cursor='\0';header[header_count++]=cursor+1;}

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = body;
  for (char *cursor = body; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 10) return false;

  const double latitude_sigma = std::strtod(fields[7], nullptr);
  const double longitude_sigma = std::strtod(fields[8], nullptr);
  if (!std::isfinite(latitude_sigma) || !std::isfinite(longitude_sigma) ||
      latitude_sigma < 0.0 || longitude_sigma < 0.0 ||
      latitude_sigma > 10000.0 || longitude_sigma > 10000.0) {
    return false;
  }

  result.received = true;
  result.latitude_sigma_m = latitude_sigma;
  result.longitude_sigma_m = longitude_sigma;
  result.horizontal_1drms_m =
      std::sqrt(latitude_sigma * latitude_sigma +
                longitude_sigma * longitude_sigma);
  result.received_ms = now_ms;
  auto numeric=[](const char *field,double &value){char *end=nullptr;if(!field||!field[0])return false;
    value=std::strtod(field,&end);return end && *end=='\0' && std::isfinite(value);};
  double latitude=0,longitude=0,msl=0,undulation=0,vertical=0,week=0,tow=0,sigma_check=0,diff_age=0,solution_age=0,station_id=0;
  if(field_count>10 && fields[10][0]=='"'){++fields[10];char *quote=std::strchr(fields[10],'"');if(quote)*quote='\0';}
  result.position_valid=std::strcmp(fields[0],"SOL_COMPUTED")==0 && std::strcmp(fields[6],"WGS84")==0 &&
    numeric(fields[2],latitude)&&numeric(fields[3],longitude)&&numeric(fields[4],msl)&&numeric(fields[5],undulation)&&numeric(fields[9],vertical)&&
    numeric(fields[7],sigma_check)&&numeric(fields[8],sigma_check)&&latitude>=-80&&latitude<=84&&std::abs(longitude)<=180&&msl>=-1200&&msl<=15000&&std::abs(undulation)<=150&&vertical>=0&&vertical<=10000&&
    header_count>5&&std::strcmp(header[3],"FINE")==0&&numeric(header[4],week)&&numeric(header[5],tow)&&week>=2000&&week<10000&&tow>=0&&tow<604800000&&
    field_count>12&&numeric(fields[10],station_id)&&station_id>=0&&station_id<=4095&&std::floor(station_id)==station_id&&
    numeric(fields[11],diff_age)&&diff_age>0&&diff_age<=3600&&numeric(fields[12],solution_age)&&solution_age>=0&&solution_age<=1.5;
  result.position={latitude,longitude,msl+undulation};result.vertical_sigma_m=vertical;
  result.differential_age_ms=result.position_valid?uint32_t(std::ceil(diff_age*1000)):UINT32_MAX;result.solution_station=uint16_t(station_id);
  result.epoch=result.position_valid?uint64_t(week)*604800000ULL+uint64_t(tow):0;
  result.rtk_fixed=std::strcmp(fields[1],"NARROW_INT")==0 || std::strcmp(fields[1],"L1_INT")==0 || std::strcmp(fields[1],"WIDE_INT")==0;
  return true;
}
