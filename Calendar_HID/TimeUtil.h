#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include "AppState.h"

inline void setupTime(){
  configTime(0, 0, ntpServer);
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

inline bool readLocal(struct tm &out){
  if (getLocalTime(&out)) return true;

  if (M5.In_I2C.isEnabled()){
    m5::rtc_time_t rt; m5::rtc_date_t rd;
    M5.Rtc.getTime(&rt); M5.Rtc.getDate(&rd);
    if (rd.year > 2000){
      memset(&out, 0, sizeof(out));
      out.tm_year = rd.year - 1900;
      out.tm_mon  = rd.month - 1;
      out.tm_mday = rd.date;
      out.tm_hour = rt.hours;
      out.tm_min  = rt.minutes;
      out.tm_sec  = rt.seconds;
      return true;
    }
  }
  return false;
}

inline void syncRTCFromNTP(){
  struct tm ti{};
  if (getLocalTime(&ti)){
    m5::rtc_time_t t; t.hours=ti.tm_hour; t.minutes=ti.tm_min; t.seconds=ti.tm_sec;
    m5::rtc_date_t d; d.date=ti.tm_mday; d.month=ti.tm_mon+1; d.year=ti.tm_year+1900;
    if (M5.In_I2C.isEnabled()){ M5.Rtc.setTime(&t); M5.Rtc.setDate(&d); }
  }
}

#endif // TIMEUTIL_H
