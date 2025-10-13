#ifndef APPSTATE_H
#define APPSTATE_H

#include "Config.h"

// ---------- Models ----------
struct CalendarEvent {
  String title;
  String location;
  int y,m,d;
  int sh, sm;
  int eh, em;
  bool allDay;
};

struct WeatherNow { int t, lo, hi; String cond; };
struct ForecastDay { int y,m,d, hi, lo; String cond; };

struct DayView {
  int y,m,d,wday;
  std::vector<int> idx;
};

// ---------- Globals (macro-controlled single definition) ----------
#ifdef APPSTATE_IMPLEMENTATION
  // Credentials (loaded from SD card)
  String ssid;
  String password;
  String backup_ssid;
  String backup_password;
  String calendarUrl;
  String calendarUrl2;
  String weatherApiKey;
  String LAT = "";
  String LON = "-";

  CalendarEvent events[160];
  int eventCount = 0;

  WeatherNow nowWx;
  ForecastDay fcast[7];

  bool g_marqueeTouchActive = false;
  const unsigned long MARQUEE_STEP_MS = 180;
  const int MARQUEE_SPEED_PX = +4;

  // Marquee state
  struct Marquee {
    int x, y, w, h;
    int offset = 0;
    int speed  = 1;
    int loopW  = 0;
    unsigned long lastMs = 0;
    M5Canvas* sp = nullptr;
  };
  std::vector<Marquee> marquees;

  // Refresh state
  int lastMinute = -1;
  int lastY=-1, lastM=-1, lastD=-1;
  unsigned long lastWxMS = 0;
  const unsigned long WX_PERIOD = 30UL*60UL*1000UL;
#else
  // Externs for other translation units (not used here, but kept clean)
  extern String ssid, password, backup_ssid, backup_password, calendarUrl, calendarUrl2, weatherApiKey, LAT, LON;
  extern CalendarEvent events[160]; extern int eventCount;
  extern WeatherNow nowWx; extern ForecastDay fcast[7];
  extern bool g_marqueeTouchActive; extern const unsigned long MARQUEE_STEP_MS; extern const int MARQUEE_SPEED_PX;
  struct Marquee; extern std::vector<Marquee> marquees;
  extern int lastMinute, lastY, lastM, lastD; extern unsigned long lastWxMS; extern const unsigned long WX_PERIOD;
#endif

// ---------- Shared helpers ----------
inline String two(int v){ char b[8]; sprintf(b,"%02d",v); return String(b); }
inline String time12(int h24, int m){
  if (h24 < 0) return "";
  int h=h24; String ap="AM";
  if (h>=12){ ap="PM"; if(h>12)h-=12; }
  if (h==0) h=12;
  char b[16]; sprintf(b,"%d:%02d %s",h,m,ap.c_str());
  return String(b);
}

#endif // APPSTATE_H
