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

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SD.h>
//#define FS_NO_GLOBALS
//#include <FS.h>                       // SPIFFS file-system: store web server html, CSS etc.

#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>

#include "emonesp.h"
#include "web_server.h"
#include "web_server_static.h"
//#include "AsyncSDServer.h"
#include "config.h"
#include "wifi.h"
#include "mqtt.h"
#include "input.h"
#include "emoncms.h"
#include "ota.h"
#include "debug.h"
#include "emondc.h"

#include "./web_server_files/web_server.config_js.h"
#include "./web_server_files/web_server.home_html.h"
#include "./web_server_files/web_server.style_css.h"
#include "./web_server_files/web_server.lib_js.h"

AsyncWebServer server(80);          // Create class for Web server
AsyncWebSocket ws("/ws");
StaticFileWebHandler staticFile;

// Content Types
const char _CONTENT_TYPE_HTML[] PROGMEM = "text/html";
const char _CONTENT_TYPE_TEXT[] PROGMEM = "text/plain";
const char _CONTENT_TYPE_CSS[] PROGMEM = "text/css";
const char _CONTENT_TYPE_JSON[] PROGMEM = "application/json";
const char _CONTENT_TYPE_JS[] PROGMEM = "application/javascript";

bool enableCors = true;

// Event timeouts
unsigned long wifiRestartTime = 0;
unsigned long mqttRestartTime = 0;
unsigned long systemRestartTime = 0;
unsigned long systemRebootTime = 0;

// Get running firmware version from build tag environment variable
#define TEXTIFY(A) #A
#define ESCAPEQUOTE(A) TEXTIFY(A)
String currentfirmware = ESCAPEQUOTE(BUILD_TAG);


void dumpRequest(AsyncWebServerRequest *request) {
  if(request->method() == HTTP_GET) {
    DBUGF("GET");
  } else if(request->method() == HTTP_POST) {
    DBUGF("POST");
  } else if(request->method() == HTTP_DELETE) {
    DBUGF("DELETE");
  } else if(request->method() == HTTP_PUT) {
    DBUGF("PUT");
  } else if(request->method() == HTTP_PATCH) {
    DBUGF("PATCH");
  } else if(request->method() == HTTP_HEAD) {
    DBUGF("HEAD");
  } else if(request->method() == HTTP_OPTIONS) {
    DBUGF("OPTIONS");
  } else {
    DBUGF("UNKNOWN");
  }
  DBUGF(" http://%s%s", request->host().c_str(), request->url().c_str());

  if(request->contentLength()){
    DBUGF("_CONTENT_TYPE: %s", request->contentType().c_str());
    DBUGF("_CONTENT_LENGTH: %u", request->contentLength());
  }

  int headers = request->headers();
  int i;
  for(i=0; i<headers; i++) {
    AsyncWebHeader* h = request->getHeader(i);
    DBUGF("_HEADER[%s]: %s", h->name().c_str(), h->value().c_str());
  }

  int params = request->params();
  for(i = 0; i < params; i++) {
    AsyncWebParameter* p = request->getParam(i);
    if(p->isFile()){
      DBUGF("_FILE[%s]: %s, size: %u", p->name().c_str(), p->value().c_str(), p->size());
    } else if(p->isPost()){
      DBUGF("_POST[%s]: %s", p->name().c_str(), p->value().c_str());
    } else {
      DBUGF("_GET[%s]: %s", p->name().c_str(), p->value().c_str());
    }
  }
}

// -------------------------------------------------------------------
// Helper function to perform the standard operations on a request
// -------------------------------------------------------------------

bool requestPreProcess(AsyncWebServerRequest *request, AsyncResponseStream *&response, const char *contentType = "application/json")
{
  if (www_username != "" && !request->authenticate(www_username.c_str(), www_password.c_str())) {
    request->requestAuthentication();
    return false;
  }

  response = request->beginResponseStream(contentType);
  if (enableCors) {
    response->addHeader("Access-Control-Allow-Origin", "*");
  }

  return true;
}

