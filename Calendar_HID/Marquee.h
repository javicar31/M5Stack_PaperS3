#ifndef MARQUEE_H
#define MARQUEE_H

#include "AppState.h"

inline void clearMarquees() {
  for (auto &m : marquees) {
    if (m.sp) { m.sp->deleteSprite(); delete m.sp; m.sp = nullptr; }
  }
  marquees.clear();
}

inline void addMarquee(int x,int y,int w,int h,const String& text,int textSize) {
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

inline void updateMarquees() {
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

#endif // MARQUEE_H
