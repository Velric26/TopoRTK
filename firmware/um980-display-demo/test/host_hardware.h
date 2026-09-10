#pragma once
// Minimal hardware doubles for exercising the actual firmware on a PC.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#define PROGMEM
#include "font/glcdfont.h"
#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define RGB565_GREEN 0x07E0
#define RGB565_RED 0xF800
#define RGB565_YELLOW 0xFFE0
#define RGB565_CYAN 0x07FF
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define SERIAL_8N1 0
#define FILE_APPEND 0
#define FILE_WRITE 1
#define FILE_READ 2
#define WIFI_OFF 0
#define WIFI_AP 1
#define WIFI_STA 2
#define WIFI_AP_STA 3
#define WL_CONNECTED 3

uint32_t host_now = 10000;
uint32_t millis() { return host_now; }
void esp_fill_random(void *buffer,size_t length) {
  static uint8_t seed = 0; auto *bytes = static_cast<uint8_t *>(buffer);
  for (size_t i=0;i<length;++i) bytes[i] = seed++;
}
void delay(uint32_t ms) { host_now += ms; }
uint32_t host_pwm_duty = 0;
uint32_t host_pwm_frequency = 0;
uint32_t ledcSetup(int, int hz, int) { host_pwm_frequency = hz; return hz; }
void ledcAttachPin(int, int) {}
void ledcWrite(int, int duty) { host_pwm_duty = duty == 255 ? 256 : duty; }
uint32_t ledcRead(int) { return host_pwm_duty; }
uint32_t ledcReadFreq(int) { return host_pwm_frequency; }
void pinMode(int, int) {}
void digitalWrite(int, int) {}

class HostPrint {
 public:
  std::string output;
  template<class T> size_t print(const T &value) { std::ostringstream s; s << value; output += s.str(); return s.str().size(); }
  template<class T> void println(const T &value) { print(value); output += '\n'; }
  void println() { output += '\n'; }
  template<class... Args> void printf(const char *format, Args... args) { char b[2048]; std::snprintf(b, sizeof(b), format, args...); output += b; }
};
class HardwareSerial : public HostPrint {
 public:
  explicit HardwareSerial(int = 0) {}
  template<class... Args> void begin(Args...) {}
  void setRxBufferSize(int) {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t *, size_t length) { return length; }
};
HardwareSerial Serial;
int esp_reset_reason(){return 1;}
struct HostESP {unsigned getFreeHeap(){return 100000;}unsigned getMinFreeHeap(){return 90000;}unsigned getFreePsram(){return 8000000;}} ESP;
class Preferences {
 public:
  bool fail = false, exists = false;
  uint32_t value = 0, writes = 0;
  std::string key_value;
  std::vector<uint8_t> blob;
  void end() {}
  size_t putBytes(const char *,const void *p,size_t n){if(fail)return 0;blob.assign(static_cast<const uint8_t *>(p),static_cast<const uint8_t *>(p)+n);return n;}
  size_t getBytes(const char *,void *p,size_t n){if(blob.size()!=n)return 0;std::memcpy(p,blob.data(),n);return n;}
  bool begin(const char *, bool) { return !fail; }
  bool isKey(const char *key) { return std::strcmp(key,"password") == 0 ? !key_value.empty() : exists; }
  size_t putString(const char *,const char *v) { if (fail) return 0; key_value=v; ++writes; return key_value.size(); }
  size_t getString(const char *,char *v,size_t capacity) {
    if (key_value.empty() || key_value.size()+1 > capacity) return 0;
    std::memcpy(v,key_value.c_str(),key_value.size()+1); return key_value.size()+1;
  }
  uint32_t getUInt(const char *, uint32_t fallback) { return exists ? value : fallback; }
  size_t putUInt(const char *, uint32_t v) { if (fail) return 0; value = v; exists = true; ++writes; return 4; }
};
class IPAddress {
 public:
  uint8_t bytes[4];
  IPAddress(uint8_t a=0, uint8_t b=0, uint8_t c=0, uint8_t d=0) : bytes{a,b,c,d} {}
  uint8_t operator[](size_t i) const { return bytes[i]; }
  std::string toString() const { return "192.168.4.2"; }
};
struct HostWiFi {
  int selected_mode = WIFI_OFF;
  bool linked = false;
  bool ap_enabled = false, ap_fail = false;
  IPAddress station_ip{192,168,4,2}, station_mask{255,255,255,0}, ap_ip{192,168,4,1};
  std::string ap_key;
  void persistent(bool) {}
  void setSleep(bool) {}
  bool mode(int m) { selected_mode = m; ap_enabled = (m & WIFI_AP); return true; }
  bool enableAP(bool enabled) { ap_enabled=enabled; selected_mode = enabled ? selected_mode|WIFI_AP : selected_mode&~WIFI_AP; return true; }
  bool softAPConfig(IPAddress ip, IPAddress, IPAddress) { ap_ip=ip; return !ap_fail; }
  bool softAP(const char *, const char *key, int, bool, int) {
    assert(key && std::strlen(key)>=8); ap_key=key; enableAP(true); return !ap_fail;
  }
  void macAddress(uint8_t *bytes) { const uint8_t mac[]={1,2,3,4,5,6}; std::memcpy(bytes,mac,6); }
  void begin(const char *, const char *) {}
  int status() { return linked ? WL_CONNECTED : 0; }
  int RSSI() { return -48; }
  void disconnect() { linked = false; }
  int softAPgetStationNum() { return linked ? 1 : 0; }
  IPAddress softAPIP() { return ap_ip; }
  IPAddress localIP() { return station_ip; }
  IPAddress subnetMask() { return station_mask; }
} WiFi;
class WiFiUDP {
 public:
  bool begin(int) { return true; }
  void stop() {}
  bool beginPacket(IPAddress, int) { return true; }
  size_t write(const uint8_t *, size_t length) { return length; }
  int endPacket() { return 1; }
  int parsePacket() { return 0; }
  int available() { return 0; }
  int read() { return -1; }
  int read(uint8_t *, int length) { return length; }
  IPAddress remoteIP() { return {192,168,4,1}; }
};
struct HostWire {
  int points = 0, x = 0, y = 0, index = 0;
  bool fail = false;
  void begin(int,int,int) {}
  void beginTransmission(int) {}
  void write(int) {}
  int endTransmission(bool = true) { return fail ? 1 : 0; }
  size_t requestFrom(int, int length) { index = 0; return length; }
  int available() { return 0; }
  int read() { const int data[] = {0,0,points,x >> 8,x & 255,y >> 8,y & 255}; return data[index++]; }
} Wire;
class TCA9554 {
 public:
  explicit TCA9554(int) {}
  bool begin() { return true; }
  void pinMode1(int,int) {}
  void write1(int,int) {}
};
class File : public HostPrint {
 public:
  explicit operator bool() const { return false; }
  void flush() {}
  void close() {}
  size_t readBytes(char *, size_t) { return 0; }
};
struct HostSD {
  bool exists(const char *) {return false;}
  void setPins(int,int,int) {}
  bool begin(const char *,bool,bool) { return false; }
  uint64_t cardSize() { return 0; }
  uint64_t totalBytes() { return 0; }
  uint64_t usedBytes() { return 0; }
  bool mkdir(const char *) { return true; }
  File open(const char *,int) { return File{}; }
} SD_MMC;

