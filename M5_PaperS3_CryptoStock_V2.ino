//TODO: Add VOlume Fix 

#include <vector>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <time.h>

// ---------- SD pins (PaperS3 defaults) ----------
#define SD_CS   47
#define SD_SCK  39
#define SD_MOSI 38
#define SD_MISO 40

// --------------------
void fetchNewsHeadlines(const String& symbol);
void fetchStockDetail(const String& symbol, float& price, float& high, float& low,
                      float& open, float& prevClose, float& change);
void fetchCryptoDetail(const String& instrument, float& price, float& high, float& low,
                       float& open, float& prevClose, float& changePct, float& volume);
String fetchCompanyName(const String& symbol);
void updatePriceIfNeeded(const String& symbol, bool isCrypto);
void drawMenu();
void drawDetail(const String &symbol);
void drawClockScreen(bool firstDraw);
void drawClockFace(bool firstDraw);
void handleTouch();
bool loadVLWIntoRAM(fs::FS &fs, const char* path);
bool tryLoadClockFontFromSD();

// -------- Layout --------
static const int kBtnW        = 200;
static const int kBtnH        = 50;
static const int kGapX        = 20;
static const int kGapY        = 12;
static const int kLeftMargin  = 40;
static const int kTopMargin   = 60;
static const int kCols        = 4;

// alarm variables
static int alarmHour = 8;
static int alarmMinute = 0;
static bool alarmEnabled = false;

// view state for alarm setting
enum View { VIEW_MENU, VIEW_DETAIL, VIEW_CLOCK, VIEW_ALARM_SET };
View currentView = VIEW_MENU;

// Clock button bottom-right
static const int kClockCx     = 865;
static const int kClockCy     = 430;
static const int kClockR      = 55;

// Clock sizes (built-in font only; no VLW scaling)
static const int CLOCK_TEXT_SIZE = 7;   // big time (fallback)
static const int DATE_TEXT_SIZE  = 2;   // small day/date
static const int RET_Y           = 440; // Return button Y
// Clock rendering state (avoid repeated VLW loads & font switches)
static bool s_clockStaticDrawn = false;   // drawn date/button once?
static bool s_clockVlwActive   = false;   // VLW is currently the active font?
// How far from the bottom to place the Clock button on the MENU (rounded-rect)
static const int kClockBtnBottomPad = 24; // was ~10; raise the button slightly
static String formatMoneyCrypto(double v) {
  String s = String(v, 5);     // 5 decimals
  return "$" + addCommas(s);
}
static int gAlarmBtnX = 0, gAlarmBtnY = 0, gAlarmBtnW = 0, gAlarmBtnH = 0;

// ---------- Stock ApiKeys ----------
String apiKey = ""; //Can Fill in code here and not use SD Card
String backupApiKey = "";

// --- CRYPTO API keys ---
String cryptoApiKey = ""; // Can Fill in code here and not use SD card
String cryptoBackupApiKey = "";

// --- Coindesk settings + fallback key (used if WIFI.txt has no CRYPTO lines) ---
const String COINDESK_MARKET        = "cadli";
const String COINDESK_API_FALLBACK  = "";


std::vector<String> newsHeadlines;
std::vector<String> items;  // stocks + crypto instruments (sorted)

int selectedIndex = 0;
unsigned long lastRefresh = 0;
String lastHeaderStr = "";
float cachedPrice = -1;
bool  cachedIsCrypto = false;


// Keep font bytes alive for the whole run so the pointer stays valid
std::vector<uint8_t> gClockFontBytes;
bool gHasClockFont = false;

// ---------- Helpers ----------
// Daily volume for stocks from Finnhub candles (D resolution)
float fetchStockDailyVolume(const String& symbol) {
  time_t now = time(nullptr);
  struct tm lt; localtime_r(&now, &lt);
  lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
  time_t dayStart = mktime(&lt);
  time_t dayEnd   = dayStart + 24*60*60 - 1;

  auto doCall = [&](const String& key)->float{
    HTTPClient http;
    String url = "https://finnhub.io/api/v1/stock/candle?symbol=" + symbol +
                 "&resolution=D&from=" + String((uint32_t)dayStart) +
                 "&to=" + String((uint32_t)dayEnd) +
                 "&token=" + key;
    http.begin(url);
    int code = http.GET();
    float vol = 0.0f;
    if (code == 200) {
      DynamicJsonDocument doc(16384);
      if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
        JsonArray V = doc["v"].as<JsonArray>();
        if (!V.isNull() && V.size() > 0) vol = V[V.size()-1].as<float>();
      }
    }
    http.end();
    return vol;
  };

  float v = 0.0f;
  if (apiKey.length()) v = doCall(apiKey);
  if (v == 0.0f && backupApiKey.length()) v = doCall(backupApiKey);
  return v;
}

