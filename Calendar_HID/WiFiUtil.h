#ifndef WIFIUTIL_H
#define WIFIUTIL_H

#include "AppState.h"

inline bool connectWiFiWithFallback(unsigned long timeoutMs = 20000){
  if (ssid.length() > 0 && password.length() > 0){
    Serial.print("Connecting to primary WiFi: "); Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (millis() - start < timeoutMs){
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to primary WiFi!");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
        return true;
      }
      delay(250);
    }
    Serial.println("Primary WiFi connection failed");
  } else {
    Serial.println("No primary WiFi credentials provided");
  }

  if (backup_ssid.length() > 0 && backup_password.length() > 0){
    Serial.print("Connecting to backup WiFi: "); Serial.println(backup_ssid);
    WiFi.begin(backup_ssid.c_str(), backup_password.c_str());

    unsigned long start = millis();
    while (millis() - start < timeoutMs){
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to backup WiFi!");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
        return true;
      }
      delay(250);
    }
    Serial.println("Backup WiFi connection failed");
  }

  Serial.println("WiFi connection failed - no valid credentials or connection");
  return false;
}

#endif // WIFIUTIL_H
