#include "HIDApp.h"

#include <vector>
#include <cstring>
#include <cctype>

#include <M5GFX.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

#include "Config.h"  // for colors BG/TEXT, etc.

namespace {

// --- Public-facing flags  ---
static bool g_hidActive     = false;
static bool g_hidJustExited = false;

// --- HID devices ---
static USBHIDKeyboard sKeyboard;
static USBHIDMouse    sMouse;

#ifndef KEY_RETURN
  #define KEY_RETURN 0x28
#endif
#ifndef KEY_ESC
  #define KEY_ESC 0x29
#endif
#ifndef KEY_BACKSPACE
  #define KEY_BACKSPACE 0x2A
#endif
#ifndef KEY_TAB
  #define KEY_TAB 0x2B
#endif
#ifndef KEY_CAPS_LOCK
  #define KEY_CAPS_LOCK 0x39
#endif
#ifndef KEY_F1
  #define KEY_F1  0x3A
  #define KEY_F2  0x3B
  #define KEY_F3  0x3C
  #define KEY_F4  0x3D
  #define KEY_F5  0x3E
  #define KEY_F6  0x3F
  #define KEY_F7  0x40
  #define KEY_F8  0x41
  #define KEY_F9  0x42
  #define KEY_F10 0x43
  #define KEY_F11 0x44
  #define KEY_F12 0x45
#endif
#ifndef KEY_RIGHT_ARROW
  #define KEY_RIGHT_ARROW 0x4F
  #define KEY_LEFT_ARROW  0x50
  #define KEY_DOWN_ARROW  0x51
  #define KEY_UP_ARROW    0x52
#endif
#ifndef KEY_LEFT_CTRL
  #define KEY_LEFT_CTRL  0xE0
  #define KEY_LEFT_SHIFT 0xE1
  #define KEY_LEFT_ALT   0xE2
#endif

// --- Modes, layout, fonts ---
enum DeviceMode { MODE_KEYBOARD, MODE_TOUCHPAD };
static DeviceMode sMode = MODE_KEYBOARD;

static int SCR_W=960, SCR_H=540;
static int MARGIN = 8;
static int PAD    = 6;
static int HEADER_H = 32;
static int N_ROWS = 6;
static int ROW_H  = 80;

static const lgfx::IFont* FONT = &fonts::Font4;

// --- Touchpad state ---
struct TouchpadState {
  bool isPressed = false;
  int lastX = 0, lastY = 0;
  bool wasDragging = false;
  unsigned long lastTapTime = 0;
  int tapCount = 0;
  static const unsigned long DOUBLE_TAP_TIMEOUT = 300; // ms
  static const int MOUSE_SENSITIVITY = 3;
};
static TouchpadState touch;

// --- Keys ---
enum ArrowDir : uint8_t { AR_NONE=0, AR_L, AR_D, AR_U, AR_R };

struct Key {
  const char* label;
  const char* shiftLabel;
  int primary;
  bool isModifier;
  bool isCaps;
  ArrowDir arrow;
  uint16_t x, y, w, h;
};
static std::vector<Key> keys;

// --- Input toggles ---
static bool kShift = false;
static bool kCtrl  = false;
static bool kAlt   = false;
static bool kCaps  = false;
static bool wasPressed = false;
static int  lastIdx = -1;

// ===================== Helpers =====================
inline void drawTriangle(uint16_t cx, uint16_t cy, uint16_t w, uint16_t h, ArrowDir dir, uint16_t color) {
  auto& d = M5.Display;
  int x0, y0, x1, y1, x2, y2;
  if (dir == AR_U) { x0=cx; y0=cy-h/3; x1=cx-w/4; y1=cy+h/6; x2=cx+w/4; y2=cy+h/6; }
  else if (dir == AR_D){ x0=cx; y0=cy+h/3; x1=cx-w/4; y1=cy-h/6; x2=cx+w/4; y2=cy-h/6; }
  else if (dir == AR_L){ x0=cx-w/3; y0=cy; x1=cx+w/6; y1=cy-h/4; x2=cx+w/6; y2=cy+h/4; }
  else {                 x0=cx+w/3; y0=cy; x1=cx-w/6; y1=cy-h/4; x2=cx-w/6; y2=cy+h/4; }
  d.drawLine(x0,y0,x1,y1,color);
  d.drawLine(x1,y1,x2,y2,color);
  d.drawLine(x2,y2,x0,y0,color);
}

inline void drawKey(const Key& k, bool pressed=false) {
  auto& d = M5.Display;
  uint16_t fg = TEXT, bg = BG;
  if (pressed) std::swap(fg, bg);
  d.fillRoundRect(k.x, k.y, k.w, k.h, 12, bg);
  d.drawRoundRect(k.x, k.y, k.w, k.h, 12, fg);

  d.setFont(FONT);
  d.setTextColor(fg, bg);
  d.setTextDatum(textdatum_t::middle_center);
  d.setTextSize(1.0f);

  if (k.arrow != AR_NONE) {
    drawTriangle(k.x + k.w/2, k.y + k.h/2, k.w*0.6, k.h*0.6, k.arrow, fg);
  } else {
    // visual label: CAPS ^ SHIFT for letters
    String center = k.label ? String(k.label) : "";
    if (!k.isModifier && !k.isCaps && k.label && strlen(k.label) == 1 &&
        isalpha((unsigned char)k.label[0])) {
      bool upper = (kCaps ^ kShift);
      char ch = upper ? (char)toupper((unsigned char)k.label[0])
                      : (char)tolower((unsigned char)k.label[0]);
      center = String(ch);
    }
    d.drawString(center, k.x + k.w/2, k.y + k.h/2);
  }

  if (k.shiftLabel && kShift && !k.isCaps && !k.isModifier) {
    d.setTextDatum(textdatum_t::top_right);
    d.setTextSize(0.8f);
    d.drawString(k.shiftLabel, k.x + k.w - 6, k.y + 6);
  }

  d.setTextDatum(textdatum_t::top_left);
  d.setTextSize(1.0f);
}

inline void drawHeader() {
  auto& d = M5.Display;
  d.fillRect(0, 0, SCR_W, HEADER_H, DARKLINE);
  d.setTextColor(BG);
  d.setFont(FONT);
  d.setTextDatum(textdatum_t::middle_left);
  d.drawString(sMode == MODE_KEYBOARD ? "USB HID Keyboard" : "USB HID Touchpad/Mouse", 12, HEADER_H/2);

  // Exit button
  int btnW=120, btnH=HEADER_H-6, btnX=SCR_W-8-btnW, btnY=3;
  d.fillRoundRect(btnX, btnY, btnW, btnH, 8, 0x07E0 /*green*/);
  d.drawRoundRect(btnX, btnY, btnW, btnH, 8, BG);
  d.setTextDatum(textdatum_t::middle_center);
  d.setTextColor(BG);
  d.drawString("Calendar", btnX+btnW/2, btnY+btnH/2);

  // Mod indicators / help
  d.setTextDatum(textdatum_t::middle_right);
  if (sMode == MODE_KEYBOARD) {
    String mods;
    if (kCaps)  mods += "CAPS ";
    if (kShift) mods += "SHIFT ";
    if (kCtrl)  mods += "CTRL ";
    if (kAlt)   mods += "ALT ";
    d.drawString(mods, SCR_W - btnW - 16, HEADER_H/2);
  } else {
    d.drawString("Double tap = right click", SCR_W - btnW - 16, HEADER_H/2);
  }
}

inline void drawTouchpad() {
  auto& d = M5.Display;
  d.fillScreen(SUBTLE);
  drawHeader();

  int padTop = HEADER_H + MARGIN;
  int padBottom = SCR_H - MARGIN - 60;
  int padLeft = MARGIN;
  int padRight = SCR_W - MARGIN;

  d.fillRoundRect(padLeft, padTop, padRight - padLeft, padBottom - padTop, 20, BG);
  d.drawRoundRect(padLeft, padTop, padRight - padLeft, padBottom - padTop, 20, DARKLINE);

  // Mode switch button (bottom-right)
  int btnW = 150, btnH = 50;
  int btnX = SCR_W - MARGIN - btnW;
  int btnY = SCR_H - MARGIN - btnH;
  d.fillRoundRect(btnX, btnY, btnW, btnH, 12, 0x001F /*blue*/);
  d.drawRoundRect(btnX, btnY, btnW, btnH, 12, DARKLINE);
  d.setTextColor(BG);
  d.setTextDatum(textdatum_t::middle_center);
  d.drawString("Keyboard", btnX + btnW/2, btnY + btnH/2);
}

inline void drawAll(bool full=true) {
  // normalize draw state
  M5.Display.setTextWrap(false);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setTextSize(1.0f);
  M5.Display.setTextColor(TEXT, BG);
  M5.Display.setFont(FONT);

  if (sMode == MODE_TOUCHPAD) {
    drawTouchpad();
    return;
  }
  if (full) M5.Display.fillScreen(BG);
  drawHeader();
  for (auto &k : keys) drawKey(k, false);
}

// layout helpers
inline int rowY(int rowIdx) {
  int top = HEADER_H + MARGIN;
  return top + rowIdx * (ROW_H + PAD);
}

inline Key K(const char* label, int code, const char* shift=nullptr,
             bool isMod=false, bool isCaps=false, ArrowDir ar=AR_NONE) {
  Key k{label, shift, code, isMod, isCaps, ar, 0,0,0,0};
  return k;
}

inline std::vector<Key> chars(const char* cs, const char* sh=nullptr) {
  std::vector<Key> v;
  size_t n = strlen(cs);
  for (size_t i=0;i<n;++i) {
    char *lab = strdup((std::string("")+cs[i]).c_str());
    const char* s = nullptr;
    if (sh) { char *sb = strdup((std::string("")+sh[i]).c_str()); s = sb; }
    v.push_back(K(lab, (int)cs[i], s));
  }
  return v;
}

inline void addUniformRow(int rowIdx, const std::vector<Key>& items) {
  int N = (int)items.size();
  int y = rowY(rowIdx);
  int x = MARGIN;
  int cellW = (SCR_W - 2*MARGIN - (N-1)*PAD) / N;
  for (int i=0;i<N;++i) {
    Key k = items[i];
    k.x = x; k.y = y; k.w = cellW; k.h = ROW_H;
    keys.push_back(k);
    x += cellW + PAD;
  }
}

inline void addRowWideEnds(int rowIdx, const Key& leftWide, const std::vector<Key>& midKeys, const Key& rightWide) {
  int y = rowY(rowIdx);
  int baseW = (SCR_W - 2*MARGIN - (13-1)*PAD) / 13;
  Key L = leftWide, R = rightWide;
  L.x = MARGIN; L.y = y; L.w = baseW*2 + PAD; L.h = ROW_H;
  R.w = baseW*2 + PAD; R.h = ROW_H; R.y = y; R.x = SCR_W - MARGIN - R.w;

  int x = L.x + L.w + PAD;
  int avail = R.x - PAD - x;
  int n = (int)midKeys.size();
  int cellW = (avail - (n-1)*PAD) / n;
  for (int i=0;i<n;++i) {
    Key k = midKeys[i];
    k.x = x; k.y = y; k.w = cellW; k.h = ROW_H;
    keys.push_back(k);
    x += cellW + PAD;
  }
  keys.push_back(L);
  keys.push_back(R);
}

inline void addRowWithSmallerEnd(int rowIdx, const Key& leftWide, const std::vector<Key>& midKeys,
                                 const Key& normalKey, const Key& switchKey) {
  int y = rowY(rowIdx);
  int baseW = (SCR_W - 2*MARGIN - (15-1)*PAD) / 15;
  Key L = leftWide;
  L.x = MARGIN; L.y = y; L.w = baseW*2 + PAD; L.h = ROW_H;

  Key S = switchKey;
  S.w = baseW + baseW/2; S.h = ROW_H; S.y = y;
  S.x = SCR_W - MARGIN - S.w;

  Key N = normalKey;
  N.w = baseW; N.h = ROW_H; N.y = y;
  N.x = S.x - PAD - N.w;

  int x = L.x + L.w + PAD;
  int avail = N.x - PAD - x;
  int n = (int)midKeys.size();
  int cellW = (avail - (n-1)*PAD) / n;

  for (int i=0;i<n;++i) {
    Key k = midKeys[i];
    k.x = x; k.y = y; k.w = cellW; k.h = ROW_H;
    keys.push_back(k);
    x += cellW + PAD;
  }
  keys.push_back(L);
  keys.push_back(N);
  keys.push_back(S);
}

// send key
inline void sendKey(const Key& k) {
  // switch to touchpad from grid
  if (k.label && !strcmp(k.label, "Touch")) {
    sMode = MODE_TOUCHPAD;
    drawAll(true);
    return;
  }

  // toggles
  if (k.isCaps) {
    kCaps = !kCaps;
    sKeyboard.press(KEY_CAPS_LOCK);
    sKeyboard.release(KEY_CAPS_LOCK);
    drawAll(false);                 // redraw labels to show new case
    return;
  }

  if (k.isModifier) {
    if (k.label && !strcmp(k.label,"Shift")) kShift = !kShift;
    else if (k.label && !strcmp(k.label,"Ctrl"))  kCtrl = !kCtrl;
    else if (k.label && !strcmp(k.label,"Alt"))   kAlt  = !kAlt;
    drawAll(false);                 // update header and keycaps
    return;
  }

  // normal keys
  if (kCtrl) sKeyboard.press(KEY_LEFT_CTRL);
  if (kAlt)  sKeyboard.press(KEY_LEFT_ALT);

  if (k.primary >= 32 && k.primary <= 126 && isalpha(k.primary)) {
    char c = (char)k.primary;
    bool upper = (kCaps ^ kShift);
    if (upper) {
      sKeyboard.press(KEY_LEFT_SHIFT);
      sKeyboard.write((char)tolower(c));
      sKeyboard.release(KEY_LEFT_SHIFT);
    } else {
      sKeyboard.write((char)tolower(c));
    }
  }
  else if (k.shiftLabel && kShift) {
    sKeyboard.press(KEY_LEFT_SHIFT);
    sKeyboard.write(k.shiftLabel[0]);
    sKeyboard.release(KEY_LEFT_SHIFT);
  }
  else if (k.primary >= 32 && k.primary <= 126) {
    sKeyboard.write((char)k.primary);
  }
  else {
    sKeyboard.press(k.primary);
    sKeyboard.release(k.primary);
  }

  // auto-unlatch momentary mods and reflect visually
  bool needRedraw = false;
  if (kCtrl) { sKeyboard.release(KEY_LEFT_CTRL); kCtrl=false; needRedraw=true; }
  if (kAlt)  { sKeyboard.release(KEY_LEFT_ALT);  kAlt=false;  needRedraw=true; }
  if (kShift){                                   kShift=false; needRedraw=true; }

  if (needRedraw) drawAll(false);
  else            drawHeader();
}

// hit tests
inline int hitKey(int x, int y) {
  for (size_t i=0;i<keys.size();++i){
    auto& k = keys[i];
    if ((unsigned)x >= k.x && (unsigned)x < k.x + k.w &&
        (unsigned)y >= k.y && (unsigned)y < k.y + k.h) return (int)i;
  }
  return -1;
}

inline bool hitExit(int x, int y) {
  int btnW=120, btnH=HEADER_H-6, btnX=SCR_W-8-btnW, btnY=3;
  return (x>=btnX && x<btnX+btnW && y>=btnY && y<btnY+btnH);
}

inline bool hitTouchpadModeBtn(int x, int y) {
  if (sMode != MODE_TOUCHPAD) return false;
  int btnW = 150, btnH = 50;
  int btnX = SCR_W - MARGIN - btnW;
  int btnY = SCR_H - MARGIN - btnH;
  return (x>=btnX && x<btnX+btnW && y>=btnY && y<btnY+btnH);
}

// touchpad logic
inline void touchpadInput(int x, int y, bool isPressed) {
  unsigned long now = millis();

  if (isPressed && !touch.isPressed) {
    touch.lastX = x; touch.lastY = y;
    touch.wasDragging = false; touch.isPressed = true;
  } else if (isPressed && touch.isPressed) {
    int dx = (x - touch.lastX) * TouchpadState::MOUSE_SENSITIVITY;
    int dy = (y - touch.lastY) * TouchpadState::MOUSE_SENSITIVITY;
    if (abs(dx) > 2 || abs(dy) > 2) { sMouse.move(dx, dy); touch.wasDragging = true; }
    touch.lastX = x; touch.lastY = y;
  } else if (!isPressed && touch.isPressed) {
    touch.isPressed = false;
    if (!touch.wasDragging) {
      if (now - touch.lastTapTime < TouchpadState::DOUBLE_TAP_TIMEOUT) {
        touch.tapCount++;
        if (touch.tapCount == 2) { sMouse.click(MOUSE_RIGHT); touch.tapCount=0; }
      } else {
        sMouse.click(MOUSE_LEFT);
        touch.tapCount = 1;
        touch.lastTapTime = now;
      }
    }
  }
  if (now - touch.lastTapTime > TouchpadState::DOUBLE_TAP_TIMEOUT) touch.tapCount = 0;
}

// build keyboard layout
inline void buildLayout() {
  keys.clear();
  SCR_W = M5.Display.width();
  SCR_H = M5.Display.height();
  int avail = SCR_H - HEADER_H - 2*MARGIN - (N_ROWS-1)*PAD;
  ROW_H = avail / N_ROWS;

  // Row 0: Esc + F1..F12
  addUniformRow(0, {
    K("Esc", KEY_ESC),
    K("F1",KEY_F1),K("F2",KEY_F2),K("F3",KEY_F3),K("F4",KEY_F4),
    K("F5",KEY_F5),K("F6",KEY_F6),K("F7",KEY_F7),K("F8",KEY_F8),
    K("F9",KEY_F9),K("F10",KEY_F10),K("F11",KEY_F11),K("F12",KEY_F12)
  });

  // Row 1: `1234567890-= + Backspace(2u)
  std::vector<Key> num = chars("`1234567890-=", "~!@#$%^&*()_+");
  {
    int y = rowY(1);
    int n = (int)num.size();
    int baseW = (SCR_W - 2*MARGIN - (13-1)*PAD)/13;
    int backW = baseW*2 + PAD;
    int x = MARGIN;
    int cellW = (SCR_W - 2*MARGIN - backW - (n-1)*PAD)/n;
    for (int i=0;i<n;++i) {
      Key k = num[i]; k.x=x; k.y=y; k.w=cellW; k.h=ROW_H; keys.push_back(k);
      x += cellW + PAD;
    }
    Key back = K("Back", KEY_BACKSPACE);
    back.w=backW; back.h=ROW_H; back.y=y; back.x=SCR_W-MARGIN-back.w;
    keys.push_back(back);
  }

  // Row 2: Tab + qwertyuiop[] + \ + Touch switch
  {
    Key tab = K("Tab", KEY_TAB);
    auto mid = chars("qwertyuiop[]", "{}");
    for (int i = 0; i < 10; ++i) {
      char *up = strdup(String((char)toupper("qwertyuiop"[i])).c_str());
      mid[i].shiftLabel = up;
    }
    mid[(int)mid.size()-2].shiftLabel = strdup("{");
    mid[(int)mid.size()-1].shiftLabel = strdup("}");
    Key bsl = K("\\", (int)'\\', "|");
    Key touchBtn = K("Touch", 0);
    addRowWithSmallerEnd(2, tab, mid, bsl, touchBtn);
  }

  // Row 3: Caps + asdfghjkl;' + Enter
  {
    Key caps = K("Caps", KEY_CAPS_LOCK, nullptr, false, true);
    auto mid = chars("asdfghjkl;\'");
    const char* base3 = "asdfghjkl";
    for (int i = 0; i < 9; ++i) {
      char *up = strdup(String((char)toupper(base3[i])).c_str());
      mid[i].shiftLabel = up;
    }
    mid[(int)mid.size()-2].shiftLabel = strdup(":");
    mid[(int)mid.size()-1].shiftLabel = strdup("\"");
    Key enter = K("Enter", KEY_RETURN);
    addRowWideEnds(3, caps, mid, enter);
  }

  // Row 4: Shift + zxcvbnm,./ + Shift
  {
    Key shL = K("Shift", 0, nullptr, true, false);
    auto mid = chars("zxcvbnm,./", "<>?");
    const char* base4 = "zxcvbnm";
    for (int i = 0; i < 7; ++i) {
      char *up = strdup(String((char)toupper(base4[i])).c_str());
      mid[i].shiftLabel = up;
    }
    mid[(int)mid.size()-3].shiftLabel = strdup("<");
    mid[(int)mid.size()-2].shiftLabel = strdup(">");
    mid[(int)mid.size()-1].shiftLabel = strdup("?");
    Key shR = K("Shift", 0, nullptr, true, false);
    addRowWideEnds(4, shL, mid, shR);
  }

  // Row 5: Ctrl Alt Space + Arrows
  {
    int y = rowY(5);
    int u = (SCR_W - 2*MARGIN - (15-1)*PAD) / 15;
    int arrowW = 4*u + 3*PAD;
    int arrowX = SCR_W - MARGIN - arrowW;
    int ctrlW = 2*u + PAD;
    int altW  = 2*u + PAD;
    int leftX = MARGIN;
    int spaceX = leftX + ctrlW + PAD + altW + PAD;
    int spaceW = arrowX - PAD - spaceX;

    Key ctrl = K("Ctrl", KEY_LEFT_CTRL, nullptr, true);
    ctrl.x=leftX; ctrl.y=y; ctrl.w=ctrlW; ctrl.h=ROW_H; keys.push_back(ctrl);

    Key alt = K("Alt", KEY_LEFT_ALT, nullptr, true);
    alt.x=ctrl.x+ctrl.w+PAD; alt.y=y; alt.w=altW; alt.h=ROW_H; keys.push_back(alt);

    Key sp = K("Space",' ');
    sp.x=spaceX; sp.y=y; sp.w=spaceW; sp.h=ROW_H; keys.push_back(sp);

    Key L = K("", KEY_LEFT_ARROW,  nullptr, false, false, AR_L);
    Key D = K("", KEY_DOWN_ARROW,  nullptr, false, false, AR_D);
    Key U = K("", KEY_UP_ARROW,    nullptr, false, false, AR_U);
    Key R = K("", KEY_RIGHT_ARROW, nullptr, false, false, AR_R);

    int ax = arrowX; int cell = u;
    L.x=ax; L.y=y; L.w=cell; L.h=ROW_H; keys.push_back(L); ax += cell + PAD;
    D.x=ax; D.y=y; D.w=cell; D.h=ROW_H; keys.push_back(D); ax += cell + PAD;
    U.x=ax; U.y=y; U.w=cell; U.h=ROW_H; keys.push_back(U); ax += cell + PAD;
    R.x=ax; R.y=y; R.w=cell; R.h=ROW_H; keys.push_back(R);
  }
}

// one-time draw on activation
inline void enterApp() {
  static bool usbStarted = false;
  if (!usbStarted) {
    USB.begin();
    sKeyboard.begin();
    sMouse.begin();
    usbStarted = true;
  }
  if (M5.Display.isEPD()) M5.Display.setEpdMode(epd_mode_t::epd_fastest);

  M5.Display.setTextWrap(false);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setTextSize(1.0f);
  M5.Display.setTextColor(TEXT, BG);
  M5.Display.setFont(FONT);

  buildLayout();
  drawAll(true);
}

} 

