#include <WiFiManager.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <iostream>
#include <string>
#include <PubSubClient.h>


#include <WiFiClient.h>
#include <esp_wifi.h>
#include <Update.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>  // ไลบรารี WiFi สำหรับ ESP32
#include <WiFiClientSecure.h>
// #define LED_PIN 19 // for ole device
#define LED_PIN 2
//#define YALLO_PIN 21
#define LED_ON HIGH
#define LED_OFF LOW
// #define YALLO_ON LOW
// #define YALLO_OFF HIGH



const char* mqtt_server = "mqtt.ssk3.go.th";  // ใส่ IP ของ Windows Server
const int mqtt_port = 1883;                  // MQTT Port
const char* mqtt_user = "ssk3";              // Username MQTT
const char* mqtt_pass = "33030000";          // Password MQTT
//const char* mqtt_topic = "schoolId/{schoolId}/deviceId/deviceId/status";  // Topic ที่ใช้ Subscribe & Publish
char* mqtt_topic;
const char* clientId = "ESP32_Device";

struct SchoolPin {
  String schoolid;
  int pin;
};

std::vector<SchoolPin> schoolPinList;

// Device Configuration
String AP_SSID;  // Will be set dynamically
//const char* AP_SSID = "4sPlusWifi";
const int MODEL_ID = 2;         // 1 = sonoffbasic 2 = esp32 3 = esp32(12ch) Your device model ID
const int CURRENT_VERSION = 1;  // Current firmware version
const char* AUTH_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJkZXZpY2VUeXBlIjoiNHNkZXZpY2UiLCJ1c2VybmFtZSI6ImRldnNzazMiLCJleHAiOjE3MzkzNTEzOTEsImlzcyI6ImFpLnNzazMuZ28udGgiLCJhdWQiOiJhaS5zc2szLmdvLnRoIn0.yF74XH5Mo9FWg9i2LUFTn-ztRHYxI1YbfqfORUpnGu0";
const char* API_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus";
const char* API_Register_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/register";
const char* API_MQTT_URL = "https://api.4splus.ssk3.go.th/api/v1/mqtt";
const char* FIRMWARE_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/firmware";
const char* SCHOOL_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/Splus/getGroupId";

