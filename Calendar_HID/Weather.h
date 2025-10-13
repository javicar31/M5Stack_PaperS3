#ifndef WEATHER_H
#define WEATHER_H

#include "AppState.h"
#include "TimeUtil.h"

inline uint32_t ymd_key(time_t ts) {
  struct tm lt = *localtime(&ts);
  return (uint32_t)( (lt.tm_year+1900)*10000 + (lt.tm_mon+1)*100 + lt.tm_mday );
}

inline bool fetchWeather(){
  if (weatherApiKey.length() == 0) return false;

  // Current conditions
  {
    HTTPClient http;
    String u = "https://api.openweathermap.org/data/2.5/weather?lat=" + LAT
             + "&lon=" + LON + "&units=imperial&appid=" + weatherApiKey;
    http.begin(u);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    DynamicJsonDocument d1(8*1024);
    if (deserializeJson(d1, http.getString())) { http.end(); return false; }
    http.end();

    nowWx.t    = int(d1["main"]["temp"].as<float>() + 0.5f);
    nowWx.cond = d1["weather"][0]["main"].as<String>();
  }

  // Forecast
  {
    HTTPClient http;
    String u = "https://api.openweathermap.org/data/2.5/forecast?lat=" + LAT
             + "&lon=" + LON + "&units=imperial&appid=" + weatherApiKey;
    http.begin(u);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return false; }
    DynamicJsonDocument d2(64*1024);
    if (deserializeJson(d2, http.getString())) { http.end(); return false; }
    http.end();

    for (int i=0;i<7;i++){
      fcast[i].y=fcast[i].m=fcast[i].d=0;
      fcast[i].hi=-999;
      fcast[i].lo=999;
      fcast[i].cond="";
    }

    uint32_t keys[7];
    int slotCount = 0;

    auto findOrAddSlot = [&](uint32_t key, time_t ts)->int {
      for (int i=0;i<slotCount;i++) if (keys[i]==key) return i;
      if (slotCount >= 7) return -1;
      int s = slotCount++;
      keys[s] = key;
      struct tm lt = *localtime(&ts);
      fcast[s].y = lt.tm_year + 1900;
      fcast[s].m = lt.tm_mon + 1;
      fcast[s].d = lt.tm_mday;
      fcast[s].hi = -999;
      fcast[s].lo = 999;
      fcast[s].cond = "";
      return s;
    };

    JsonArray arr = d2["list"].as<JsonArray>();
    for (JsonObject item : arr){
      time_t ts = item["dt"].as<long>();
      uint32_t key = ymd_key(ts);
      int s = findOrAddSlot(key, ts);
      if (s < 0) continue;

      float tmin = item["main"]["temp_min"].as<float>();
      float tmax = item["main"]["temp_max"].as<float>();
      if (tmax > fcast[s].hi) fcast[s].hi = int(tmax + 0.5f);
      if (tmin < fcast[s].lo) fcast[s].lo = int(tmin + 0.5f);

      int hour = localtime(&ts)->tm_hour;
      String cond = item["weather"][0]["main"].as<String>();
      if ((hour >= 11 && hour <= 14) || fcast[s].cond == "") fcast[s].cond = cond;
    }

    struct tm nowLT{};
    getLocalTime(&nowLT);
    uint32_t todayKey = (nowLT.tm_year+1900)*10000 + (nowLT.tm_mon+1)*100 + (nowLT.tm_mday);

    int todaySlot = -1;
    for (int i=0;i<slotCount;i++) if (keys[i] == todayKey){ todaySlot = i; break; }

    if (todaySlot >= 0) {
      nowWx.hi = (fcast[todaySlot].hi == -999) ? nowWx.t : fcast[todaySlot].hi;
      nowWx.lo = (fcast[todaySlot].lo ==  999) ? nowWx.t : fcast[todaySlot].lo;
    } else if (slotCount > 0) {
      nowWx.hi = (fcast[0].hi == -999) ? nowWx.t : fcast[0].hi;
      nowWx.lo = (fcast[0].lo ==  999) ? nowWx.t : fcast[0].lo;
    } else {
      nowWx.hi = nowWx.t;
      nowWx.lo = nowWx.t;
    }
  }

  return true;
}

#endif // WEATHER_H
