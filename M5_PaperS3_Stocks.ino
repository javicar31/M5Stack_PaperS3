#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>

#define SD_CS 47
#define SD_SCK 39
#define SD_MOSI 38
#define SD_MISO 40


String apiKey = "";
String backupApiKey = "";
const int buttonHeight = 50;
std::vector<String> newsHeadlines;
std::vector<String> stocks;
int selectedStock = 0;
bool inDetailView = false;
unsigned long lastStockRefresh = 0;
String lastTimeStr = "";
float cachedPrice = -1;

void showMessage(const String& message) {
  M5.Display.clear();
  M5.Display.setCursor(50, 100);
  M5.Display.setTextColor(BLACK);
  M5.Display.print(message);
}

void loadCredentialsFromSD() {
  File file = SD.open("/Wifi/WIFI.txt");
  if (!file) {
    showMessage("WIFI.txt not found on SD!");
    delay(2000);
    return;
  }

  std::vector<std::pair<String, String>> wifiList;
  String apiPrimary, apiBackup;

  while (file.available()) {
    String ssid = file.readStringUntil('\n'); ssid.trim();
    if (!file.available()) break;
    String pass = file.readStringUntil('\n'); pass.trim();

    if (ssid.startsWith("APIKEY:")) {
      apiPrimary = ssid.substring(7);
      if (file.available()) {
        String maybeBackup = file.readStringUntil('\n'); maybeBackup.trim();
        if (maybeBackup.startsWith("BACKUP:")) {
          apiBackup = maybeBackup.substring(7);
        }
      }
      break;
    }

    wifiList.push_back({ssid, pass});
  }
  file.close();

  for (auto& creds : wifiList) {
    showMessage("Connecting to WiFi: " + creds.first + " ...");
    if (creds.second.length() == 0) {
      WiFi.begin(creds.first.c_str());
    } else {
      WiFi.begin(creds.first.c_str(), creds.second.c_str());
    }

    int retries = 20;
    while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
      showMessage("Connected to: " + creds.first);
      delay(1000);
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    showMessage("WiFi connection failed.");
    delay(2000);
  }

  apiKey = apiPrimary;
  backupApiKey = apiBackup;
}

void loadStocksFromSD() {
  stocks.clear();
  File file = SD.open("/Wifi/STOCK.txt");
  if (file) {
    while (file.available()) {
      String symbol = file.readStringUntil('\n');
      symbol.trim();
      if (symbol.length() > 0) {
        stocks.push_back(symbol);
      }
    }
    file.close();
  }
}

void fetchTime() {
  configTime(-14400, 0, "pool.ntp.org", "time.nist.gov");  // UTC-4 Eastern
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";
  char buf[30];
  strftime(buf, sizeof(buf), "%a %b %e %I:%M %p", &timeinfo);
  return String(buf);
}

void updateHeader() {
  int battery = M5.Power.getBatteryLevel();
  String timeStr = getFormattedTime();
  lastTimeStr = timeStr;

  M5.Display.fillRect(0, 0, 540, 30, WHITE);
  M5.Display.setCursor(10, 5);
  M5.Display.setTextColor(BLACK);
  M5.Display.printf("Battery: %d%%", battery);
  M5.Display.setCursor(300, 5);
  M5.Display.print(timeStr);
}