// ตั้งค่าหัวข้อสำหรับ LWT
//const char* willTopic = "schoolId/{schoolId}/deviceId/{deviceId}/status";
char* willTopic;
String willMessageStr = String("{\"statusState\":\"OFFLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
const char* willMessage = willMessageStr.c_str();
//const char* willMessage = "{'statusState':'OFFLINE','version':'" + CURRENT_VERSION.c_str() + "'}";
const int willQoS = 0;
const bool willRetain = true;

// EEPROM Size
#define EEPROM_SIZE 512



String Ex_String_Read;

// timer for post state
unsigned long startMillis;  // example by https://forum.arduino.cc/t/wifimanager-h-with-esp32-serial/1288065/7
unsigned long timer_duration = 1000;
unsigned long elapsedMillis;

int loopcount = 0;

bool IsPressing = false;
bool ButtonState = false;
int AlarmStep[7] = { 1, 0};
int AlarmStepState = 0;

int LedStep[2] = { 1, 0 };
int LedStepState = 0;


void LedOn();
void LedOff();
void generateDeviceId();
void upgradeFirmware(String url);


bool wifiConfigured = false;
String deviceMAC = "";
String deviceId = "";
unsigned long lastFirmwareCheck = 0;
const unsigned long FIRMWARE_CHECK_INTERVAL = 24 * 60 * 60 * 1000;  // Check every 24 hours

// Custom parameters
String schoolId = "";// "1033530296";

WiFiManager wifiManager;
bool wifiConnected = false;
// WebServer server(80);
Preferences preferences;

WiFiClientSecure clientSSL;
WiFiClient espClient;
PubSubClient client(espClient);




String getMacAddress() {
  uint8_t baseMac[6];

#if defined(ESP8266)
  WiFi.macAddress(baseMac);
#elif defined(ESP32)
  esp_wifi_get_mac(WIFI_IF_STA, baseMac);
#endif
  //if (ret == ESP_OK) {
  char baseMacChr[18] = { 0 };
  sprintf(baseMacChr, "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  return String(baseMacChr);
}

void generateDeviceId() {
  if (deviceMAC.length() > 0) {
    MD5Builder md5;
    md5.begin();
    md5.add(deviceMAC);
    md5.calculate();
    deviceId = md5.toString();
  }
}


void LedOn() {
  digitalWrite(LED_PIN, LED_ON);
  //digitalWrite(YALLO_PIN, YALLO_ON);
}
void LedOff() {
  digitalWrite(LED_PIN, LED_OFF);
  //digitalWrite(YALLO_PIN, YALLO_OFF);
}

void clearSchoolId() {
    preferences.begin("config", false);  // เปิด Preferences ในโหมด Read/Write

    // ลบข้อมูล schoolId และ pin
    preferences.remove("schoolId"); // ลบ schoolId
    preferences.remove("schoolIdCount"); // ถ้ามีการเก็บจำนวน schoolId
    int index = 0;
    while (preferences.getString(("schoolId" + String(index)).c_str(), "") != "") {
        preferences.remove(("schoolId" + String(index)).c_str()); // ลบ schoolId ตามลำดับ
        preferences.remove(("pin" + String(index)).c_str()); // ลบ pin ตามลำดับ
        index++;
    }

    preferences.end();  // ปิด Preferences
    Serial.println("schoolId และ pin ถูกล้างเรียบร้อยแล้ว");
}
void ResetWifi(){
    wifiManager.resetSettings();
    clearSchoolId();
    ESP.restart();
}


void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("📩 MQTT Received: ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(message);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.println("❌ JSON Parsing Failed!");
    return;
  }


  String topicStr = String(topic);

  if (topicStr.endsWith("/relay")) {
    String deviceId = doc["deviceId"].as<String>();
    String relayState = doc["relayState"].as<String>();
    String IsTestMode = doc["testMode"].as<String>();

    if (IsTestMode == "true") {
      return;
    }

    for (auto& item : schoolPinList) {
      if (item.schoolid == deviceId) {
        digitalWrite(item.pin, (relayState == "ON") ? HIGH : LOW);
        Serial.println("✅ Relay " + String(item.pin) + " -> " + relayState);
        return;
      }
    }
  } else if (topicStr.endsWith("/reset")) {
    // ทำงานสำหรับ OTA
    String Pass = doc["pass"];

    if (Pass == "33030000") {
      ResetWifi();
    }
  }else if (topicStr.endsWith("/config")) {
    // ทำงานสำหรับ OTA
    String Pass = doc["pass"];

    if (Pass == "33030000") {
      //ResetWifi();
      Serial.println("in config new data");
      LoadSchoolID();
    }
  }
  

  Serial.println("❌ Device ID Not Found: " + deviceId);
}


void getSchoolID() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    return;
  }

  HTTPClient http;
   String url = String(SCHOOL_BASE_URL) + "/" + deviceId;  // ✅ แปลง char* เป็น String
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("📩 Response: " + response);

    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      JsonArray groupItems = responseDoc["groupItem"].as<JsonArray>();
      schoolPinList.clear();

      preferences.begin("config", false);
      preferences.clear();
      preferences.putUInt("schoolIdCount", groupItems.size());

      int index = 0;
      for (JsonVariant item : groupItems) {
        SchoolPin sp;
        sp.schoolid = item["schoolid"].as<String>();
        sp.pin = item["pin"].as<int>();
        schoolPinList.push_back(sp);

        preferences.putString(("schoolId" + String(index)).c_str(), sp.schoolid);
        preferences.putInt(("pin" + String(index)).c_str(), sp.pin);
        index++;
      }

      preferences.end();
      Serial.println("✅ Data saved to Preferences");
      delay(2000);
      ESP.restart();
    } else {
      Serial.println("❌ JSON Parsing Failed!");
    }
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}


