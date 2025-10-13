
#define APPSTATE_IMPLEMENTATION   

#include "Config.h"
#include "AppState.h"
#include "TimeUtil.h"
#include "Marquee.h"
#include "Calendar.h"
#include "Weather.h"
#include "Drawing.h"
#include "Secrets.h"
#include "WiFiUtil.h"
#include "HIDApp.h"

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
  lastY = t.tm_year + 1900;
  lastM = t.tm_mon + 1;
  lastD = t.tm_mday;

  Serial.println("Drawing display...");
  drawAll();
  Serial.println("Setup complete!");

  // Start USB HID 
  hid_begin();
}

void loop() {
  // If HID app is active, let it handle UI for this frame
  if (hid_tick()) return;
// If HID just exited, clear+redraw calendar once (no fetch)
if (hid_justExited()) {
  clearMarquees();
  drawAll();
  return;
}

  struct tm t{};
  bool haveTime = readLocal(t);

  M5.update();
  auto td = M5.Touch.getDetail();

  // Tap-to-open HID button in the header
  bool justTapped = td.wasPressed();   // single-edge tap detection
  const int hidBtnW = 80, hidBtnH = 30;
  const int hidBtnX = SCREEN_W - 90;
  const int hidBtnY = HEADER_H - hidBtnH - 6;
  if (justTapped &&
      td.x >= hidBtnX && td.x < hidBtnX + hidBtnW &&
      td.y >= hidBtnY && td.y < hidBtnY + hidBtnH) {
    hid_setActive(true);
    return; // next loop call, hid_tick() will draw HID UI
  }

  // Marquee touch activation
  static unsigned long touchExpire = 0;
  if (td.isPressed() || td.wasPressed()) {
    g_marqueeTouchActive = true;
    touchExpire = millis() + 1200;
  } else if (millis() > touchExpire) {
    g_marqueeTouchActive = false;
  }

  updateMarquees();

  // Header clock: redraw once per minute
  if (haveTime && t.tm_min != lastMinute) {
    lastMinute = t.tm_min;
    M5.Display.fillRect(0, 0, SCREEN_W, HEADER_H, SUBTLE);
    drawHeader(t);   // NOTE: drawHeader() should include the HID button drawing
  }

  // Calendar: refresh at midnight
  if (haveTime) {
    int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
    if (y != lastY || m != lastM || d != lastD) {
      lastY = y; lastM = m; lastD = d;
      if (WiFi.status() == WL_CONNECTED) fetchCalendar();
      drawAll();
    }
  }

  // Weather: refresh every 30 minutes
  if (millis() - lastWxMS > WX_PERIOD) {
    if (WiFi.status() == WL_CONNECTED) fetchWeather();
    lastWxMS = millis();
    drawAll();
  }

  delay(30);
}