// Insert commas into the integer part of a decimal string
static String addCommas(const String& s) {
  int dot = s.indexOf('.');
  int end = (dot == -1) ? s.length() : dot;

  int start = (s.startsWith("-") ? 1 : 0);
  String head = s.substring(0, start);
  String intp = s.substring(start, end);
  String frac = (dot == -1) ? "" : s.substring(dot); // includes '.'

  String out = "";
  int cnt = 0;
  for (int i = intp.length()-1; i >= 0; --i) {
    out = String(intp[i]) + out;
    if (++cnt == 3 && i > 0) { out = "," + out; cnt = 0; }
  }
  return head + out + frac;
}

// Money with 2 decimals + commas, prefixed with $
static String formatMoney(double v) {
  String s = String(v, 2);
  return "$" + addCommas(s);
}

// Whole number with commas (volume)
static String formatWhole(double v) {
  // round to nearest whole number for display
  long long n = (long long)(v + (v >= 0 ? 0.5 : -0.5));
  String s = String(n);
  return addCommas(s);
}

// Strip "-USD" from crypto labels for display
String displayLabelForSymbol(const String& s) {
  if (isCryptoSymbol(s) && endsWithCaseInsensitive(s, "-USD")) {
    return s.substring(0, s.length() - 4);
  }
  return s;
}
// Compute rectangles for "Return" and "Sync Time" buttons on the clock screen
static inline void clockActionButtonRects(int &rx, int &ry, int &rw, int &rh,
                                          int &sx, int &sy, int &sw, int &sh,
                                          int &ax, int &ay, int &aw, int &ah) {
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(DATE_TEXT_SIZE);

  const String retLbl = "Return";
  const String syncLbl = "Sync Time";
  const String alarmLbl = "Set Alarm";

  int retTextW = M5.Display.textWidth(retLbl);
  int syncTextW = M5.Display.textWidth(syncLbl);
  int alarmTextW = M5.Display.textWidth(alarmLbl);

  rw = retTextW + 60;
  sw = syncTextW + 60;
  aw = alarmTextW + 60;
  rh = sh = ah = 50;

  const int gap = 20;
  rx = 30;
  ry = sy = ay = RET_Y + 30;
  sx = rx + rw + gap;
  ax = sx + sw + gap; // Alarm button is to the right of Sync
}
// Clock button rectangle (same shape/size as other buttons), lower on screen
static inline void clockButtonRect(int &x, int &y, int &w, int &h) {
  w = kBtnW;
  h = kBtnH;
  x = 960 - kLeftMargin - w;      // right-aligned to grid margin
  y = 540 - h - kClockBtnBottomPad; 
}
static inline bool ieq(char a, char b){ return (a==b) || (a^32)==b; }
static bool endsWithCaseInsensitive(const String& s, const char* suf) {
  size_t n = s.length(), m = strlen(suf);
  if (m > n) return false;
  for (size_t i = 0; i < m; ++i) {
    if (!ieq(s[n - m + i], suf[i])) return false;
  }
  return true;
}
bool isCryptoSymbol(const String& s) {
  // Treat common crypto instrument patterns as crypto:
  //  "BTC-USD", "ETH-USD" (Coindesk style).
  int dash = s.indexOf('-');
  if (dash > 0 && (endsWithCaseInsensitive(s, "-USD"))) return true;
  return false;
}

void showMessage(const String& message) {
  M5.Display.clear();
  M5.Display.setCursor(50, 100);
  M5.Display.setTextColor(BLACK);
  M5.Display.print(message);
}

void loadCredentialsFromSD() {
  File file = SD.open("/Wifi/WIFI.txt");
  if (!file) { showMessage("WIFI.txt not found on SD!"); delay(2000); return; }

  // Read every line first (including blanks)
  std::vector<String> lines;
  while (file.available()) {
    String L = file.readStringUntil('\n');
    L.trim();                       // keep blank lines as "" (open network)
    lines.push_back(L);
  }
  file.close();

  std::vector<std::pair<String, String>> wifiList;
  String apiPrimary, apiBackup, cryptoPrimary, cryptoBackup;

  auto isKeyLine = [](const String& s){
    return s.startsWith("APIKEY:") || s.startsWith("BACKUP:") ||
           s.startsWith("CRYPTO:") || s.startsWith("CBACKUP:");
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    const String& s = lines[i];
    if (s.startsWith("APIKEY:"))   { apiPrimary    = s.substring(7);   continue; }
    if (s.startsWith("BACKUP:"))   { apiBackup     = s.substring(7);   continue; }
    if (s.startsWith("CRYPTO:"))   { cryptoPrimary = s.substring(8);   continue; }
    if (s.startsWith("CBACKUP:"))  { cryptoBackup  = s.substring(8);   continue; }

    if (s.length() == 0) continue; // skip stray blank lines not used as SSID

    // Treat as SSID, next line (if present and not a key) is the password
    String ssid = s;
    String pass = "";
    if (i + 1 < lines.size() && !isKeyLine(lines[i + 1])) {
      pass = lines[i + 1];         // may be empty => open network
      ++i;                         // consume password line
    }
    wifiList.push_back({ssid, pass});
  }

  // Try networks in order
  for (auto& creds : wifiList) {
    showMessage("Connecting to WiFi: " + creds.first + " ...");
    if (creds.second.length() == 0) WiFi.begin(creds.first.c_str());
    else                            WiFi.begin(creds.first.c_str(), creds.second.c_str());

    int retries = 20;
    while (WiFi.status() != WL_CONNECTED && retries-- > 0) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      showMessage("Connected: " + creds.first);
      delay(800);
      break;
    }
  }
  if (WiFi.status() != WL_CONNECTED) { showMessage("WiFi connection failed."); delay(1500); }

  // Assign keys
  apiKey            = apiPrimary;
  backupApiKey      = apiBackup;
  cryptoApiKey      = cryptoPrimary;
  cryptoBackupApiKey= cryptoBackup;
}
void loadItemsFromSD() {
  items.clear();
  File file = SD.open("/Wifi/STOCK.txt");
  if (file) {
    while (file.available() && items.size() < 50) { // read more than needed; we’ll cap later
      String symbol = file.readStringUntil('\n'); symbol.trim();
      if (symbol.length() > 0) items.push_back(symbol);
    }
    file.close();
  }

  // Sort case-insensitive alphabetically
  std::sort(items.begin(), items.end(), [](const String& a, const String& b){
    String A=a; String B=b;
    A.toUpperCase(); B.toUpperCase();
    return A < B;
  });

  // Cap to 29 
  if (items.size() > 29) items.resize(29);
}