class Arduino_DataBus {};
class Arduino_ESP32SPI : public Arduino_DataBus {
 public:
  template<class... Args> explicit Arduino_ESP32SPI(Args...) {}
};
class Arduino_GFX {
 public:
  std::array<uint16_t, 320*480> pixels{};
  int cursor_x=0, cursor_y=0, text_size=1;
  uint16_t text_color=0xFFFF;
  unsigned draws=0;
  bool begin() { return true; }
  void setTextWrap(bool) {}
  void setTextSize(int size) { text_size=size; }
  void setTextColor(uint16_t color) { text_color=color; }
  void setCursor(int x,int y) { cursor_x=x; cursor_y=y; }
  void fillRect(int x,int y,int w,int h,uint16_t color) {
    assert(x >= 0 && y >= 0 && x+w <= 320 && y+h <= 480);
    ++draws;
    for (int row=y; row<y+h; ++row) for (int col=x; col<x+w; ++col) pixels[row*320+col]=color;
  }
  void fillScreen(uint16_t color) { fillRect(0,0,320,480,color); }
  void drawFastHLine(int x,int y,int w,uint16_t color) { fillRect(x,y,w,1,color); }
  void fillRoundRect(int x,int y,int w,int h,int radius,uint16_t color) {
    for (int row=0; row<h; ++row) {
      int dy = row < radius ? radius-row-1 : row >= h-radius ? row-(h-radius) : 0;
      int inset = dy ? radius-static_cast<int>(std::sqrt(radius*radius-dy*dy)) : 0;
      fillRect(x+inset,y+row,w-2*inset,1,color);
    }
  }
  void drawRoundRect(int x,int y,int w,int h,int radius,uint16_t color) {
    for (int row=0; row<h; ++row) {
      int dy = row < radius ? radius-row-1 : row >= h-radius ? row-(h-radius) : 0;
      int inset = dy ? radius-static_cast<int>(std::sqrt(radius*radius-dy*dy)) : 0;
      if (row==0 || row==h-1) fillRect(x+inset,y+row,w-2*inset,1,color);
      else { fillRect(x+inset,y+row,1,1,color); fillRect(x+w-inset-1,y+row,1,1,color); }
    }
  }
  void print(const char *text) {
    while (*text) {
      const unsigned char character = *text++;
      assert(cursor_x >= 0 && cursor_y >= 0 && cursor_x+6*text_size<=320 && cursor_y+8*text_size<=480);
      for (int col=0; col<5; ++col) for (int row=0; row<8; ++row)
        if (font[character*5+col] & (1 << row)) fillRect(cursor_x+col*text_size,cursor_y+row*text_size,text_size,text_size,text_color);
      cursor_x += 6*text_size;
    }
  }
  void print(char character) { char text[] = {character,0}; print(text); }
  void save(const char *path) {
    std::ofstream file(path,std::ios::binary); file << "P6\n320 480\n255\n";
    for (uint16_t p:pixels) { unsigned char rgb[] = {static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)}; file.write(reinterpret_cast<char *>(rgb),3); }
  }
};
class Arduino_ST7796 : public Arduino_GFX {
 public:
  template<class... Args> explicit Arduino_ST7796(Args...) {}
};
