// ota.h
// Checks GitHub for a newer firmware version on every boot.
// Reads version from latest GitHub Release tag directly.
// No version.txt needed. CI never writes back to main.
// Device reboots automatically into new firmware.

#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

#define GH_USER  "shenoiz"
#define GH_REPO  "princess_ai_bot"

namespace OTA {

// Show a message on the OLED during update
void showOLED(String msg) {
  Face::oled.clearDisplay();
  Face::oled.setCursor(0, 10);
  Face::oled.setTextSize(1);
  Face::oled.println(msg);
  Face::oled.display();
}

// Fetch latest release tag name AND bin URL in one API call
// Returns false if fetch failed
bool getLatestRelease(String &outVersion, String &outBinUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.github.com/repos/")
             + GH_USER + "/" + GH_REPO + "/releases/latest";
  http.begin(client, url);
  http.addHeader("User-Agent", "ESP32-Princess-Buddy");
  http.setTimeout(10000);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("OTA: releases API error %d\n", code);
    http.end();
    return false;
  }

  // Use stream to save heap
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("OTA: JSON parse error %s\n", err.c_str());
    return false;
  }

  // tag_name is "v1.0.13" — strip the v prefix
  String tag = doc["tag_name"].as<String>();
  if (tag.startsWith("v")) tag = tag.substring(1);
  outVersion = tag;

  // Find .bin asset URL
  JsonArray assets = doc["assets"];
  for (JsonObject a : assets) {
    String name = a["name"].as<String>();
    if (name.endsWith(".bin")) {
      outBinUrl = a["browser_download_url"].as<String>();
      break;
    }
  }

  return true;
}

// Resolve redirect manually — GitHub release URLs redirect to
// signed objects.githubusercontent.com URLs
String resolveRedirect(String url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  String location = http.getLocation();
  http.end();
  if (location.length() > 0) return location;
  return url;
}

// Call once in setup() after WiFi connects.
// Compares FW_VERSION with latest GitHub release tag.
// Downloads and flashes if different.
void checkAndUpdate(String currentVersion) {
  Serial.println("OTA: checking for update...");
  showOLED("Checking\nfor update...");

  String latestVersion = "";
  String binUrl = "";

  if (!getLatestRelease(latestVersion, binUrl)) {
    Serial.println("OTA: could not reach GitHub");
    return;
  }

  Serial.printf("OTA: current=%s latest=%s\n",
    currentVersion.c_str(), latestVersion.c_str());

  // Compare patch number only — format is 1.0.X
  int latestNum  = latestVersion.substring(latestVersion.lastIndexOf('.') + 1).toInt();
  int currentNum = currentVersion.substring(currentVersion.lastIndexOf('.') + 1).toInt();

  if (latestNum <= currentNum) {
    Serial.println("OTA: up to date");
    showOLED("Up to date!\n" + currentVersion);
    delay(1000);
    return;
  }

  if (binUrl == "") {
    Serial.println("OTA: no .bin found in release");
    return;
  }

  Serial.println("OTA: update available " + latestVersion);
  showOLED("Update!\n" + latestVersion + "\nDownloading...");

  // Resolve the redirect to get final signed URL
  String finalUrl = resolveRedirect(binUrl);
  Serial.println("OTA: final URL " + finalUrl);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  t_httpUpdate_return ret = httpUpdate.update(client, finalUrl);

  switch (ret) {
    case HTTP_UPDATE_OK:
      // Device reboots automatically — never reaches here
      break;
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA failed: %s\n",
        httpUpdate.getLastErrorString().c_str());
      showOLED("Update failed\nTry next boot");
      delay(5000);
      break;
    case HTTP_UPDATE_NO_UPDATES:
      break;
  }
}

} // namespace OTA