// (Eastern Time with DST)
static const char* kTZ_Eastern = "EST5EDT,M3.2.0/2,M11.1.0/2";
String headerTimeString() {
  struct tm t;
  if (!getLocalTime(&t)) return "--";
  char buf[32];
  strftime(buf, sizeof(buf), "    %a %b %e %I:%M %p", &t); // Mon Aug 11 04:38 PM
  return String(buf);
}

String clockTimeString() {
  struct tm t;
  if (!getLocalTime(&t)) return "--:--";
  char buf[6];
  strftime(buf, sizeof(buf), "%I:%M", &t);             //  04:38
  return String(buf);
}

String shortDateString() {
  struct tm t;
  if (!getLocalTime(&t)) return "-- --- --";
  char buf[16];
  strftime(buf, sizeof(buf), "%a %b %e", &t);          //  Mon Aug 11
  return String(buf);
}

void fetchTime() {
  // Set timezone and NTP servers; handles DST automatically
  configTzTime(kTZ_Eastern, "pool.ntp.org", "time.nist.gov", "time.google.com");

  // Wait briefly for sync so first draw uses local time
  struct tm t{};
  for (int i = 0; i < 10; ++i) {
    if (getLocalTime(&t, 500)) break;
    delay(200);
  }

  // Debug
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  Serial.printf("Time synced: %02d:%02d (isdst=%d)\n", lt.tm_hour, lt.tm_min, lt.tm_isdst);
}

// Top bar: battery + time/date (hidden on clock screen)
void drawTopBar(bool showTimeAndDate) {
  M5.Display.fillRect(0, 0, 960, 30, WHITE);
  M5.Display.setCursor(10, 5);
  M5.Display.setTextColor(BLACK);
  M5.Display.printf("Battery: %d%%", M5.Power.getBatteryLevel());
  if (showTimeAndDate) {
    String s = headerTimeString();
    M5.Display.setCursor(300, 5);
    M5.Display.print(s);
    lastHeaderStr = s;
  } else {
    lastHeaderStr = "";
  }
}
// ---------- Networking ----------
void fetchStockDetail(const String& symbol, float& price, float& high, float& low,
                      float& open, float& prevClose, float& change) {
  HTTPClient http;
  String url = "https://finnhub.io/api/v1/quote?symbol=" + symbol + "&token=" + apiKey;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200 && backupApiKey.length() > 0) {
    http.end();
    url = "https://finnhub.io/api/v1/quote?symbol=" + symbol + "&token=" + backupApiKey;
    http.begin(url);
    httpCode = http.GET();
  }

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      price = doc["c"] | 0.0f;
      high = doc["h"] | 0.0f;
      low = doc["l"] | 0.0f;
      open = doc["o"] | 0.0f;
      prevClose = doc["pc"] | 0.0f;
      change = (prevClose != 0.0f) ? ((price - prevClose) / prevClose) * 100.0f : 0.0f;
    } else {
      price = high = low = open = prevClose = change = 0.0f;
    }
  } else {
    price = high = low = open = prevClose = change = 0.0f;
  }
  http.end();
}