// -------------------------------------------------------------------
// Wifi scan /scan not currently used
// url: /scan
//
// First request will return 0 results unless you start scan from somewhere else (loop/setup)
// Do not request more often than 3-5 seconds
// -------------------------------------------------------------------
void
handleScan(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response)) {
    return;
  }

  String json = "[";
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
  } else if (n) {
    for (int i = 0; i < n; ++i) {
      if (i) json += ",";
      json += "{";
      json += "\"rssi\":" + String(WiFi.RSSI(i));
      json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
      json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
      json += ",\"channel\":" + String(WiFi.channel(i));
      json += ",\"secure\":" + String(WiFi.encryptionType(i));
      json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
      json += "}";
    }
    WiFi.scanDelete();
    if (WiFi.scanComplete() == -2) {
      WiFi.scanNetworks(true);
    }
  }
  json += "]";
  request->send(200, "text/json", json);
}

// -------------------------------------------------------------------
// Handle turning Access point off
// url: /apoff
// -------------------------------------------------------------------
void
handleAPOff(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  response->setCode(200);
  response->print("Turning AP Off");
  request->send(response);

  DBUGLN("Turning AP Off");
  systemRebootTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Save selected network to EEPROM and attempt connection
// url: /savenetwork
// -------------------------------------------------------------------
void
handleSaveNetwork(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String qsid = request->arg("ssid");
  String qpass = request->arg("pass");

  if (qsid != 0) {
    config_save_wifi(qsid, qpass);

    response->setCode(200);
    response->print("saved");
    wifiRestartTime = millis() + 2000;
  } else {
    response->setCode(400);
    response->print("No SSID");
  }

  request->send(response);
}

// -------------------------------------------------------------------
// Save Emoncms
// url: /saveemoncms
// -------------------------------------------------------------------
void
handleSaveEmoncms(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_save_emoncms(request->arg("server"),
                      request->arg("path"),
                      request->arg("node"),
                      request->arg("apikey"),
                      request->arg("fingerprint"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s %s",
           emoncms_server.c_str(),
           emoncms_path.c_str(),
           emoncms_node.c_str(),
           emoncms_apikey.c_str(),
           emoncms_fingerprint.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);
}

// -------------------------------------------------------------------
// Save MQTT Config
// url: /savemqtt
// -------------------------------------------------------------------
void
handleSaveMqtt(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_save_mqtt(request->arg("server"),
                   request->arg("topic"),
                   request->arg("prefix"),
                   request->arg("user"),
                   request->arg("pass"));

  char tmpStr[200];
  snprintf(tmpStr, sizeof(tmpStr), "Saved: %s %s %s %s %s", mqtt_server.c_str(),
           mqtt_topic.c_str(), mqtt_feed_prefix.c_str(), mqtt_user.c_str(), mqtt_pass.c_str());
  DBUGLN(tmpStr);

  response->setCode(200);
  response->print(tmpStr);
  request->send(response);

  // If connected disconnect MQTT to trigger re-connect with new details
  mqttRestartTime = millis();
}


// -------------------------------------------------------------------
// Save the web site user/pass
// url: /saveadmin
// -------------------------------------------------------------------
void handleSaveAdmin(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String quser = request->arg("user");
  String qpass = request->arg("pass");

  config_save_admin(quser, qpass);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}


// -------------------------------------------------------------------
// Handle emonDC sampling setting
// url /emondc
// -------------------------------------------------------------------
void handleEmonDC(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String qinterval = request->arg("interval");
  String qvcalA = request->arg("vcalA");
  String qicalA = request->arg("icalA");
  String qvcalB = request->arg("vcalB");
  String qicalB = request->arg("icalB");
  String qchanA_VrefSet = request->arg("chanA_VrefSet");
  String qchanB_VrefSet = request->arg("chanB_VrefSet");
  String qchannelA_gain = request->arg("channelA_gain");
  String qchannelB_gain = request->arg("channelB_gain");
  String qR1_A = request->arg("R1_A");
  String qR2_A = request->arg("R2_A");
  String qR1_B = request->arg("R1_B");
  String qR2_B = request->arg("R2_B");
  String qRshunt_A = request->arg("Rshunt_A");
  String qRshunt_B = request->arg("Rshunt_B");
  String qAmpOffset_A = request->arg("AmpOffset_A");
  String qAmpOffset_B = request->arg("AmpOffset_B");
  String qVoltOffset_A = request->arg("VoltOffset_A");
  String qVoltOffset_B = request->arg("VoltOffset_B");
  String qBattType = request->arg("BattType");
  String qBattCapacity = request->arg("BattCapacity");
  String qBattCapHr = request->arg("BattCapHr");
  String qBattNom = request->arg("BattNom");
  String qBattVoltsAlarmHigh = request->arg("BattVoltsAlarmHigh");
  String qBattVoltsAlarmLow = request->arg("BattVoltsAlarmLow");
  String qBattPeukert = request->arg("BattPeukert");
  String qBattTempCo = request->arg("BattTempCo");

  config_save_emondc(qinterval, qicalA, qvcalA, qicalB, qvcalB, 
  qchanA_VrefSet, qchanB_VrefSet, qchannelA_gain, qchannelB_gain, 
  qR1_A, qR2_A, qR1_B, qR2_B, qRshunt_A, qRshunt_B,
  qAmpOffset_A, qAmpOffset_B, qVoltOffset_A, qVoltOffset_B,
  qBattType,qBattCapacity,qBattCapHr,qBattNom,qBattVoltsAlarmHigh,qBattVoltsAlarmLow,
  qBattPeukert,qBattTempCo);

  response->setCode(200);
  response->print("saved");
  request->send(response);
}

// -------------------------------------------------------------------
// Last values on atmega serial
// url: /lastvalues
// -------------------------------------------------------------------
void handleLastValues(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  response->setCode(200);
  response->print(last_datastr);
  request->send(response);
}


// -------------------------------------------------------------------
// Download from SD card.
// url: /download
// -------------------------------------------------------------------
bool sdGlobalFileInUse = false;
File sdGlobalFile;
// https://github.com/me-no-dev/ESPAsyncWebServer/issues/124
void handleDownload(AsyncWebServerRequest *request) 
{
  if(SD_present && !sdGlobalFileInUse)
  {
    sdGlobalFile = SD.open(datalogFilename, FILE_READ);
    if(sdGlobalFile)
    {
      Serial.println("Data remaining:" + String(sdGlobalFile.size()));
      sdGlobalFileInUse = true;

      AsyncWebServerResponse *response = request->beginChunkedResponse("application/octet-stream", 
        [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t 
        {
          uint32_t bytes = 0;

          //File SDReadFile = SD.open(datalogFilename, FILE_READ);
          if(sdGlobalFile)
          {
            sdGlobalFile.seek(index);
      
            size_t dataRemaining = sdGlobalFile.size() - sdGlobalFile.position();

            Serial.println("Data remaining:" + String(dataRemaining));

            uint32_t readBytes;

            //waitSD();
            if( dataRemaining > 0) {
              bytes = sdGlobalFile.readBytes((char *)buffer, min(maxLen, dataRemaining));
            }
            //leaveSD();

            if(0 == bytes) {
              Serial.println("Download finished");
              sdGlobalFile.close();
              sdGlobalFileInUse = false;
            }
          }
          return bytes;
        });

      response->addHeader("Content-Disposition", "attachment; filename=" + String("datalog.csv"));
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    } else {
      request->send(500, "text/plain", "Could not open " + datalogFilename);
    }
  } else {
    request->send(500, "text/plain", "SD card busy or not initialized");
  }
}

static String formatTime(time_t time)
{
  struct tm * tmstruct = localtime(&time);
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%d-%02d-%02d %02d:%02d:%02d", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
  return String(buffer);
}

void handleSdGet(AsyncWebServerRequest *request) {
  dumpRequest(request);

  AsyncResponseStream *response;
  if (www_username != "" && !request->authenticate(www_username.c_str(), www_password.c_str())) {
    request->requestAuthentication();
    return;
  }

  if(SD_present)
  {
    String path = request->url().substring(3);
    if(0 == path.length()) {
      const size_t capacity = JSON_OBJECT_SIZE(6) + 128;
      DynamicJsonDocument doc(capacity);

      doc["type"] = SD.type();
      doc["fatType"] = SD.fatType();
      doc["blocksPerCluster"] = SD.blocksPerCluster();
      doc["totalClusters"] = SD.totalClusters();
      doc["blockSize"] = SD.blockSize();
      doc["totalBlocks"] = SD.totalBlocks();
      doc["clusterSize"] = SD.clusterSize();
      doc["size"] = SD.size64();

      response = request->beginResponseStream(String(CONTENT_TYPE_JSON));
      if (enableCors) {
        response->addHeader("Access-Control-Allow-Origin", "*");
      }
      response->setCode(200);
      serializeJson(doc, *response);
      request->send(response);
    }
    else
    {
      String urlBase = String("/sd");
      if(path.equals("/")) {
        urlBase += "/";
      }

      File object = SD.open(path);
      if(!object) 
      {
        request->send(404, "text/plain", "Path \""+path+"\" does not exist");
      }
      else if(object.isDirectory())
      {
        response = request->beginResponseStream(String(CONTENT_TYPE_JSON));
        if (enableCors) {
          response->addHeader("Access-Control-Allow-Origin", "*");
        }
        response->setCode(200);

        // The directory listing can result in a large JSON doc so we will only use ArduinoJSON for each object
        // and build the JSON array manually 
        bool first = true;
        response->print("[");

        while (true) 
        {
          File entry =  object.openNextFile();
          if (! entry) {
            // no more files
            break;
          }

          const size_t capacity = JSON_OBJECT_SIZE(7) + 512;
          DynamicJsonDocument doc(capacity);
          doc["name"] = entry.name();
          doc["url"] = urlBase + entry.fullName();
          doc["directory"] = entry.isDirectory();
          doc["hidden"] = '.' == entry.name()[0];
          if (entry.isFile()) 
          {
            doc["size"] = entry.size();
            doc["create"] = formatTime(entry.getCreationTime());
            doc["modified"] = formatTime(entry.getLastWrite());
          }
          if(!first) {
            response->print(",");
          }
          serializeJson(doc, *response);
          entry.close();

          first = false;
        }
        object.close();

        response->print("]");
        request->send(response);
      } 
      else
      {
        if(sdGlobalFileInUse) {
          request->send(429, "text/plain", "Already downloading a file from SD, try again later");
          return;
        }
        sdGlobalFileInUse = true;

        sdGlobalFile = object;

        AsyncWebServerResponse *response = request->beginChunkedResponse("application/octet-stream", 
          [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t 
          {
            uint32_t bytes = 0;

            sdGlobalFile.seek(index);
      
            size_t dataRemaining = sdGlobalFile.size() - sdGlobalFile.position();
            DBUGLN("Data remaining:" + String(dataRemaining));

            uint32_t readBytes;

            //waitSD();
            if( dataRemaining > 0) {
              bytes = sdGlobalFile.readBytes((char *)buffer, min(maxLen, dataRemaining));
            }
            //leaveSD();

            if(0 == bytes) {
              DBUGLN("Download finished");
              sdGlobalFile.close();
              sdGlobalFileInUse = false;
            }

            return bytes;
          });

        response->addHeader("Content-Disposition", String("attachment; filename=") + sdGlobalFile.name());
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      }
    }
  } else {
    request->send(428, "text/plain", "SD card busy or not initialized");
  }
}

void handleSdDelete(AsyncWebServerRequest *request) {
  dumpRequest(request);

  AsyncResponseStream *response;
  if (www_username != "" && !request->authenticate(www_username.c_str(), www_password.c_str())) {
    request->requestAuthentication();
    return;
  }

  if(SD_present)
  {
    String path = request->url().substring(3);
    if(0 == path.length()) {
      request->send(405);
      return;
    }

    // Check if the file is a directory
    File object = SD.open(path);
    if(object) 
    {
      if(object.isDirectory()) {
        request->send(405, "text/plain", "Can not delete directories");
      }
      object.close();
      SD.remove(path);

      request->send(200, "text/plain", "Ok");
    } else {
      request->send(404, "text/plain", "Path \""+path+"\" does not exist");
    }
  } else {
    request->send(428, "text/plain", "SD card busy or not initialized");
  }
}

void handleSdPost(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Ok");
}

void handleSdPostBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if(!index) {
    DBUGF("BodyStart: %u", total);

    dumpRequest(request);

    AsyncResponseStream *response;
    if (www_username != "" && !request->authenticate(www_username.c_str(), www_password.c_str())) {
      request->requestAuthentication();
      return;
    }

    if(SD_present)
    {
      String path = request->url().substring(3);
      if(0 == path.length()) {
        request->send(405);
        return;
      }

      // Check if the file is a directory
      File object = SD.open(path);
      if(object) 
      {
        if(object.isDirectory()) {
          request->send(405, "text/plain", "Can not upload to directories");
        }
        object.close();
        SD.remove(path);
      }

      if(sdGlobalFileInUse) {
        request->send(429, "text/plain", "Already downloading a file from SD, try again later");
        return;
      }
      sdGlobalFile = SD.open(path, FILE_WRITE);
      if(sdGlobalFile)
      {
        sdGlobalFileInUse = true;

      } else {
        request->send(500, "text/plain", "Error opening file for write");
        return;
      } 
    } else {
      request->send(428, "text/plain", "SD card busy or not initialized");
      return;
    }

    //request->_tempObject = new String();
  }

  DBUGF("%.*s", len, (const char*)data);
  sdGlobalFile.write((const char*)data, len);

  //body->concat((const char*)data, len);
  if(index + len == total) {
    DBUGF("BodyEnd: %u", total);
    sdGlobalFile.close();
    sdGlobalFileInUse = false;
  }
}



// -------------------------------------------------------------------
// Returns status json
// url: /status
// -------------------------------------------------------------------
void handleStatus(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  if (wifi_mode == WIFI_MODE_STA) {
    s += "\"mode\":\"STA\",";
  } else if (wifi_mode == WIFI_MODE_AP_STA_RETRY
             || wifi_mode == WIFI_MODE_AP_ONLY) {
    s += "\"mode\":\"AP\",";
  } else if (wifi_mode == WIFI_MODE_AP_AND_STA) {
    s += "\"mode\":\"STA+AP\",";
  }
  s += "\"networks\":[" + st + "],";
  s += "\"rssi\":[" + rssi + "],";

  s += "\"srssi\":\"" + String(WiFi.RSSI()) + "\",";
  s += "\"ipaddress\":\"" + ipaddress + "\",";
  s += "\"emoncms_connected\":\"" + String(emoncms_connected) + "\",";
  s += "\"packets_sent\":\"" + String(packets_sent) + "\",";
  s += "\"packets_success\":\"" + String(packets_success) + "\",";

  s += "\"mqtt_connected\":\"" + String(mqtt_connected()) + "\",";

  s += "\"free_heap\":\"" + String(ESP.getFreeHeap()) + "\",";

  s += "\"rtc_set\":" + String(timeConfidence ? "true" : "false");

#ifdef ENABLE_LEGACY_API
  s += ",\"version\":\"" + currentfirmware + "\"";
  s += ",\"ssid\":\"" + esid + "\"";
  //s += ",\"pass\":\""+epass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"emoncms_server\":\"" + emoncms_server + "\"";
  s += ",\"emoncms_path\":\"" + emoncms_path + "\"";
  s += ",\"emoncms_node\":\"" + emoncms_node + "\"";
  //s += ",\"emoncms_apikey\":\""+emoncms_apikey+"\""; security risk: DONT RETURN APIKEY
  s += ",\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\"";
  s += ",\"mqtt_server\":\"" + mqtt_server + "\"";
  s += ",\"mqtt_topic\":\"" + mqtt_topic + "\"";
  s += ",\"mqtt_user\":\"" + mqtt_user + "\"";
  //s += ",\"mqtt_pass\":\""+mqtt_pass+"\""; security risk: DONT RETURN PASSWORDS
  s += ",\"mqtt_feed_prefix\":\"" + mqtt_feed_prefix + "\"";
  s += ",\"www_username\":\"" + www_username + "\"";
  //s += ",\"www_password\":\""+www_password+"\""; security risk: DONT RETURN PASSWORDS

#endif
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}


// -------------------------------------------------------------------
// Returns OpenEVSE Config json
// url: /config
// -------------------------------------------------------------------
void
handleConfig(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response)) {
    return;
  }

  String s = "{";
  s += "\"espflash\":\"" + String(ESP.getFlashChipSize()) + "\",";
  s += "\"version\":\"" + currentfirmware + "\",";

  s += "\"ssid\":\"" + esid + "\",";
  //s += "\"pass\":\""+epass+"\","; security risk: DONT RETURN PASSWORDS
  s += "\"emoncms_server\":\"" + emoncms_server + "\",";
  s += "\"emoncms_path\":\"" + emoncms_path + "\",";
  s += "\"emoncms_node\":\"" + emoncms_node + "\",";
  // s += "\"emoncms_apikey\":\""+emoncms_apikey+"\","; security risk: DONT RETURN APIKEY
  s += "\"emoncms_fingerprint\":\"" + emoncms_fingerprint + "\",";
  s += "\"mqtt_server\":\"" + mqtt_server + "\",";
  s += "\"mqtt_topic\":\"" + mqtt_topic + "\",";
  s += "\"mqtt_feed_prefix\":\"" + mqtt_feed_prefix + "\",";
  s += "\"mqtt_user\":\"" + mqtt_user + "\",";
  //s += "\"mqtt_pass\":\""+mqtt_pass+"\","; security risk: DONT RETURN PASSWORDS
  s += "\"www_username\":\"" + www_username + "\",";
  //s += "\"www_password\":\""+www_password+"\","; security risk: DONT RETURN PASSWORDS

  s += "\"postInterval\":\"" + String(main_interval_seconds) + "\",";
  s += "\"icalA\":\"" + String(icalA,3) + "\",";
  s += "\"vcalA\":\"" + String(vcalA,3) + "\",";
  s += "\"icalB\":\"" + String(icalB,3) + "\",";
  s += "\"vcalB\":\"" + String(vcalB,3) + "\",";
  s += "\"chanA_VrefSet\":\"" + String(chanA_VrefSet) + "\",";
  s += "\"chanB_VrefSet\":\"" + String(chanB_VrefSet) + "\",";
  s += "\"channelA_gain\":\"" + String(channelA_gain) + "\",";
  s += "\"channelB_gain\":\"" + String(channelB_gain) + "\",";
  s += "\"R1_A\":\"" + String(R1_A) + "\",";
  s += "\"R2_A\":\"" + String(R2_A) + "\",";
  s += "\"R1_B\":\"" + String(R1_B) + "\",";
  s += "\"R2_B\":\"" + String(R2_B) + "\",";
  s += "\"Rshunt_A\":\"" + String(Rshunt_A,5) + "\",";
  s += "\"Rshunt_B\":\"" + String(Rshunt_B,5) + "\",";
  s += "\"AmpOffset_A\":\"" + String(AmpOffset_A,3) + "\",";
  s += "\"AmpOffset_B\":\"" + String(AmpOffset_B,3) + "\",";
  s += "\"VoltOffset_A\":\"" + String(VoltOffset_A,3) + "\",";
  s += "\"VoltOffset_B\":\"" + String(VoltOffset_B,3) + "\",";
  s += "\"BattType\":\"" + String(BattType) + "\",";
  s += "\"BattCapacity\":\"" + String(BattCapacity) + "\",";
  s += "\"BattCapHr\":\"" + String(BattCapHr) + "\",";
  s += "\"BattNom\":\"" + String(BattNom) + "\",";
  s += "\"BattVoltsAlarmHigh\":\"" + String(BattVoltsAlarmHigh) + "\",";
  s += "\"BattVoltsAlarmLow\":\"" + String(BattVoltsAlarmLow) + "\",";
  s += "\"BattPeukert\":\"" + String(BattPeukert) + "\",";
  s += "\"BattTempCo\":\"" + String(BattTempCo) + "\"";
  s += "}";

  response->setCode(200);
  response->print(s);
  request->send(response);
}

// -------------------------------------------------------------------
// Reset config and reboot
// url: /reset
// -------------------------------------------------------------------
void
handleRst(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  config_reset();
  ESP.eraseConfig();

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRebootTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Restart (Reboot)
// url: /restart
// -------------------------------------------------------------------
void
handleRestart(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  response->setCode(200);
  response->print("1");
  request->send(response);

  systemRestartTime = millis() + 1000;
}

// -------------------------------------------------------------------
// Handle test input API
// url /input
// e.g http://192.168.0.75/input?string=CT1:3935,CT2:325,T1:12.5,T2:16.9,T3:11.2,T4:34.7
// -------------------------------------------------------------------
void handleInput(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  input_string = request->arg("string");

  response->setCode(200);
  response->print(input_string);
  request->send(response);

  DBUGLN(input_string);
}


// -------------------------------------------------------------------
// Manually set the time
// url: /settime
// -------------------------------------------------------------------
void
handleSetTime(AsyncWebServerRequest *request) {
  AsyncResponseStream *response;
  if (false == requestPreProcess(request, response, "text/plain")) {
    return;
  }

  String time = request->arg("time");

  struct tm tm;

  int yr, mnth, d, h, m, s;
  if(6 == sscanf( time.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &yr, &mnth, &d, &h, &m, &s))
  {
    DateTime set_time(yr, mnth, d, h, m, s);
    char buf[] = "YYYY-MM-DDThh:mm:ssZ";
    DBUGF("set_time = %s", set_time.toString(buf));
    set_rtc(set_time);
  }
  else
  {
    response->setCode(400);
    response->print("could not parse time");
    request->send(response);
    return;
  }

  response->setCode(200);
  response->print("set");
  request->send(response);
}



// -------------------------------------------------------------------
// Update firmware
// url: /update
// -------------------------------------------------------------------
void handle_update_progress_cb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  uint32_t free_space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!index) {
    Serial.println("Update");
    Update.runAsync(true);
    if (!Update.begin(free_space)) {
      Update.printError(Serial);
    }
  }

  Serial.print(".");
  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }

  if (final) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    } else {
      // restartNow = true; //Set flag so main loop can issue restart call
      Serial.println("Update complete");
      delay(1000);
      ESP.reset();
    }
  }
}