void drawMenu() {
  inDetailView = false;
  M5.Display.clear();
  updateHeader();

  int buttonHeight = 50;
  int rowSpacing = 20;
  int colSpacing = 30;
  int topMargin = 60;
  int leftMargin = 30;
  int screenHeight = 540;
  int screenWidth = 960;
  int usableHeight = screenHeight - topMargin - 100;  

  int maxRows = usableHeight / (buttonHeight + rowSpacing);
  int col = 0;
  int row = 0;

  for (size_t i = 0; i < stocks.size(); ++i) {
    if (row >= maxRows) {
      row = 0;
      col++;
    }

    int y = topMargin + row * (buttonHeight + rowSpacing);
    int x = leftMargin + col * 250;  

    bool selected = (i == selectedStock);
    String label = stocks[i];
    int textWidth = M5.Display.textWidth(label);
    int buttonWidth = textWidth + 60;
    int textHeight = M5.Display.fontHeight();
    int textX = x + (buttonWidth - textWidth) / 2;
    int textY = y + (buttonHeight + textHeight) / 3 - 9;

    M5.Display.fillRoundRect(x, y, buttonWidth, buttonHeight, 25, selected ? BLACK : LIGHTGREY);
    M5.Display.setTextColor(selected ? WHITE : BLACK);
    M5.Display.setCursor(textX, textY);
    M5.Display.print(label);

    row++;
  }

  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(10, 500);  
  M5.Display.print("Touch stock to view.\n                                            Programmed By: Javicar31");
}

void fetchStockDetail(const String& symbol, float& price, float& high, float& low,
                      float& open, float& prevClose, float& change, float& volume) {
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
    deserializeJson(doc, payload);
    price = doc["c"];
    high = doc["h"];
    low = doc["l"];
    open = doc["o"];
    prevClose = doc["pc"];
    volume = doc["v"];
    if (prevClose != 0) change = ((price - prevClose) / prevClose) * 100.0;
  } else {
    price = high = low = open = prevClose = change = volume = 0;
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
    deserializeJson(doc, payload);

    int count = 0;
    for (JsonObject article : doc.as<JsonArray>()) {
      if (article.containsKey("headline") && count < 5) {
        newsHeadlines.push_back(article["headline"].as<String>());
        count++;
      }
    }
  }

  http.end();
}
String fetchCompanyName(const String& symbol) {
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

  String name = symbol;  // fallback to symbol
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    if (doc.containsKey("name") && doc["name"].as<String>() != "") {
      name = doc["name"].as<String>();
    }
  }

  http.end();
  return name;
}

void drawDetail(const String &symbol) {
  inDetailView = true;
  M5.Display.clear();
  updateHeader();

  float price, high, low, open, prevClose, change, volume;
  fetchStockDetail(symbol, price, high, low, open, prevClose, change, volume);
  cachedPrice = price;

  M5.Display.setTextColor(BLACK);
  M5.Display.setCursor(30, 50);
  String companyName = fetchCompanyName(symbol);
M5.Display.printf("%s (%s)", companyName.c_str(), symbol.c_str());

  M5.Display.setCursor(30, 100); M5.Display.printf("Price       : $%.2f", price);
  M5.Display.setCursor(30, 130); M5.Display.printf("High        : $%.2f", high);
  M5.Display.setCursor(30, 160); M5.Display.printf("Low         : $%.2f", low);
  M5.Display.setCursor(30, 190); M5.Display.printf("Open        : $%.2f", open);
  M5.Display.setCursor(30, 220); M5.Display.printf("Prev Close  : $%.2f", prevClose);
  M5.Display.setCursor(30, 250); M5.Display.printf("Change %%    : %.2f%%", change);
  M5.Display.setCursor(30, 280); M5.Display.printf("Volume      : %.0f", volume);

  int newsX = 390;
  int newsY = 50;
  int newsWidth = 960 - newsX - 10;
  int lineHeight = 20;

  M5.Display.setCursor(newsX, newsY);
  M5.Display.setTextColor(BLACK);
  M5.Display.print("Latest News:\n");
  int cursorY = newsY + lineHeight;

  for (size_t n = 0; n < 2 && n < newsHeadlines.size(); ++n) {
    String headline = newsHeadlines[n];
    String line = "", word = "";

    for (size_t i = 0; i < headline.length(); ++i) {
      char c = headline[i];
      if (c == ' ' || i == headline.length() - 1) {
        if (i == headline.length() - 1 && c != ' ') word += c;
        String testLine = line + (line.length() > 0 ? " " : "") + word;
        if (M5.Display.textWidth(testLine) > newsWidth) {
          M5.Display.setCursor(newsX, cursorY);
          M5.Display.print(line);
          cursorY += lineHeight;
          line = word;
        } else {
          line = testLine;
        }
        word = "";
      } else {
        word += c;
      }
    }

    if (line.length() > 0) {
      M5.Display.setCursor(newsX, cursorY);
      M5.Display.print(line);
      cursorY += lineHeight;
    }

    cursorY += 15;
  }

  // Return button
  String label = "Return";
  int textWidth = M5.Display.textWidth(label);
  int buttonWidth = textWidth + 60;
  int xBtn = 30;
  int yBtn = 420;
  int buttonHeight = 50;
  int textX = xBtn + (buttonWidth - textWidth) / 2;
  int textY = yBtn + (buttonHeight + M5.Display.fontHeight()) / 3 - 9;

  M5.Display.fillRoundRect(xBtn, yBtn, buttonWidth, buttonHeight, 25, BLACK);
  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(textX, textY);
  M5.Display.print(label);

  lastStockRefresh = millis();
}