// Coindesk crypto detail (VALUE, CURRENT_DAY_*)
void fetchCryptoDetail(const String& instrument, float& price, float& high, float& low,
                       float& open, float& prevClose, float& changePct, float& volume) {
  HTTPClient http;

  auto buildUrl = [&](const String& key){
    return "https://data-api.coindesk.com/index/cc/v1/latest/tick?market=" + COINDESK_MARKET +
           "&instruments=" + instrument + "&apply_mapping=true&api_key=" + key;
  };

  // pick a starting key: file primary, else fallback
  String keyToUse = cryptoApiKey.length() ? cryptoApiKey : COINDESK_API_FALLBACK;

  String url = buildUrl(keyToUse);
  http.begin(url);
  int httpCode = http.GET();

  // if primary failed AND we have a backup from WIFI.txt, try it
  if (httpCode != 200 && cryptoBackupApiKey.length() > 0) {
    http.end();
    url = buildUrl(cryptoBackupApiKey);
    http.begin(url);
    httpCode = http.GET();
  }

  price = high = low = open = prevClose = changePct = volume = 0.0f;

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonVariant rec = doc["Data"][instrument];
      if (!rec.isNull()) {
        price     = rec["VALUE"] | 0.000f;
        high      = rec["CURRENT_DAY_HIGH"] | 0.000f;
        low       = rec["CURRENT_DAY_LOW"] | 0.000f;
        open      = rec["CURRENT_DAY_OPEN"] | 0.000f;
        prevClose = open; // no separate prev close in this feed
        volume    = rec["CURRENT_DAY_VOLUME"] | 0.0f;

        if (rec.containsKey("CURRENT_DAY_CHANGE_PERCENTAGE")) {
          changePct = rec["CURRENT_DAY_CHANGE_PERCENTAGE"].as<float>();
          // If API returns a fraction instead of %, normalize:
          if (fabs(changePct) < 1.0f) changePct *= 100.0f;
        } else {
          changePct = (open != 0.0f) ? ((price - open) / open) * 100.0f : 0.0f;
        }
      }
    }
  }
  http.end();
}

void fetchNewsHeadlines(const String& symbol) {
  newsHeadlines.clear();
  HTTPClient http;

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char dateBuf[11];
  strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", t);
  String today = String(dateBuf);

  String url = "https://finnhub.io/api/v1/company-news?symbol=" + symbol + "&from=" + today + "&to=" + today + "&token=" + apiKey;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200 && backupApiKey.length() > 0) {
    http.end();
    url = "https://finnhub.io/api/v1/company-news?symbol=" + symbol + "&from=" + today + "&to=" + today + "&token=" + backupApiKey;
    http.begin(url);
    httpCode = http.GET();
  }

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      int count = 0;
      for (JsonObject article : doc.as<JsonArray>()) {
        if (article.containsKey("headline") && count < 5) {
          newsHeadlines.push_back(article["headline"].as<String>());
          count++;
        }
      }
    }
  }
  http.end();
}

String fetchCompanyName(const String& symbol) {
  if (isCryptoSymbol(symbol)) {
    // Show crypto without "-USD"
    return displayLabelForSymbol(symbol);
  }

  HTTPClient http;
  String url = "https://finnhub.io/api/v1/stock/profile2?symbol=" + symbol + "&token=" + apiKey;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200 && backupApiKey.length() > 0) {
    http.end();
    url = "https://finnhub.io/api/v1/stock/profile2?symbol=" + symbol + "&token=" + backupApiKey;
    http.begin(url);
    httpCode = http.GET();
  }

  String name = symbol;  // fallback
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      String n = doc["name"] | "";
      if (n.length()) name = n;
    }
  }
  http.end();
  return name;
}

// -------- Drawing --------
static inline void buttonRectForIndex(size_t idx, int &x, int &y, int &w, int &h) {
  int maxRows = (540 - kTopMargin - 100) / (kBtnH + kGapY);
  if (maxRows < 1) maxRows = 1;
  int row = idx % maxRows;
  int col = idx / maxRows;
  if (col >= kCols) col = kCols - 1;
  x = kLeftMargin + col * (kBtnW + kGapX);
  y = kTopMargin + row * (kBtnH + kGapY);
  w = kBtnW; h = kBtnH;
}

void drawMenu() {
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(1);

  currentView = VIEW_MENU;
  M5.Display.clear();
  drawTopBar(true);

  // Grid of items
  for (size_t i = 0; i < items.size(); ++i) {
    int x, y, w, h; buttonRectForIndex(i, x, y, w, h);
    bool selected = (int)i == selectedIndex;

    M5.Display.fillRoundRect(x, y, w, h, 25, selected ? BLACK : LIGHTGREY);
    M5.Display.setTextColor(selected ? WHITE : BLACK);

    //(crypto without "-USD")
    String label = displayLabelForSymbol(items[i]);
    int textW = M5.Display.textWidth(label);
    int textH = M5.Display.fontHeight();
    int textX = x + (w - textW) / 2;
    int textY = y + (h + textH) / 3 - 9;
    M5.Display.setCursor(textX, textY);
    M5.Display.print(label);
  }

  // Footer text
  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(10, 500);
  M5.Display.print("Touch stock/crypto to view.\n                                            Programmed By: Javicar31");

  // Clock button
  int cx, cy, cw, ch; clockButtonRect(cx, cy, cw, ch);
  M5.Display.fillRoundRect(cx, cy, cw, ch, 25, LIGHTGREY);
  M5.Display.drawRoundRect(cx, cy, cw, ch, 25, BLACK);

  String clk = "Clock";
  int tw = M5.Display.textWidth(clk);
  int th = M5.Display.fontHeight();
  int tX = cx + (cw - tw) / 2;
  int tY = cy + (ch + th) / 3 - 9;
  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(tX, tY);
  M5.Display.print(clk);
}

