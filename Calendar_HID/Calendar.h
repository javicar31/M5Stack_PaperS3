#ifndef CALENDAR_H
#define CALENDAR_H

#include "TimeUtil.h"
#include "Marquee.h"  // for types, not required but safe
#include "AppState.h"

inline void parseICSDateTime(const String& line, bool isEnd, CalendarEvent& ev){
  int p = line.indexOf(':');
  if (p < 0) return;
  String head = line.substring(0, p);
  String dt   = line.substring(p + 1);
  dt.trim();
  dt.replace("\r", "");

  if (head.indexOf("VALUE=DATE") != -1) {
    if (!isEnd) {
      ev.allDay = true;
      if (dt.length() >= 8) {
        ev.y  = dt.substring(0, 4).toInt();
        ev.m  = dt.substring(4, 6).toInt();
        ev.d  = dt.substring(6, 8).toInt();
        ev.sh = -1; ev.sm = -1;
        ev.eh = -1; ev.em = -1;
      }
    }
    return;
  }

  int tPos = dt.indexOf('T');
  if (tPos < 0 || dt.length() < 15) return;

  bool hasZ    = dt.endsWith("Z");
  bool hasTZID = (head.indexOf("TZID=") != -1);

  int y = dt.substring(0, 4).toInt();
  int m = dt.substring(4, 6).toInt();
  int d = dt.substring(6, 8).toInt();

  String timePart = dt.substring(tPos + 1);
  if (hasZ && timePart.length() > 0) timePart.remove(timePart.length() - 1);
  timePart.trim();

  int hh = 0, mm = 0;
  if (timePart.length() >= 4) {
    hh = timePart.substring(0, 2).toInt();
    mm = timePart.substring(2, 4).toInt();
  } else return;

  if (!hasZ && hh == 0 && mm == 0) {
    if (!isEnd) {
      ev.allDay = true;
      ev.y  = y; ev.m = m; ev.d = d;
      ev.sh = -1; ev.sm = -1;
      ev.eh = -1; ev.em = -1;
    }
    return;
  }

  if (!hasZ) {
    if (!isEnd) { ev.y = y; ev.m = m; ev.d = d; ev.sh = hh; ev.sm = mm; }
    else        { ev.eh = hh; ev.em = mm; }
    return;
  }

  struct tm tmutc{};
  tmutc.tm_year = y - 1900;
  tmutc.tm_mon  = m - 1;
  tmutc.tm_mday = d;
  tmutc.tm_hour = hh;
  tmutc.tm_min  = mm;
  tmutc.tm_sec  = 0;

  char* prevTZ = getenv("TZ");
  String prev = prevTZ ? String(prevTZ) : String();

  setenv("TZ", "UTC", 1); tzset();
  time_t utc_ts = mktime(&tmutc);

  setenv("TZ", TZ_INFO, 1); tzset();
  struct tm lt = *localtime(&utc_ts);

  if (!isEnd) {
    ev.y  = lt.tm_year + 1900;
    ev.m  = lt.tm_mon + 1;
    ev.d  = lt.tm_mday;
    ev.sh = lt.tm_hour;
    ev.sm = lt.tm_min;
  } else {
    ev.eh = lt.tm_hour;
    ev.em = lt.tm_min;
  }

  if (prev.length() > 0) { setenv("TZ", prev.c_str(), 1); tzset(); }
  else                   { setenv("TZ", TZ_INFO, 1);     tzset(); }
}

inline bool parseAndAddEvents(const String& data, int maxEvents) {
  int pos = 0;
  bool success = false;
  while (pos < data.length() && eventCount < maxEvents) {
    int s=data.indexOf("BEGIN:VEVENT",pos); if(s<0)break;
    int e=data.indexOf("END:VEVENT",s); if(e<0)break;
    String body=data.substring(s,e);

    CalendarEvent ev;
    ev.title=""; ev.location=""; ev.y=ev.m=ev.d=0;
    ev.sh=ev.sm=-1; ev.eh=ev.em=-1; ev.allDay=false;

    int p=body.indexOf("SUMMARY:");
    if(p!=-1){
      int q=body.indexOf("\n",p);
      ev.title=body.substring(p+8,q);
      ev.title.trim();
      ev.title.replace("\r","");
    }

    p=body.indexOf("LOCATION:");
    if(p!=-1){
      int q=body.indexOf("\n",p);
      ev.location=body.substring(p+9,q);
      ev.location.trim();
      ev.location.replace("\r","");
    }

    p=body.indexOf("DTSTART");
    if(p!=-1){
      int q=body.indexOf("\n",p);
      parseICSDateTime(body.substring(p,q),false,ev);
    }

    p=body.indexOf("DTEND");
    if(p!=-1){
      int q=body.indexOf("\n",p);
      parseICSDateTime(body.substring(p,q),true,ev);
    }

    events[eventCount++]=ev;
    pos=e+10;
    success = true;
  }
  return success;
}

inline void fetchAndParse(const char* url, const char* filename){
  if (WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http;
  http.begin(url);
  http.addHeader("User-Agent","PaperS3-Calendar/1.4");
  int code=http.GET();
  if (code==HTTP_CODE_OK){
    String payload=http.getString();
    File f=SD.open(filename,FILE_WRITE);
    if(f){ f.print(payload); f.close(); }
    parseAndAddEvents(payload, 160);
  } else {
    File f=SD.open(filename,FILE_READ);
    if(f){
      String payload=f.readString();
      f.close();
      parseAndAddEvents(payload, 160);
    }
  }
  http.end();
}

inline void fetchCalendar(){
  eventCount = 0;

  if (calendarUrl.length() > 0) fetchAndParse(calendarUrl.c_str(), "/calendar1.ics");
  if (calendarUrl2.length() > 0) fetchAndParse(calendarUrl2.c_str(), "/calendar2.ics");

  for (int i=0;i<eventCount-1;i++)
    for (int j=0;j<eventCount-i-1;j++){
      auto&a=events[j]; auto&b=events[j+1];
      if (a.y!=b.y? a.y>b.y : (a.m!=b.m? a.m>b.m : (a.d!=b.d? a.d>b.d : (a.sh!=b.sh? a.sh>b.sh : a.sm>b.sm)))){
        auto t=events[j]; events[j]=events[j+1]; events[j+1]=t;
      }
    }
}

#endif // CALENDAR_H
