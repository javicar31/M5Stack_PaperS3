#ifndef DRAWING_H
#define DRAWING_H

#include "AppState.h"
#include "Marquee.h"
#include "TimeUtil.h"

inline void badge(int x,int y,const String& s){
  M5.Display.setTextSize(2);
  int tw = M5.Display.textWidth(s.c_str()), th = M5.Display.fontHeight();
  int padX = 8, padY = 4;
  M5.Display.fillRoundRect(x, y, tw + 2*padX, th + 2*padY, 8, BADGE_FILL);
  M5.Display.drawRoundRect(x, y, tw + 2*padX, th + 2*padY, 8, DARKLINE);
  M5.Display.setCursor(x + padX, y + padY + 1);
  M5.Display.setTextColor(TEXT);
  M5.Display.print(s);
}

inline int wrapInsideBox(int x,int y,int w,int bottom,const String& text,int sz){
  M5.Display.setTextSize(sz);
  int lineH = M5.Display.fontHeight();
  String word, line; int cy = y;

  auto flushLine = [&](){
    if (cy + lineH > bottom) return false;
    M5.Display.setCursor(x, cy);
    M5.Display.print(line);
    cy += lineH + 2; line = "";
    return true;
  };

  for (int i = 0; i <= text.length(); ++i){
    char c = (i < text.length()) ? text[i] : ' ';
    if (c == ' ' || c == '\n' || i == text.length()){
      String prospect = line.length() ? (line + " " + word) : word;
      if (M5.Display.textWidth(prospect.c_str()) <= w) line = prospect;
      else { if (!flushLine()) return bottom; line = word; }
      word = "";
      if (c == '\n'){ if (!flushLine()) return bottom; }
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

inline void drawHeader(const tm& t){
  M5.Display.fillRect(0, 0, SCREEN_W, HEADER_H, SUBTLE);
  M5.Display.drawLine(0, HEADER_H, SCREEN_W, HEADER_H, DARKLINE);

  static const char* MONTHS[] = {
    "JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"
  };
  static const char* DOW[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};

  M5.Display.setTextColor(TEXT);

  // Month + year
  M5.Display.setTextSize(3);
  M5.Display.setCursor(20, 18);
  M5.Display.printf("%s %d", MONTHS[t.tm_mon], t.tm_year + 1900);

  // Big day
  M5.Display.setTextSize(6);
  M5.Display.setCursor(20, 62);
  M5.Display.printf("%s %d", DOW[t.tm_wday], t.tm_mday);

  // Time
  M5.Display.setTextSize(6);
  M5.Display.setCursor(360, 22);
  char buf[16];
  sprintf(buf, "%d:%02d %s",
          (t.tm_hour % 12) ? (t.tm_hour % 12) : 12,
          t.tm_min,
          (t.tm_hour >= 12) ? "PM" : "AM");
  M5.Display.print(buf);

  // Right-side weather block
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

  // HID button
  const int hidBtnW = 80, hidBtnH = 30;
  const int hidBtnX = SCREEN_W - 90;
  const int hidBtnY = HEADER_H - hidBtnH - 6;
  M5.Display.fillRoundRect(hidBtnX, hidBtnY, hidBtnW, hidBtnH, 8, 0x001F /*blue*/);
  M5.Display.drawRoundRect(hidBtnX, hidBtnY, hidBtnW, hidBtnH, 8, DARKLINE);
  M5.Display.setTextColor(BG);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.drawString("HID", hidBtnX + hidBtnW/2, hidBtnY + hidBtnH/2);

  // restore defaults for the rest of the UI
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setTextColor(TEXT);   // <-- important so ribbon text is visible
}

inline void drawForecastRibbon(int x,int y,int w){
  // Harden state to avoid inheriting from other modules
  M5.Display.setTextColor(TEXT, BG);
  M5.Display.setFont(&fonts::Font0);

  const int cellW = w / DAYS_TO_SHOW;

  for (int i = 0; i < DAYS_TO_SHOW; i++){
    int cx = x + i*cellW + 6;
    if (fcast[i].y){
      M5.Display.setTextSize(2);
      M5.Display.setCursor(cx, y + 18);
      M5.Display.printf("%s  %d%c/%d%c",
                        fcast[i].cond.c_str(), fcast[i].hi, DEG, fcast[i].lo, DEG);
    }
  }
}

inline void drawEventBlock(int x,int y,int w,int bottom,const CalendarEvent& ev,int &nextY) {
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

inline void drawDayCard(int x,int y,int w,int h,const DayView& dv,bool isToday){
  if (isToday) M5.Display.fillRect(x, y, w, h, TODAY_BG);
  else         M5.Display.fillRect(x, y, w, h, BG);
  M5.Display.drawRect(x, y, w, h, DARKLINE);

  static const char* DOW[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  M5.Display.setTextColor(TEXT);

  M5.Display.setTextSize(isToday ? 5 : 4);
  M5.Display.setCursor(x+12, y+6);
  M5.Display.printf("%d", dv.d);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(x + (isToday ? 108 : 90), y+12);
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

inline void drawAll(){
  // Normalize draw state for a clean full redraw
  M5.Display.setTextWrap(false);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setTextSize(1.0f);
  M5.Display.setTextColor(TEXT, BG);
  M5.Display.setFont(&fonts::Font0);

  M5.Display.fillScreen(BG);
  clearMarquees();

  struct tm t{};
  if (!readLocal(t)) return;

  drawHeader(t);
  drawForecastRibbon(10, HEADER_H + 4, SCREEN_W - 20);

  DayView days[DAYS_TO_SHOW];
  for (int i = 0; i < DAYS_TO_SHOW; i++){
    tm dt = t;
    dt.tm_mday += i;
    mktime(&dt);
    days[i].y = dt.tm_year + 1900;
    days[i].m = dt.tm_mon + 1;
    days[i].d = dt.tm_mday;
    days[i].wday = dt.tm_wday;
    for (int j = 0; j < eventCount; j++)
      if (events[j].y==days[i].y && events[j].m==days[i].m && events[j].d==days[i].d)
        days[i].idx.push_back(j);
  }

  int top = TOP_AREA_H;
  int h   = SCREEN_H - top - 10;

  const int gap = 8;
  int colW = (SCREEN_W - 20 - (DAYS_TO_SHOW-1)*gap) / DAYS_TO_SHOW;

  for (int i = 0; i < DAYS_TO_SHOW; i++){
    int x = 10 + i*(colW + gap);
    int y = top;
    int w = colW;
    int hh = h;

    bool isToday = (i == 0);
    if (isToday) { x -= 4; y -= 4; w += 8; hh += 8; }

    drawDayCard(x, y, w, hh, days[i], isToday);
  }
}

#endif // DRAWING_H