void drawDetail(const String &symbol) {
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(1);

  currentView = VIEW_DETAIL;
  M5.Display.clear();
  drawTopBar(true);

  bool isCrypto = isCryptoSymbol(symbol);

  float price, high, low, open, prevClose, change, volume;
  if (isCrypto) {
    fetchCryptoDetail(symbol, price, high, low, open, prevClose, change, volume);
  } else {
    fetchStockDetail(symbol, price, high, low, open, prevClose, change);
     volume = fetchStockDailyVolume(symbol);
  }
  cachedPrice = price;
  cachedIsCrypto = isCrypto;

  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(30, 50);
  String displayName = fetchCompanyName(symbol);
  M5.Display.printf("%s (%s)", displayName.c_str(), symbol.c_str());

auto money = [&](double x)->String { return isCrypto ? formatMoneyCrypto(x) : formatMoney(x); };

M5.Display.setCursor(30, 100); M5.Display.printf("Price       : %s", money(price).c_str());
M5.Display.setCursor(30, 130); M5.Display.printf("High        : %s",  money(high).c_str());
M5.Display.setCursor(30, 160); M5.Display.printf("Low         : %s",   money(low).c_str());
M5.Display.setCursor(30, 190); M5.Display.printf("Open        : %s",   money(open).c_str());
M5.Display.setCursor(30, 220); M5.Display.printf("Prev Close  : %s",   money(prevClose).c_str());
M5.Display.setCursor(30, 250); M5.Display.printf("Change %%    : %.4f%%", isCrypto ? change : change); // keep % precision if you like
M5.Display.setCursor(30, 280); M5.Display.printf("Volume      : %s", formatWhole(volume).c_str());


  // Right-side block: show news for stocks only
if (!isCrypto) {
  // one full line below the name line (name is drawn at y=50)
  const int nameY = 50;
  const int lineH = M5.Display.fontHeight();      // current font height
  const int newsX = 390;
  const int newsY = nameY + lineH + 10;           //  one line below 
  const int newsWidth = 960 - newsX - 10;
  const int lineHeight = 20;

  // Title
  M5.Display.setCursor(newsX, newsY);
  M5.Display.print("Latest News:");

  // leave a blank line before first headline
  int cursorY = newsY + lineHeight;

  for (size_t n = 0; n < 2 && n < newsHeadlines.size(); ++n) {
    String headline = newsHeadlines[n];
    String line = "", word = "";
    for (size_t i = 0; i < headline.length(); ++i) {
      char c = headline[i];
      if (c == ' ' || i == headline.length() - 1) {
        if (i == headline.length() - 1 && c != ' ') word += c;
        String testLine = line + (line.length() ? " " : "") + word;
        if (M5.Display.textWidth(testLine) > newsWidth) {
          M5.Display.setCursor(newsX, cursorY); M5.Display.print(line);
          cursorY += lineHeight; line = word;
        } else line = testLine;
        word = "";
      } else word += c;
    }
    if (line.length()) { M5.Display.setCursor(newsX, cursorY); M5.Display.print(line); cursorY += lineHeight; }
    cursorY += 15;
  }
}

  // Return button
  String label = "Return";
  int textWidth = M5.Display.textWidth(label);
  int buttonWidth = textWidth + 60;
  int xBtn = 30, yBtn = RET_Y, buttonHeight = 50;
  int textX = xBtn + (buttonWidth - textWidth) / 2;
  int textY = yBtn + (buttonHeight + M5.Display.fontHeight()) / 3 - 9;

  M5.Display.fillRoundRect(xBtn, yBtn, buttonWidth, buttonHeight, 25, BLACK);
  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(textX, textY);
  M5.Display.print(label);

  lastRefresh = millis();
}

