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

// ---------- Credentials (loaded from SD card) ----------
String ssid;
String password;
String backup_ssid;
String backup_password;
String calendarUrl;
String calendarUrl2;
String weatherApiKey;
String LAT = "";           
String LON = "-";
constexpr char DEG = (char)248;

// ---------- Time ----------
const char* ntpServer = "pool.ntp.org";
const char* TZ_INFO   = "EST5EDT,M3.2.0/2,M11.1.0/2";

// ---------- UI ----------
const int SCREEN_W = 960;
const int SCREEN_H = 540;
const int HEADER_H = 110;
const int FORECAST_H = 56;
const int TOP_AREA_H = HEADER_H + FORECAST_H + 8;
const int DAYS_TO_SHOW = 5;

// Colors 
const uint16_t BG         = 0xFFFF;
const uint16_t TEXT       = 0x0000;
const uint16_t SUBTLE     = 0xDEFB;
const uint16_t LINE       = 0xC618;
const uint16_t DARKLINE   = 0x0000;
const uint16_t BADGE_FILL = 0xE71C;
const uint16_t TODAY_BG   = 0xCEDC;

// ---------- Models ----------
struct CalendarEvent {
  String title;
  String location;
  int y,m,d;
  int sh, sm;
  int eh, em;
  bool allDay;
};

CalendarEvent events[160];
int eventCount = 0;

struct WeatherNow { int t, lo, hi; String cond; } nowWx;
struct ForecastDay { int y,m,d, hi, lo; String cond; } fcast[7];

struct DayView {
  int y,m,d,wday;
  std::vector<int> idx;
};

bool g_marqueeTouchActive = false;
const unsigned long MARQUEE_STEP_MS = 180;
const int MARQUEE_SPEED_PX = +4;

// ---------- Marquee ----------
struct Marquee {
  int x, y, w, h;
  int offset = 0;
  int speed  = 1;
  int loopW  = 0;
  unsigned long lastMs = 0;
  M5Canvas* sp = nullptr;
};
std::vector<Marquee> marquees;

void clearMarquees() {
  for (auto &m : marquees) {
    if (m.sp) { m.sp->deleteSprite(); delete m.sp; m.sp = nullptr; }
  }
  marquees.clear();
}

void addMarquee(int x,int y,int w,int h,const String& text,int textSize) {
  M5Canvas* sp = new M5Canvas(&M5.Display);
  sp->setColorDepth(8);
  sp->createSprite(w + 64, h);
  sp->setTextColor(TEXT, BG);
  sp->setTextWrap(false);
  sp->setTextSize(textSize);

  int textW = sp->textWidth(text.c_str());
  int lh    = sp->fontHeight();
  int gap   = 32;
  int loopW = textW + gap + w;

  if (textW <= w) {
    sp->deleteSprite();
    delete sp;
    M5.Display.setTextSize(textSize);
    M5.Display.setTextColor(TEXT, BG);
    M5.Display.setCursor(x, y);
    M5.Display.setClipRect(x, y, w, lh + 2);
    M5.Display.print(text);
    M5.Display.clearClipRect();
    return;
  }

  sp->deleteSprite();
  sp->createSprite(loopW, lh + 2);
  sp->fillSprite(BG);
  sp->setTextColor(TEXT, BG);
  sp->setTextSize(textSize);
  sp->setCursor(0, 0);                 sp->print(text);
  sp->setCursor(textW + gap, 0);       sp->print(text);

  Marquee m;
  m.x = x; m.y = y; m.w = w; m.h = lh + 2;
  m.offset = 0;
  m.loopW  = loopW;
  m.sp     = sp;
  m.lastMs = millis();
  m.speed  = MARQUEE_SPEED_PX;

  M5.Display.setClipRect(m.x, m.y, m.w, m.h);
  m.sp->pushSprite(&M5.Display, m.x - m.offset, m.y);
  M5.Display.clearClipRect();

  marquees.push_back(m);
}