void LoadSchoolID() {
  preferences.begin("config", true);

  uint32_t count = preferences.getUInt("schoolIdCount", 0);
  schoolPinList.clear();

  if (count == 0) {
    Serial.println("No data found, fetching from API...");
    preferences.end();
    getSchoolID();
    return;
  }

  for (size_t i = 0; i < count; i++) {
    SchoolPin sp;
    sp.schoolid = preferences.getString(("schoolId" + String(i)).c_str(), "");
    sp.pin = preferences.getInt(("pin" + String(i)).c_str(), -1);
    if (sp.schoolid != "" && sp.pin != -1) {
      schoolPinList.push_back(sp);
    }
  }

  preferences.end();

  Serial.println("✅ Loaded schoolIds & Pins: ");
  for (auto& item : schoolPinList) {
    Serial.println("ID: " + item.schoolid + " -> Pin: " + String(item.pin));
    pinMode(item.pin, OUTPUT);
    digitalWrite(item.pin, LOW);
  }

}


void upgradeFirmware(String url) {
  //
  Serial.println("Starting OTA Update...");
  Serial.println("Downloading: " + url);
  //playConfigTone();
  // ทำการอัปเดต OTA ผ่าน HTTP
  t_httpUpdate_return ret = httpUpdate.update(clientSSL, url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      Serial.println("Firmware update successful, restarting...");
      ESP.restart();  // รีบูตอุปกรณ์หลังอัปเดตสำเร็จ
      break;
  }
}


void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi disconnected! Reconnecting...");
  WiFi.reconnect();
  delay(5000);
  return;  // ออกจาก loop() เพื่อรอ WiFi ก่อน
}
  Serial.print("Connecting to MQTT...");
    //checkButtonPress();
    //countPress();
    if (client.connect(deviceId.c_str(), mqtt_user, mqtt_pass, ("deviceId/" + deviceId + "/status").c_str(), willQoS, willRetain, willMessage)) {  // ใช้ Username & Password
         
      // ส่งข้อความแจ้งสถานะออนไลน์
      String willMessageStrOnline = String("{\"statusState\":\"ONLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
      const char* willMessageOnline = willMessageStrOnline.c_str();
      client.publish(("deviceId/" + deviceId + "/status").c_str(), willMessageOnline, willRetain);
      
      Serial.println("✅ Connected!");
      String reSettopic = "deviceId/" + deviceId + "/reset";
      String configtopic = "deviceId/" + deviceId + "/config";
      client.subscribe(reSettopic.c_str());
      client.subscribe(configtopic.c_str());
      for (auto& item : schoolPinList) {
        String topic = "schoolId/" + item.schoolid + "/deviceId/+/relay";
        client.subscribe(topic.c_str());
        Serial.println("📡 Subscribed to: " + topic);
      } 
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(3000);
  Serial.println("Hellow " + String(CURRENT_VERSION));
  WiFi.mode(WIFI_STA);
  clientSSL.setInsecure();
  //WiFi.STA.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  //playConfigTone();
  

  startMillis = millis();  // set default start millis for loop by timer_duration

  

  deviceMAC = getMacAddress();
  Serial.println(deviceMAC);
  //AP_SSID = "4sPlus-" + deviceMAC.substring(6);  // Use last 6 characters of MAC
  AP_SSID = "4s+" + deviceMAC;  // Use last 6 characters of MAC

  generateDeviceId();
  // ใช้ WiFiManager ตั้งค่า WiFi อัตโนมัติ
  //setupWiFiManager();
  Serial.println(deviceId);

  //wifiManager.setAPCallback(configModeCallback);  // เรียกเมื่อเข้าโหมดตั้งค่า WiFi
  wifiManager.setConfigPortalTimeout(90);       // ตั้งเวลา 180 วินาที (3 นาที)

  if (!wifiManager.autoConnect(AP_SSID.c_str(), "12345678")) {
    Serial.println("Failed to connect WiFi");
    ESP.restart();
  }
  // get schoolid from server
  //checkFirmwareVersion();
  LoadSchoolID();

  // mqtt connecting
  client.setServer(mqtt_server, mqtt_port);
  // callback function for check relay by topic
  client.setCallback(callback);  // กำหนด callback สำหรับรับข้อความ MQTT
  // isConnected Wifi
  // display Led Green
  LedOn();

  // get
}

void loop() {
  
  if (!client.connected()) {
     Serial.print("MQTT disconnected. Error: ");
    Serial.println(client.state());  // ดู Error Code ของ MQTT
    reconnectMQTT();
  }
  client.loop();
  LedOn();
}