void drawClockFace(bool firstDraw) {
  // Entering clock view: draw header (battery only)
  if (firstDraw) {
    drawTopBar(false);
  }

  // Draw static elements (date + action buttons) ONLY ONCE
  if (!s_clockStaticDrawn) {
    M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
    M5.Display.setTextSize(DATE_TEXT_SIZE);

    // ---- DATE (bottom-right) ----
    String d = shortDateString();        // "Mon Aug 11"
    int dw = M5.Display.textWidth(d);
    int dh = M5.Display.fontHeight();
    int dx = 960 - dw - 14;
    int dy = 440 - dh - 12;

    M5.Display.fillRect(dx - 8, dy - 6, dw + 16, dh + 14, WHITE);
    M5.Display.setCursor(dx, dy + dh / 3);
    M5.Display.setTextColor(BLACK);
    M5.Display.print(d);
    M5.Display.display(dx - 8, dy - 6, dw + 16, dh + 14);

  // ---- Action buttons: Return + Sync Time + Set Alarm ----
    int rx, ry, rw, rh, sx, sy, sw, sh, ax, ay, aw, ah;
    clockActionButtonRects(rx, ry, rw, rh, sx, sy, sw, sh, ax, ay, aw, ah);

    // Return
    M5.Display.fillRect(rx - 4, ry - 4, rw + 8, rh + 8, WHITE);
    M5.Display.fillRoundRect(rx, ry, rw, rh, 25, BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(rx + (rw - M5.Display.textWidth("Return")) / 2,
                         ry + (rh + M5.Display.fontHeight()) / 5 - 9);
    M5.Display.print("Return");
    M5.Display.display(rx - 4, ry - 4, rw + 8, rh + 8);

    // Sync Time
    M5.Display.fillRect(sx - 4, sy - 4, sw + 8, sh + 8, WHITE);
    M5.Display.fillRoundRect(sx, sy, sw, sh, 25, BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(sx + (sw - M5.Display.textWidth("Sync Time")) / 2,
                         sy + (sh + M5.Display.fontHeight()) / 5 - 9);
    M5.Display.print("Sync Time");
    M5.Display.display(sx - 4, sy - 4, sw + 8, sh + 8);

    // Set Alarm
    M5.Display.fillRect(ax - 4, ay - 4, aw + 8, ah + 8, WHITE);
    M5.Display.fillRoundRect(ax, ay, aw, ah, 25, BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(ax + (aw - M5.Display.textWidth("Set Alarm")) / 2,
                     ay + (ah + M5.Display.fontHeight()) / 5 - 9);
    M5.Display.print("Set Alarm");
    M5.Display.display(ax - 4, ay - 4, aw + 8, ah + 8);

    s_clockStaticDrawn = true;
    }

    // Ensure VLW is active ONCE (never reload every minute)
    if (gHasClockFont && !s_clockVlwActive) {
    M5.Display.loadFont(gClockFontBytes.data());
    M5.Display.setTextSize(2);        // scaling small for VLW
    s_clockVlwActive = true;
    }

    // If no VLW, use built-in big font as fallback
    if (!gHasClockFont) {
    M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
    M5.Display.setTextSize(CLOCK_TEXT_SIZE);
    }

 // ---- TIME redraw (partial refresh) ----
  const int cx  = 960 / 2;
  const int cy  = 540 / 2 - 6; 
  const char* REF = "88:88";
  const int pad = 16;

  auto prevMode = M5.Display.getEpdMode();
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fast);

  int w = M5.Display.textWidth(REF);
  int h = M5.Display.fontHeight();
  int boxX = cx - w / 2 - pad;
  int boxY = cy - h / 2 - pad;
  int boxW = w + pad * 2;
  int boxH = h + pad * 2;

  // Erase + flush just the time area
  M5.Display.fillRect(boxX, boxY, boxW, boxH, WHITE);
  M5.Display.display(boxX, boxY, boxW, boxH);

  String nowStr = clockTimeString();   // "HH:MM"
  M5.Display.setTextColor(BLACK);
  M5.Display.setTextDatum(m5gfx::textdatum_t::middle_center);
  M5.Display.drawString(nowStr, cx, cy);
  M5.Display.setTextDatum(m5gfx::textdatum_t::top_left);
  M5.Display.display(boxX, boxY, boxW, boxH);

  M5.Display.setEpdMode(prevMode);
}

void drawClockScreen(bool firstDraw) {
  if (firstDraw) {
    currentView = VIEW_CLOCK;
    M5.Display.clear();
    s_clockStaticDrawn = false;
    s_clockVlwActive   = false;
  }
  drawClockFace(firstDraw);
}

void updatePriceIfNeeded(const String& symbol, bool isCrypto) {
  float price, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6;
  if (isCrypto) {
    fetchCryptoDetail(symbol, price, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6);
  } else {
    fetchStockDetail(symbol, price, dummy1, dummy2, dummy3, dummy4, dummy5);
  }
  // Replace the body of updatePriceIfNeeded's redraw:
  if (price != cachedPrice) {
  cachedPrice = price;
  M5.Display.fillRect(30, 100, 340, 24, WHITE);
  M5.Display.setCursor(30, 100);
  M5.Display.setTextColor(BLACK);
  String line = "Price       : " + (isCrypto ? formatMoneyCrypto(price) : formatMoney(price));
  M5.Display.print(line);
}

}
// -------- Touch ----------
void handleTouch() {
  auto detail = M5.Touch.getDetail();
  if (!detail.isPressed()) return;
  int x = detail.x, y = detail.y;

  if (currentView == VIEW_CLOCK) {
    int rx, ry, rw, rh, sx, sy, sw, sh, ax, ay, aw, ah;
    clockActionButtonRects(rx, ry, rw, rh, sx, sy, sw, sh, ax, ay, aw, ah);

    // Return button
    if (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh) {
      delay(250);
      s_clockStaticDrawn = false;
      s_clockVlwActive = false;
      drawMenu();
      return;
    }

    // Sync Time button
    if (x >= sx && x <= sx + sw && y >= sy && y <= sy + sh) {
      delay(150);
      fetchTime();
      s_clockStaticDrawn = false;
      drawClockScreen(true);
      return;
    }

    // Set Alarm button
    if (x >= ax && x <= ax + aw && y >= ay && y <= ay + ah) {
      delay(200);
      drawAlarmScreen();
      return;
    }
  } else if (currentView == VIEW_ALARM_SET) { 
    // "+H" button
    if (x >= 30 && x <= 80 && y >= 190 && y <= 240) { //was 200 and 250
      alarmHour = (alarmHour + 1) % 24;
      drawAlarmScreen();
      return;
    }
    // "-H" button
    //x indicates the location on the x axis, x30-x80 creates the "touch" on the left side of the screen
    if (x >= 30 && x <= 80 && y >= 270 && y <= 340) { // 
      alarmHour = (alarmHour - 1 + 24) % 24;
      drawAlarmScreen();
      return;
    }
    // "+M" button
    if (x >= 960 - 80 && x <= 960 - 30 && y >= 190 && y <= 240) {
      alarmMinute = (alarmMinute + 1) % 60;
      drawAlarmScreen();
      return;
    }
    // "-M" button
    if (x >= 960 - 80 && x <= 960 - 30 && y >= 270 && y <= 340) {
      alarmMinute = (alarmMinute - 1 + 60) % 60;
      drawAlarmScreen();
      return;
    }
    // Toggle Alarm ON/OFF button
    if (x >= gAlarmBtnX && x <= gAlarmBtnX + gAlarmBtnW &&
    y >= gAlarmBtnY && y <= gAlarmBtnY + gAlarmBtnH) {
     alarmEnabled = !alarmEnabled;
      drawAlarmScreen();
       return;
    }

    // "Save" button
    if (x >= 960 / 2 - 100 && x <= 960 / 2 + 100 && y >= 420 && y <= 470) {
      delay(200);
      drawClockScreen(true);
      return;
    }
  } else if (currentView == VIEW_DETAIL) { 
    // Shared "Return" for detail view
    String label = "Return";
    int textWidth = M5.Display.textWidth(label);
    int buttonWidth = textWidth + 60;
    int buttonX = 30, buttonY = RET_Y;

    if (x >= buttonX && x <= buttonX + buttonWidth &&
        y >= buttonY && y <= buttonY + 50) {
      delay(250);
      drawMenu();
      return;
    }
  } else if (currentView == VIEW_MENU) { 
    // Clock button 
    int cx, cy, cw, ch; clockButtonRect(cx, cy, cw, ch);
    if (x >= cx && x <= cx + cw && y >= cy && y <= cy + ch) {
      delay(200);
      drawClockScreen(true);
      return;
    }
    // Item buttons
    for (size_t i = 0; i < items.size(); ++i) {
      int bx, by, bw, bh; buttonRectForIndex(i, bx, by, bw, bh);
      if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
        selectedIndex = (int)i;
        String sel = items[selectedIndex];
        if (isCryptoSymbol(sel)) newsHeadlines.clear();
        else fetchNewsHeadlines(sel);
        delay(200);
        drawDetail(sel);
        return;
      }
    }
  }
}
void drawAlarmScreen() {
  currentView = VIEW_ALARM_SET;
  M5.Display.clear();
  drawTopBar(false);

  // ---- Title ----
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(250, 150);
  M5.Display.print("Set Alarm Time");

  // ---- Time ----
  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", alarmHour, alarmMinute);
  M5.Display.setTextSize(5);                     
  int timeW = M5.Display.textWidth(timeBuf);
  int timeX = (960 - timeW) / 2;
  M5.Display.setCursor(timeX, 200);
  M5.Display.print(timeBuf);

    // ---- Alarm ON/OFF  ----
    String enabledLbl = alarmEnabled ? "Alarm ON" : "Alarm OFF";

    M5.Display.setTextSize(2);
    M5.Display.setFont(&fonts::FreeMonoBold12pt7b);

    const int padX = 24;
    const int padY = 10;
    int labelW = M5.Display.textWidth(enabledLbl);
    int labelH = M5.Display.fontHeight();

    // Compute and store the rect for reuse in handleTouch()
    gAlarmBtnW = labelW + padX * 2;
    gAlarmBtnH = labelH + padY * 2;
    gAlarmBtnX = (960 - gAlarmBtnW) / 2;
    gAlarmBtnY = 320;

    uint16_t pillColor = alarmEnabled ? BLACK : LIGHTGREY;
    uint16_t textColor = alarmEnabled ? WHITE : BLACK;

    // Draw 
    M5.Display.fillRoundRect(gAlarmBtnX, gAlarmBtnY, gAlarmBtnW, gAlarmBtnH, gAlarmBtnH/2, pillColor);

    // Center the text precisely
    M5.Display.setTextColor(textColor);
    auto oldDatum = M5.Display.getTextDatum();  // if available; if not, skip saving
    M5.Display.setTextDatum(MC_DATUM);          // middle-center
    M5.Display.drawString(enabledLbl, gAlarmBtnX + gAlarmBtnW/2, gAlarmBtnY + gAlarmBtnH/2);
    M5.Display.setTextDatum(TL_DATUM);          // restore top-left 

    // ---- Hour/Minute buttons ----
    drawButton(30, 190, 50, 50, "+H", BLACK, WHITE);
    drawButton(30, 270, 50, 50, "-H", BLACK, WHITE);
    drawButton(960 - 80, 190, 50, 50, "+M", BLACK, WHITE);
    drawButton(960 - 80, 270, 50, 50, "-M", BLACK, WHITE);

    // ---- Save ----
    drawButton(960 / 2 - 100, 430, 200, 50, "Save", BLACK, WHITE);
}

//  function to draw buttons easily
void drawButton(int x, int y, int w, int h, const String& label, int bgColor, int textColor) {
  M5.Display.fillRoundRect(x, y, w, h, 25, bgColor);
  M5.Display.setTextColor(textColor);
  int textW = M5.Display.textWidth(label);
  M5.Display.setCursor(x + (w - textW) / 2, y + (h + M5.Display.fontHeight()) / 4 - 9);
  M5.Display.print(label);
}

// Read a VLW font into RAM and select it
bool loadVLWIntoRAM(fs::FS &fs, const char* path) {
  File f = fs.open(path, "r");
  if (!f) {
    Serial.printf("Font open failed: %s\n", path);
    return false;
  }
  size_t sz = f.size();
  if (sz == 0) {
    Serial.printf("Font empty: %s\n", path);
    f.close();
    return false;
  }
  gClockFontBytes.resize(sz);
  size_t n = f.read(gClockFontBytes.data(), sz);
  f.close();
  if (n != sz) {
    Serial.printf("Font read short: %u / %u\n", (unsigned)n, (unsigned)sz);
    gClockFontBytes.clear();
    return false;
  }
  bool ok = M5.Display.loadFont(gClockFontBytes.data()); // memory-based load
  Serial.printf("loadFont(memory) -> %d (size=%u)\n", ok, (unsigned)sz);
  return ok;
}

// Load clock font only from SD
bool tryLoadClockFontFromSD() {
  if (SD.exists("/font/ftime.vlw")) {
    Serial.println("Trying SD:/font/ftime.vlw");
    if (loadVLWIntoRAM(SD, "/font/ftime.vlw")) return true;
  } else {
    Serial.println("SD:/font/ftime.vlw not found");
  }
  return false;
}
void playAlarmTone() {
  M5.Speaker.setVolume(200);
  for (int i = 0; i < 5; ++i) { // Play a tone for a few seconds
    M5.Speaker.tone(1000, 500); // Frequency, duration
    delay(500);
    M5.Speaker.tone(1200, 500);
    delay(500);
  }
  M5.Speaker.tone(0); // Turn off the tone
}
// -------- Setup / Loop --------
void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextColor(BLACK);

  // SD SPI
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  showMessage("Mounting SD card...");
  if (!SD.begin(SD_CS)) { showMessage("SD mount failed!"); delay(2500); return; }

  loadCredentialsFromSD();
  loadItemsFromSD();
  fetchTime();

  drawMenu();

  // Attempt to load a VLW for the clock from SD only
  gHasClockFont = tryLoadClockFontFromSD();

  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextSize(1);

  Serial.printf("gHasClockFont=%d\n", gHasClockFont);
}
void loop() {
  M5.update();
  handleTouch();

  if (currentView != VIEW_CLOCK) {
    String nowHeader = headerTimeString();
    if (nowHeader != lastHeaderStr) drawTopBar(true);
  } else {
    static String lastClock = "";
    String nowClock = clockTimeString();
    if (nowClock != lastClock) {
      drawClockScreen(false);   // partial time/date redraw
      lastClock = nowClock;
    }
  }

  if (currentView == VIEW_DETAIL && millis() - lastRefresh > 30000) {
    String sym = items[selectedIndex];
    updatePriceIfNeeded(sym, isCryptoSymbol(sym));
    lastRefresh = millis();
  }
    // Alarm check
  if (alarmEnabled && currentView != VIEW_ALARM_SET) {
    struct tm t;
    if (getLocalTime(&t)) {
      if (t.tm_hour == alarmHour && t.tm_min == alarmMinute && t.tm_sec == 0) {
        Serial.println("Alarm triggered!");
        playAlarmTone();
        alarmEnabled = false; // Disable alarm after it has been triggered
        drawClockScreen(false); //Dont Force redraw to show "Alarm OFF" else font will load for battery%
  delay(80);
    }
    }
  }
}
    

