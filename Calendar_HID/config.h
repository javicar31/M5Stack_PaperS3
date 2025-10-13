#ifndef CONFIG_H
#define CONFIG_H

#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// ---------- PaperS3 SD pins ----------
#define SD_CS   47
#define SD_SCK  39
#define SD_MOSI 38
#define SD_MISO 40

// ---------- Time ----------
static const char* ntpServer = "pool.ntp.org";
static const char* TZ_INFO   = "EST5EDT,M3.2.0/2,M11.1.0/2";

// ---------- UI ----------
static const int SCREEN_W = 960;
static const int SCREEN_H = 540;
static const int HEADER_H = 140;         
static const int FORECAST_H = 56;
static const int TOP_AREA_H = HEADER_H + FORECAST_H + 8;
static const int DAYS_TO_SHOW = 5;

// Colors
static const uint16_t BG         = 0xFFFF;
static const uint16_t TEXT       = 0x0000;
static const uint16_t SUBTLE     = 0xDEFB;
static const uint16_t LINE       = 0xC618;
static const uint16_t DARKLINE   = 0x0000;
static const uint16_t BADGE_FILL = 0xE71C;
static const uint16_t TODAY_BG   = 0xCEDC;

constexpr char DEG = (char)248;

#endif // CONFIG_H
