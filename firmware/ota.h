// ota.h
// Checks GitHub for a newer firmware version on every boot.
// If found, downloads the .bin from GitHub Releases and flashes it.
// Device reboots automatically into new firmware.

#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Change these to match YOUR GitHub username and repo name
#define GH_USER  "shenoiz"
#define GH_REPO  "princess_ai_bot"

namespace OTA {

// Fetch version.txt from GitHub raw content
String getLatestVersion() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://raw.githubusercontent.com/")
             + GH_USER + "/" + GH_REPO + "/main/version.txt";
  http.begin(client, url);
  http.setTimeout(8000);
  int code = http.GET();
  String ver = "";
  if (code == 200) {
    ver = http.getString();
    ver.trim();
  }
  http.end();
  return ver;
}

// Fetch the .bin download URL from the latest GitHub Release
String getBinUrl() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.github.com/repos/")
             + GH_USER + "/" + GH_REPO + "/releases/latest";
  http.begin(client, url);
  http.addHeader("User-Agent", "ESP32-Princess-Buddy");
  http.setTimeout(10000);
  int code = http.GET();
  String binUrl = "";
  if (code == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getStream());
    JsonArray assets = doc["assets"];
    for (JsonObject a : assets) {
      String name = a["name"].as<String>();
      if (name.endsWith(".bin")) {
        binUrl = a["browser_download_url"].as<String>();
        break;
      }
    }
  }
  http.end();
  return binUrl;
}

// Show a message on the OLED during update
void showOLED(String msg) {
  Face::oled.clearDisplay();
  Face::oled.setCursor(0, 10);
  Face::oled.setTextSize(1);
  Face::oled.println(msg);
  Face::oled.display();
}

// Call this once in setup() after WiFi connects.
// Compares currentVersion (FW_VERSION) with version.txt on GitHub.
// Downloads and flashes if different.
void checkAndUpdate(String currentVersion) {
  Serial.println("OTA: checking for update...");
  showOLED("Checking\nfor update...");

  String latest = getLatestVersion();
  if (latest == "") {
    Serial.println("OTA: could not reach GitHub");
    return;
  }

  Serial.printf("OTA: current=%s latest=%s\n",
    currentVersion.c_str(), latest.c_str());

  if (latest == currentVersion) {
    Serial.println("OTA: up to date");
    showOLED("Up to date!\n" + currentVersion);
    delay(1000);
    return;
  }

  // New version available
  String binUrl = getBinUrl();
  if (binUrl == "") {
    Serial.println("OTA: no .bin found in release");
    return;
  }

  Serial.println("OTA: downloading " + binUrl);
  showOLED("Update found!\n" + latest + "\nDownloading...");

  // Follow the redirect manually to get the real download URL
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, binUrl);
  http.setTimeout(5000);
  // Use GET with redirect following to find final URL
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  String finalUrl = http.getLocation();
  http.end();

  // If we got a redirect location use it, otherwise use original
  if (finalUrl == "" || finalUrl == binUrl) {
    finalUrl = binUrl;
  }
  Serial.println("OTA: final URL " + finalUrl);

  WiFiClientSecure client2;
  client2.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  t_httpUpdate_return ret = httpUpdate.update(client2, finalUrl);
  
  switch (ret) {
    case HTTP_UPDATE_OK:
      // Device reboots automatically — never reaches here
      break;
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA failed: %s\n",
        httpUpdate.getLastErrorString().c_str());
      showOLED("Update failed\nTry next boot");
      delay(2000);
      break;
    case HTTP_UPDATE_NO_UPDATES:
      break;
  }
}

} // namespace OTA