void updateMarquees() {
  if (!g_marqueeTouchActive || marquees.empty()) return;

  static bool epdBoosted = false;
  if (!epdBoosted && M5.Display.isEPD()) {
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    epdBoosted = true;
  }

  const unsigned long now = millis();
  for (auto &m : marquees) {
    if (!m.sp) continue;
    if (now - m.lastMs < MARQUEE_STEP_MS) continue;
    m.lastMs = now;

    m.offset += m.speed;
    if (m.offset >= m.loopW) m.offset -= m.loopW;
    if (m.offset < 0)        m.offset += m.loopW;

    M5.Display.setClipRect(m.x, m.y, m.w, m.h);
    M5.Display.fillRect(m.x, m.y, m.w, m.h, BG);
    m.sp->pushSprite(&M5.Display, m.x - m.offset, m.y);
    M5.Display.clearClipRect();
  }
}

// ---------- Refresh state ----------
int lastMinute = -1;
int lastY=-1, lastM=-1, lastD=-1;
unsigned long lastWxMS = 0;
const unsigned long WX_PERIOD = 30UL*60UL*1000UL;

// ---------- Utils ----------
String two(int v){ char b[8]; sprintf(b,"%02d",v); return String(b); }

String time12(int h24, int m){
  if (h24 < 0) return "";
  int h=h24; String ap="AM";
  if (h>=12){ ap="PM"; if(h>12)h-=12; }
  if (h==0) h=12;
  char b[16]; sprintf(b,"%d:%02d %s",h,m,ap.c_str());
  return String(b);
}

void badge(int x,int y,const String& s){
  M5.Display.setTextSize(2);
  int tw=M5.Display.textWidth(s.c_str()), th=M5.Display.fontHeight();
  int padX=8,padY=4;
  M5.Display.fillRoundRect(x,y,tw+2*padX,th+2*padY,8,BADGE_FILL);
  M5.Display.drawRoundRect(x,y,tw+2*padX,th+2*padY,8,DARKLINE);
  M5.Display.setCursor(x+padX,y+padY+1);
  M5.Display.setTextColor(TEXT);
  M5.Display.print(s);
}

int wrapInsideBox(int x,int y,int w,int bottom,const String& text,int sz){
  M5.Display.setTextSize(sz);
  int lineH = M5.Display.fontHeight();
  String word, line; int cy=y;
  auto flushLine=[&](){
    if (cy + lineH > bottom) return false;
    M5.Display.setCursor(x, cy); M5.Display.print(line);
    cy += lineH + 2; line = ""; return true;
  };
  for (int i=0;i<=text.length();++i){
    char c=(i<text.length())?text[i]:' ';
    if (c==' '||c=='\n'||i==text.length()){
      String prospect = line.length()? (line+" "+word):word;
      if (M5.Display.textWidth(prospect.c_str()) <= w) line = prospect;
      else { if (!flushLine()) return bottom; line = word; }
      word = "";
      if (c=='\n'){ if (!flushLine()) return bottom; }
    } else word += c;
  }
  if (line.length()){
    if (M5.Display.textWidth(line.c_str()) > w){
      while (line.length() && M5.Display.textWidth((line+"…").c_str()) > w) line.remove(line.length()-1);
      line += "…";
    }
    if (y + lineH <= bottom){ M5.Display.setCursor(x, cy); cy += lineH + 2; }
  }
  return cy;
}