// ===================== Public API =====================
void hid_begin() {
  static bool inited = false;
  if (inited) return;
  inited = true;
  USB.begin(); 
}

void hid_setActive(bool on) { g_hidActive = on; }
bool hid_isActive()         { return g_hidActive; }

bool hid_justExited() {
  bool v = g_hidJustExited;
  g_hidJustExited = false;
  return v;
}

bool hid_tick() {
  if (!g_hidActive) return false;

  SCR_W = M5.Display.width();
  SCR_H = M5.Display.height();

  static bool shown = false;
  if (!shown) { enterApp(); shown = true; }

  M5.update();
  auto t = M5.Touch.getDetail();
  bool pressed = t.isPressed();

  // Exit to Calendar?
  if (pressed && hitExit(t.x, t.y)) {
    g_hidActive = false;
    shown = false;

    if (M5.Display.isEPD()) M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    M5.Display.setTextWrap(false);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextColor(TEXT, BG);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.fillScreen(BG);

    g_hidJustExited = true;
    return false;
  }

  if (sMode == MODE_KEYBOARD) {
    if (pressed && !wasPressed) {
      int idx = hitKey(t.x, t.y);
      lastIdx = idx;
      if (idx >= 0) {
        drawKey(keys[idx], true);
        sendKey(keys[idx]);
      }
    } else if (!pressed && wasPressed) {
      if (lastIdx >= 0) drawKey(keys[lastIdx], false);
      lastIdx = -1;
    }
    wasPressed = pressed;
  } else {
    if (pressed) {
      if (hitTouchpadModeBtn(t.x, t.y)) {
        sMode = MODE_KEYBOARD;
        drawAll(true);
      } else {
        touchpadInput(t.x, t.y, true);
      }
    } else {
      touchpadInput(t.x, t.y, false);
    }
  }

  delay(5);
  return true;  // HID handled the frame
}
