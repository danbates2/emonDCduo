/*
   -------------------------------------------------------------------
   EmonESP Serial to Emoncms gateway
   -------------------------------------------------------------------
   Adaptation of Chris Howells OpenEVSE ESP Wifi
   by Trystan Lea, Glyn Hudson, OpenEnergyMonitor
   All adaptation GNU General Public License as below.

   -------------------------------------------------------------------

   This file is part of OpenEnergyMonitor.org project.
   EmonESP is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.
   EmonESP is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with EmonESP; see the file COPYING.  If not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include "emonesp.h"
#include "wifi.h"
#include "config.h"
#include <ESP8266WiFi.h>              // Connect to Wifi
#include <ESP8266mDNS.h>              // Resolve URL for update server etc.
#include <DNSServer.h>                // Required for captive portal
#include <string.h>

DNSServer dnsServer;                  // Create class DNS server, captive portal re-direct
const byte DNS_PORT = 53;

// Access Point SSID, password & IP address. SSID will be softAP_ssid + chipID to make SSID unique
const char *softAP_ssid = "emonDC";
const char* softAP_password = "";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// hostname for mDNS. Try http://emondc.local
const char *esp_hostname = "emondc";

// Wifi Network Strings
String connected_network = "";
String status_string = "";
String ipaddress = "";


unsigned long Timer;
String st, rssi;

int wifi_mode = WIFI_MODE_STA;


// -------------------------------------------------------------------
// Start Access Point
// Access point is used for wifi network selection
// -------------------------------------------------------------------
void startAP() {
  DEBUG.print("Starting AP");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  DEBUG.print("Scan: ");
  int n = WiFi.scanNetworks();
  DEBUG.print(n);
  DEBUG.println(" networks found");
  st = "";
  rssi = "";
  for (int i = 0; i < n; ++i) {
    st += "\"" + WiFi.SSID(i) + "\"";
    rssi += "\"" + String(WiFi.RSSI(i)) + "\"";
    if (i < n - 1)
      st += ",";
    if (i < n - 1)
      rssi += ",";
  }
  delay(100);

  WiFi.softAPConfig(apIP, apIP, netMsk);
  // Create Unique SSID e.g "emonDC_XXXXXX"
  String softAP_ssid_ID = String(softAP_ssid) + "_" + String(ESP.getChipId());;
  WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password);
  connected_network = softAP_ssid_ID;

  // Setup the DNS server redirecting all the domains to the apIP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  //dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  dnsServer.start(DNS_PORT, "*", apIP);

  IPAddress myIP = WiFi.softAPIP();
  char tmpStr[40];
  sprintf(tmpStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  DEBUG.print("AP IP Address: ");
  DEBUG.println(tmpStr);
  ipaddress = tmpStr;
}

// -------------------------------------------------------------------
// Start Client, attempt to connect to Wifi network
// -------------------------------------------------------------------
void startClient() {
  DEBUG.print("Connecting to SSID: ");
  DEBUG.print(esid.c_str());
  WiFi.hostname(esp_hostname);
  WiFi.begin(esid.c_str(), epass.c_str());
  Timer = millis();

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tries++;
    if (tries == 20) break;
    if (digitalRead(0) == LOW) {
      Serial.println(" ");
      startAP();
      wifi_mode = WIFI_MODE_AP_ONLY;
      return;
    }
  }
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (digitalRead(0) == HIGH) {
      DEBUG.println(" ");
      DEBUG.print("Try Again...");
      WiFi.disconnect();
      delay(100);
      WiFi.begin(esid.c_str(), epass.c_str());
      tries = 0;
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        tries++;
        if (tries == 15) { Serial.println(" Network not found"); startAP(); wifi_mode = WIFI_MODE_AP_STA_RETRY; break; }
        if (digitalRead(0) == LOW) {
        Serial.println(" ");
        startAP();
        wifi_mode = WIFI_MODE_AP_ONLY;
        break;
        }
      }
      if (WIFI_MODE_AP_ONLY) break;
      attempt++;
      if (attempt == 3) {
        DEBUG.println(" ");
        startAP();
        // AP mode with SSID in EEPROM, connection will retry in 5 minutes
        wifi_mode = WIFI_MODE_AP_STA_RETRY;
        return;
      }
    }
    else if (digitalRead(0) == LOW) {
      DEBUG.println(" ");
      startAP();
      wifi_mode = WIFI_MODE_AP_ONLY;
      break;
    }
  }

  if (wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_AP_AND_STA) {
    IPAddress myAddress = WiFi.localIP();
    char tmpStr[40];
    sprintf(tmpStr, "%d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
    DEBUG.print("Connected, IP: ");
    DEBUG.println(tmpStr);
    // Copy the connected network and ipaddress to global strings for use in status request
    connected_network = esid;
    ipaddress = tmpStr;
  }
  DEBUG.println(" ");
}


void wifi_setup() {
  WiFi.disconnect();
  // 1) If no network configured start up access point
  if (esid == 0 || esid == "") {
    startAP();
    wifi_mode = WIFI_MODE_AP_ONLY; // AP mode with no SSID in EEPROM
  }
  // 2) else try and connect to the configured network
  else {
    WiFi.mode(WIFI_STA);
    wifi_mode = WIFI_MODE_STA;
    startClient();
  }

  // Start hostname broadcast in STA mode
  if ((wifi_mode == WIFI_MODE_STA || wifi_mode == WIFI_MODE_AP_AND_STA)) {
    if (MDNS.begin(esp_hostname)) {
      MDNS.addService("http", "tcp", 80);
    }
  }

  Timer = millis();
}

void wifi_loop() {

  dnsServer.processNextRequest(); // Captive portal DNS re-direct
  MDNS.update();

  // Remain in AP mode for 5 Minutes before resetting
  if (wifi_mode == WIFI_MODE_AP_STA_RETRY) {
    if ((millis() - Timer) >= 300000) {
      wifi_disconnect();
      delay(100);
      wifi_setup();
    }
  }
  yield();
}

void wifi_restart() {
  // Startup in STA + AP mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, netMsk);

  // Create Unique SSID e.g "emonESP_XXXXXX"
  String softAP_ssid_ID =
    String(softAP_ssid) + "_" + String(ESP.getChipId());;
  WiFi.softAP(softAP_ssid_ID.c_str(), softAP_password);

  // Setup the DNS server redirecting all the domains to the apIP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  wifi_mode = WIFI_MODE_AP_AND_STA;
  startClient();
}

void wifi_scan() {
  DEBUG.println("WIFI Scan");
  int n = WiFi.scanNetworks();
  DEBUG.print(n);
  DEBUG.println(" networks found");
  st = "";
  rssi = "";
  for (int i = 0; i < n; ++i) {
    st += "\"" + WiFi.SSID(i) + "\"";
    rssi += "\"" + String(WiFi.RSSI(i)) + "\"";
    if (i < n - 1)
      st += ",";
    if (i < n - 1)
      rssi += ",";
  }
}

void wifi_disconnect() {
  WiFi.disconnect();
}