// ---------- Time ----------
void setupTime(){
  configTime(0, 0, ntpServer);
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

bool readLocal(struct tm &out){
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

void syncRTCFromNTP(){
  struct tm ti{};
  if (getLocalTime(&ti)){
    m5::rtc_time_t t; t.hours=ti.tm_hour; t.minutes=ti.tm_min; t.seconds=ti.tm_sec;
    m5::rtc_date_t d; d.date=ti.tm_mday; d.month=ti.tm_mon+1; d.year=ti.tm_year+1900;
    if (M5.In_I2C.isEnabled()){ M5.Rtc.setTime(&t); M5.Rtc.setDate(&d); }
  }
}

// ---------- Calendar parsing ----------
void parseICSDateTime(const String& line, bool isEnd, CalendarEvent& ev){
  int p = line.indexOf(':'); 
  if (p < 0) return;

  String head = line.substring(0, p);
  String dt   = line.substring(p + 1);
  dt.trim(); 
  dt.replace("\r", "");

  if (head.indexOf("VALUE=DATE") != -1) {
    ev.allDay = true;
    if (dt.length() >= 8) {
      ev.y = dt.substring(0, 4).toInt();
      ev.m = dt.substring(4, 6).toInt();
      ev.d = dt.substring(6, 8).toInt();
      if (!isEnd) { ev.sh = 9; ev.sm = 0; }
    }
    return;
  }

  bool hasZ = dt.endsWith("Z");
  bool hasTZID = (head.indexOf("TZID=") != -1);

  int tPos = dt.indexOf('T');
  if (tPos < 0 || dt.length() < 15) return;

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
  else { setenv("TZ", TZ_INFO, 1); tzset(); }
}

bool parseAndAddEvents(const String& data, int maxEvents) {
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

void fetchAndParse(const char* url, const char* filename){
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

void fetchCalendar(){
  eventCount = 0;
  
  if (calendarUrl.length() > 0) {
    fetchAndParse(calendarUrl.c_str(), "/calendar1.ics");
  }

  if (calendarUrl2.length() > 0) {
    fetchAndParse(calendarUrl2.c_str(), "/calendar2.ics");
  }

  // Sort events
  for (int i=0;i<eventCount-1;i++)
    for (int j=0;j<eventCount-i-1;j++){
      auto&a=events[j]; auto&b=events[j+1];
      if (a.y!=b.y? a.y>b.y : (a.m!=b.m? a.m>b.m : (a.d!=b.d? a.d>b.d : (a.sh!=b.sh? a.sh>b.sh : a.sm>b.sm)))){
        auto t=events[j]; events[j]=events[j+1]; events[j+1]=t;
      }
    }
}

static uint32_t ymd_key(time_t ts) {
  struct tm lt = *localtime(&ts);
  return (uint32_t)( (lt.tm_year+1900)*10000 + (lt.tm_mon+1)*100 + lt.tm_mday );
}

bool fetchWeather(){
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
    uint32_t todayKey = (nowLT.tm_year+1900)*10000 + (nowLT.tm_mon+1)*100 + nowLT.tm_mday;

    int todaySlot = -1;
    for (int i=0;i<slotCount;i++){
      if (keys[i] == todayKey){ todaySlot = i; break; }
    }

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

// ---------- Drawing ----------
void drawHeader(const tm& t){
  M5.Display.fillRect(0, 0, SCREEN_W, HEADER_H, SUBTLE);
  M5.Display.drawLine(0, HEADER_H, SCREEN_W, HEADER_H, DARKLINE);

  static const char* MONTHS[] = {
    "JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"
  };
  M5.Display.setTextColor(TEXT);

  M5.Display.setTextSize(3);
  M5.Display.setCursor(20, 18);
  M5.Display.printf("%s %d", MONTHS[t.tm_mon], t.tm_year + 1900);

  M5.Display.setTextSize(5);
  M5.Display.setCursor(300, 14);
  char buf[16];
  sprintf(buf, "%d:%02d %s",
          (t.tm_hour % 12) ? (t.tm_hour % 12) : 12,
          t.tm_min,
          (t.tm_hour >= 12) ? "PM" : "AM");
  M5.Display.print(buf);

  int x = SCREEN_W - 260;

  M5.Display.setTextSize(4);
  M5.Display.setCursor(x, 10);
  M5.Display.printf("%d%cF", nowWx.t, DEG);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(x, 58);
  M5.Display.printf("%s  H:%d%c  L:%d%c",
                    nowWx.cond.c_str(), nowWx.hi, DEG, nowWx.lo, DEG);

  bool charging = M5.Power.isCharging();
  int pct = M5.Power.getBatteryLevel();
  M5.Display.setCursor(x, 86);
  M5.Display.printf("%s  %d%%", charging ? "CHG" : "BATT", pct);
}

void drawForecastRibbon(int x,int y,int w){
  const int cellW = w / DAYS_TO_SHOW;
  static const char* DOW[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};

  for(int i=0;i<DAYS_TO_SHOW;i++){
    int cx = x + i*cellW + 6;
    if (fcast[i].y){
      tm tt{}; 
      tt.tm_year=fcast[i].y-1900; 
      tt.tm_mon=fcast[i].m-1; 
      tt.tm_mday=fcast[i].d; 
      mktime(&tt);

      M5.Display.setTextSize(2);
      M5.Display.setCursor(cx, y+6);
      M5.Display.printf("%s %d", DOW[tt.tm_wday], fcast[i].d);

      M5.Display.setCursor(cx, y+30);
      M5.Display.printf("%s  %d%c/%d%c",
                        fcast[i].cond.c_str(), fcast[i].hi, DEG, fcast[i].lo, DEG);
    }
  }
}

void drawEventBlock(int x,int y,int w,int bottom,const CalendarEvent& ev,int &nextY) {
  int cy = y;

  String sTop = ev.allDay ? "All-day" : time12(ev.sh, ev.sm);
  badge(x + 10, cy, sTop);
  cy += 28;

  if (!ev.allDay && ev.eh >= 0){
    badge(x + 10, cy, time12(ev.eh, ev.em));
    cy += 28;
  }

  cy += 2;

  int textX = x + 10;
  int textW = w - 20;

  M5.Display.setTextSize(2);
  int lhTitle = M5.Display.fontHeight();
  int titleW  = M5.Display.textWidth(ev.title.c_str());
  if (titleW > textW && (cy + lhTitle + 2) <= bottom) {
    addMarquee(textX, cy, textW, lhTitle + 2, ev.title, 2);
    cy += lhTitle + 6;
  } else {
    if (cy + lhTitle <= bottom) {
      M5.Display.setCursor(textX, cy);
      M5.Display.print(ev.title);
      cy += lhTitle + 4;
    }
  }

  if (ev.location.length())
    cy = wrapInsideBox(textX, cy, textW, bottom, ev.location, 1);

  if (cy + 4 <= bottom){
    M5.Display.drawLine(x+10, cy, x+w-10, cy, LINE);
    cy += 6;
  }
  nextY = cy;
}

void drawDayCard(int x,int y,int w,int h,const DayView& dv,bool isToday){
  if (isToday) M5.Display.fillRect(x, y, w, h, TODAY_BG);
  else         M5.Display.fillRect(x, y, w, h, BG);
  M5.Display.drawRect(x, y, w, h, DARKLINE);

  static const char* DOW[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  M5.Display.setTextColor(TEXT);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(x+12, y+6);
  M5.Display.printf("%d", dv.d);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(x+90, y+12);
  M5.Display.printf("%s", DOW[dv.wday]);

  M5.Display.drawLine(x, y+44, x+w, y+44, DARKLINE);

  int curY = y + 52;
  const int bottom = y + h - 8;
  int hidden = 0;

  for (size_t k=0; k<dv.idx.size(); ++k){
    if (curY + 50 > bottom) { hidden = (int)dv.idx.size() - (int)k; break; }
    int after = curY;
    drawEventBlock(x, curY, w, bottom, events[dv.idx[k]], after);
    if (after <= curY) { hidden = (int)dv.idx.size() - (int)k; break; }
    curY = after;
  }

  if (hidden > 0){
    M5.Display.setTextSize(2);
    M5.Display.setCursor(x+10, y+h-28);
    M5.Display.printf("+ %d more", hidden);
  }

  if (dv.idx.empty()){
    M5.Display.setTextSize(2);
    M5.Display.setCursor(x+10, y+72);
    M5.Display.print("No events");
  }
}

void drawAll(){
  M5.Display.fillScreen(BG);
  clearMarquees();

  struct tm t{}; 
  if(!readLocal(t)) return;

  drawHeader(t);
  drawForecastRibbon(10, HEADER_H+4, SCREEN_W-20);

  DayView days[DAYS_TO_SHOW];
  for (int i=0;i<DAYS_TO_SHOW;i++){
    tm dt=t; 
    dt.tm_mday += i; 
    mktime(&dt);
    days[i].y=dt.tm_year+1900; 
    days[i].m=dt.tm_mon+1; 
    days[i].d=dt.tm_mday; 
    days[i].wday=dt.tm_wday;
    for (int j=0;j<eventCount;j++)
      if (events[j].y==days[i].y && events[j].m==days[i].m && events[j].d==days[i].d)
        days[i].idx.push_back(j);
  }

  int top = TOP_AREA_H;
  int h   = SCREEN_H - top - 10;
  int colW = (SCREEN_W - 20 - (DAYS_TO_SHOW-1)*8) / DAYS_TO_SHOW;
  for (int i=0;i<DAYS_TO_SHOW;i++){
    int x = 10 + i*(colW + 8);
    drawDayCard(x, top, colW, h, days[i], i==0);
  }
}

// ---------- SD Card Secrets Loader ----------
void trim_inplace(String &s){
  int i=0; 
  while(i < s.length() && isspace((unsigned char)s[i])) i++;
  if (i) s = s.substring(i);
  int j = (int)s.length()-1; 
  while(j>=0 && isspace((unsigned char)s[j])) j--;
  if (j < (int)s.length()-1) s = s.substring(0, j+1);
}

bool loadSecretsFromSD(const char* path="/secrets.txt"){
  // Initialize all to empty
  ssid = "";
  password = "";
  backup_ssid = "";
  backup_password = "";
  calendarUrl = "";
  calendarUrl2 = "";
  weatherApiKey = "";
  LAT = "25.7617";  // Keep default location
  LON = "-80.1918";

  if (!SD.exists(path)) {
    Serial.println("WARNING: secrets.txt not found on SD card!");
    return false;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println("ERROR: Could not open secrets.txt");
    return false;
  }

  Serial.println("Reading secrets.txt from SD card...");
  bool hasCredentials = false;

  while (f.available()){
    String line = f.readStringUntil('\n');
    trim_inplace(line);
    if (line.length() == 0) continue;
    if (line[0] == '#') continue;

    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    trim_inplace(key); 
    trim_inplace(val);

    // Convert key to uppercase for comparison
    for (int i=0;i<key.length();++i) key[i] = toupper((unsigned char)key[i]);

    // Primary WiFi - support multiple formats
    if (key == "SSID") {
      ssid = val;
      Serial.println("  SSID: " + ssid);
      hasCredentials = true;
    }
    else if (key == "PASSWORD" || key == "PASS") {
      password = val;
      Serial.println("  PASSWORD: [set]");
    }
    // Backup WiFi - support multiple formats
    else if (key == "BACKUP_SSID" || key == "SSID2") {
      backup_ssid = val;
      Serial.println("  BACKUP_SSID: " + backup_ssid);
    }
    else if (key == "BACKUP_PASSWORD" || key == "PASS2") {
      backup_password = val;
      Serial.println("  BACKUP_PASSWORD: [set]");
    }
    // Calendar URLs - support multiple formats
    else if (key == "CAL_URL1" || key == "CALENDARURL" || key == "CALENDAR_URL" || key == "CAL1") {
      calendarUrl = val;
      Serial.println("  CAL_URL1: [set]");
    }
    else if (key == "CAL_URL2" || key == "CALENDARURL2" || key == "CALENDAR_URL2" || key == "CAL2") {
      calendarUrl2 = val;
      Serial.println("  CAL_URL2: [set]");
    }
    // Weather API - support multiple formats
    else if (key == "WEATHER_API_KEY" || key == "WEATHERAPIKEY" || key == "API_KEY" || key == "OWM_KEY") {
      weatherApiKey = val;
      Serial.println("  WEATHER_API_KEY: [set]");
    }
    // Location
    else if (key == "LAT") {
      LAT = val;
      Serial.println("  LAT: " + LAT);
    }
    else if (key == "LON") {
      LON = val;
      Serial.println("  LON: " + LON);
    }
  }
  f.close();

  return hasCredentials;
}

// ---------- WiFi Connection ----------
bool connectWiFiWithFallback(unsigned long timeoutMs = 20000){
  // Try primary WiFi
  if (ssid.length() > 0 && password.length() > 0){
    Serial.print("Connecting to primary WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long start = millis();
    while (millis() - start < timeoutMs){
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to primary WiFi!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        return true;
      }
      delay(250);
    }
    Serial.println("Primary WiFi connection failed");
  } else {
    Serial.println("No primary WiFi credentials provided");
  }

  // Try backup WiFi if defined
  if (backup_ssid.length() > 0 && backup_password.length() > 0){
    Serial.print("Connecting to backup WiFi: ");
    Serial.println(backup_ssid);
    WiFi.begin(backup_ssid.c_str(), backup_password.c_str());
    
    unsigned long start = millis();
    while (millis() - start < timeoutMs){
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to backup WiFi!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        return true;
      }
      delay(250);
    }
    Serial.println("Backup WiFi connection failed");
  }

  Serial.println("WiFi connection failed - no valid credentials or connection");
  return false;
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== M5Paper S3 Calendar Starting ===");

  auto cfg = M5.config(); 
  cfg.clear_display = true; 
  M5.begin(cfg);
  
  if (M5.Display.isEPD()) {
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    M5.Display.invertDisplay(true);
  }
  M5.Display.setRotation(1);
  M5.Display.setTextColor(TEXT);
  M5.Display.fillScreen(BG);

  // Initialize SD card
  Serial.println("Initializing SD card...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS, SPI, 25000000)) {
    Serial.println("ERROR: SD Card initialization failed!");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 20);
    M5.Display.print("ERROR: SD Card Failed!");
    M5.Display.setCursor(20, 50);
    M5.Display.print("Please insert SD card with secrets.txt");
    return;  // Cannot continue without SD card
  }
  
  Serial.println("SD card initialized successfully");

  // Load credentials from SD card
  if (!loadSecretsFromSD("/secrets.txt")) {
    Serial.println("ERROR: Failed to load credentials from SD card");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 20);
    M5.Display.print("ERROR: secrets.txt missing or invalid");
    M5.Display.setCursor(20, 50);
    M5.Display.print("Please create secrets.txt on SD card");
    return;  // Cannot continue without credentials
  }

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  if (!connectWiFiWithFallback(15000)) {
    Serial.println("WARNING: WiFi connection failed - continuing with limited functionality");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 20);
    M5.Display.print("WiFi connection failed");
    M5.Display.setCursor(20, 50);
    M5.Display.print("Check credentials in secrets.txt");
    delay(3000);
  }

  // Setup time
  setupTime();
  delay(1200);
  syncRTCFromNTP();

  // Fetch data
  lastWxMS = millis();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Fetching weather...");
    fetchWeather();
    Serial.println("Fetching calendar...");
    fetchCalendar();
  } else {
    Serial.println("Skipping data fetch - no WiFi connection");
  }

  struct tm t{}; 
  readLocal(t);
  lastMinute = -1;
  lastY = t.tm_year+1900; 
  lastM = t.tm_mon+1; 
  lastD = t.tm_mday;

  Serial.println("Drawing display...");
  drawAll();
  Serial.println("Setup complete!");
}

void loop() {
  struct tm t{};
  bool haveTime = readLocal(t);

  M5.update();
  auto td = M5.Touch.getDetail();

  static unsigned long touchExpire = 0;
  if (td.isPressed() || td.wasPressed()) {
    g_marqueeTouchActive = true;
    touchExpire = millis() + 1200;
  } else if (millis() > touchExpire) {
    g_marqueeTouchActive = false;
  }

  updateMarquees();

  bool needsUpdate = false;

  // Header clock: redraw once per minute
  if (haveTime && t.tm_min != lastMinute) {
    lastMinute = t.tm_min;
    // redraw header background consistently
    M5.Display.fillRect(0, 0, SCREEN_W, HEADER_H, SUBTLE);
    drawHeader(t);
    needsUpdate = true;
  }

  // Calendar: refresh at midnight
  if (haveTime) {
    int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
    if (y != lastY || m != lastM || d != lastD) {
      lastY = y; lastM = m; lastD = d;
      if (WiFi.status() == WL_CONNECTED) fetchCalendar();
      drawAll();
      needsUpdate = true;
    }
  }

  // Weather: refresh every 30 minutes
  if (millis() - lastWxMS > WX_PERIOD) {
    if (WiFi.status() == WL_CONNECTED) fetchWeather();
    lastWxMS = millis();
    drawAll();
    needsUpdate = true;
  }

  // No light sleep — keep the UI responsive for touch/scroll
  delay(30);
}