void handleNotFound(AsyncWebServerRequest *request)
{
  DBUG("NOT_FOUND: ");
  dumpRequest(request);

  if(wifi_mode == WIFI_MODE_AP_ONLY) {
    // Redirect to the home page in AP mode (for the captive portal)
    AsyncResponseStream *response = request->beginResponseStream(String(CONTENT_TYPE_HTML));

    String url = F("http://");
    url += ipaddress;

    String s = F("<html><body><a href=\"");
    s += url;
    s += F("\">OpenEVSE</a></body></html>");

    response->setCode(301);
    response->addHeader(F("Location"), url);
    response->print(s);
    request->send(response);
  } else {
    request->send(404);
  }
}


void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if(type == WS_EVT_CONNECT) {
    DBUGF("ws[%s][%u] connect", server->url(), client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT) {
    DBUGF("ws[%s][%u] disconnect: %u", server->url(), client->id());
  } else if(type == WS_EVT_ERROR) {
    DBUGF("ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG) {
    DBUGF("ws[%s][%u] pong[%u]: %s", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len)
    {
      //the whole message is in a single frame and we got all of it's data
      DBUGF("ws[%s][%u] %s-message[%u]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", len);
    } else {
      // TODO: handle messages that are comprised of multiple frames or the frame is split into multiple packets
    }
  }
}



void
web_server_setup()
{
  // SPIFFS.begin(); // mount the fs

  // Setup the static files
  // server.serveStatic("/", SPIFFS, "/")
  // .setDefaultFile("index.html")
  // .setAuthentication(www_username.c_str(), www_password.c_str()); 

  // Add the Web Socket server
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.addHandler(&staticFile);
  //server.addHandler(new AsyncStaticSDWebHandler("/", SD, "/", "max-age=604800"));

  // Start server & server root html /
  // server.on("/", handleHome);
  // Handle HTTP web interface button presses
  // server.on("/generate_204", handleHome);  //Android captive portal. Maybe not needed. Might be handled by notFound
  // server.on("/fwlink", handleHome);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound
  server.on("/status", handleStatus);
  server.on("/config", handleConfig);

  server.on("/savenetwork", handleSaveNetwork);
  server.on("/saveemoncms", handleSaveEmoncms);
  server.on("/savemqtt", handleSaveMqtt);
  server.on("/saveadmin", handleSaveAdmin);
  server.on("/settime", handleSetTime);

  server.on("/reset", handleRst);
  server.on("/restart", handleRestart);

  server.on("/scan", handleScan);
  server.on("/apoff", handleAPOff);
  server.on("/input", handleInput);
  server.on("/lastvalues", handleLastValues);

  server.on("/savedc", handleEmonDC);
  server.on("/download", handleDownload);

  server.on("/sd*", HTTP_GET, handleSdGet);
  server.on("/sd*", HTTP_DELETE, handleSdDelete);
  server.on("/sd*", HTTP_POST, handleSdPost, NULL, handleSdPostBody);

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200);
  }, handle_update_progress_cb);

  // Simple Firmware Update Form
  //server.on("/upload", HTTP_GET, handleUpdateGet);
  //server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);

  server.onNotFound(handleNotFound);
  //server.onNotFound(handleHome);
  
  server.begin();
}

void
web_server_loop() {
  // Do we need to restart the WiFi?
  if (wifiRestartTime > 0 && millis() > wifiRestartTime) {
    wifiRestartTime = 0;
    wifi_restart();
  }

  // Do we need to restart MQTT?
  if (mqttRestartTime > 0 && millis() > mqttRestartTime) {
    mqttRestartTime = 0;
    mqtt_restart();
  }

  // Do we need to restart the system?
  if (systemRestartTime > 0 && millis() > systemRestartTime) {
    systemRestartTime = 0;
    wifi_disconnect();
    ESP.restart();
  }

  // Do we need to reboot the system?
  if (systemRebootTime > 0 && millis() > systemRebootTime) {
    systemRebootTime = 0;
    wifi_disconnect();
    ESP.reset();
  }
}
