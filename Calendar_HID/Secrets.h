#ifndef SECRETS_H
#define SECRETS_H

#include "AppState.h"

inline void trim_inplace(String &s){
  int i=0;
  while(i < s.length() && isspace((unsigned char)s[i])) i++;
  if (i) s = s.substring(i);
  int j = (int)s.length()-1;
  while(j>=0 && isspace((unsigned char)s[j])) j--;
  if (j < (int)s.length()-1) s = s.substring(0, j+1);
}

inline bool loadSecretsFromSD(const char* path="/secrets.txt"){
  ssid = ""; password = ""; backup_ssid = ""; backup_password = "";
  calendarUrl = ""; calendarUrl2 = ""; weatherApiKey = "";
  LAT = "25.7617"; LON = "-80.1918";

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

    for (int i=0;i<key.length();++i) key[i] = toupper((unsigned char)key[i]);

    if (key == "SSID") { ssid = val; Serial.println("  SSID: " + ssid); hasCredentials = true; }
    else if (key == "PASSWORD" || key == "PASS") { password = val; Serial.println("  PASSWORD: [set]"); }
    else if (key == "BACKUP_SSID" || key == "SSID2") { backup_ssid = val; Serial.println("  BACKUP_SSID: " + backup_ssid); }
    else if (key == "BACKUP_PASSWORD" || key == "PASS2") { backup_password = val; Serial.println("  BACKUP_PASSWORD: [set]"); }
    else if (key == "CAL_URL1" || key == "CALENDARURL" || key == "CALENDAR_URL" || key == "CAL1") { calendarUrl = val; Serial.println("  CAL_URL1: [set]"); }
    else if (key == "CAL_URL2" || key == "CALENDARURL2" || key == "CALENDAR_URL2" || key == "CAL2") { calendarUrl2 = val; Serial.println("  CAL_URL2: [set]"); }
    else if (key == "WEATHER_API_KEY" || key == "WEATHERAPIKEY" || key == "API_KEY" || key == "OWM_KEY") { weatherApiKey = val; Serial.println("  WEATHER_API_KEY: [set]"); }
    else if (key == "LAT") { LAT = val; Serial.println("  LAT: " + LAT); }
    else if (key == "LON") { LON = val; Serial.println("  LON: " + LON); }
  }
  f.close();
  return hasCredentials;
}

#endif // SECRETS_H