void updateStockPriceIfNeeded(const String& symbol) {
  float price, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6;
  fetchStockDetail(symbol, price, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6);

  if (price != cachedPrice) {
    cachedPrice = price;
    M5.Display.fillRect(30, 100, 300, 20, WHITE);
    M5.Display.setCursor(30, 100);
    M5.Display.setTextColor(BLACK);
    M5.Display.printf("Price       : $%.2f", price);
  }
}

void handleTouch() {
  auto detail = M5.Touch.getDetail();
  int x = detail.x;
  int y = detail.y;

  if (inDetailView) {
    String label = "Return";
    int textWidth = M5.Display.textWidth(label);
    int buttonWidth = textWidth + 60;
    int buttonX = 30;
    int buttonY = 420;

    if (x >= buttonX && x <= buttonX + buttonWidth &&
        y >= buttonY && y <= buttonY + buttonHeight) {
      delay(300);  
      drawMenu();
      return;
    }
  } else {
    // --- MULTI-COLUMN BUTTON SUPPORT ---
    int rowSpacing = 20;
    int colSpacing = 30;
    int topMargin = 60;
    int leftMargin = 30;
    int maxRows = (540 - topMargin - 100) / (buttonHeight + rowSpacing);
    int col = 0;
    int row = 0;

    for (size_t i = 0; i < stocks.size(); ++i) {
      if (row >= maxRows) {
        row = 0;
        col++;
      }

      int btnX = leftMargin + col * 250;
      int btnY = topMargin + row * (buttonHeight + rowSpacing);
      String label = stocks[i];
      int textWidth = M5.Display.textWidth(label);
      int buttonWidth = textWidth + 60;

      if (x >= btnX && x <= btnX + buttonWidth &&
          y >= btnY && y <= btnY + buttonHeight) {
        selectedStock = i;
        fetchNewsHeadlines(stocks[selectedStock]);
        delay(300);  // debounce
        drawDetail(stocks[selectedStock]);
        return;
      }
      row++;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeMonoBold12pt7b);
  M5.Display.setTextColor(BLACK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  showMessage("Mounting SD card...");
  if (!SD.begin(SD_CS)) {
    showMessage("SD mount failed!");
    delay(3000);
    return;
  }

  loadCredentialsFromSD();  // Load Wi-Fi + API keys
  loadStocksFromSD();       // Load stock list
  fetchTime();
  drawMenu();
}

void loop() {
  M5.update();
  handleTouch();

  String currentTimeStr = getFormattedTime();
  if (currentTimeStr != lastTimeStr) {
    updateHeader();
  }

  if (inDetailView && millis() - lastStockRefresh > 30000) {
    updateStockPriceIfNeeded(stocks[selectedStock]);
    lastStockRefresh = millis();
  }

  delay(100);
}
