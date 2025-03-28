#include <WiFiManager.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <iostream>
#include <string>
#include <PubSubClient.h>

#ifdef ESP32
  #include <WiFiClient.h>
  #include <esp_wifi.h>
  #include <Update.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #include <WiFi.h>  // ไลบรารี WiFi สำหรับ ESP32
  #include <WiFiClientSecure.h>
  #define LED_PIN 25
  #define RELAY_PIN 26
  #define BUTTON_PIN 13
  #define BUZZER_PIN 33
  #define LED_ON HIGH
  #define LED_OFF LOW
  #define RELAY_ON HIGH
  #define RELAY_OFF LOW
  #define BUTTON_ON HIGH
  #define BUTTON_OFF LOW
  #define BUZZER_ON LOW
  #define BUZZER_OFF HIGH
#elif defined(ESP8266)
  #include <WiFiClientSecureBearSSL.h>
  #include <WiFiClient.h>
  #include <ESP8266WiFi.h>  // ไลบรารี WiFi สำหรับ ESP8266
  #include <WiFiClientSecure.h>
  #include <ESP8266httpUpdate.h>
  #include <ESP8266HTTPClient.h>
  #define LED_PIN 13
  #define RELAY_PIN 12
  #define BUTTON_PIN 3
  #define BUZZER_PIN 1  // TX Pin ของ ESP8266
  #define LED_ON HIGH
  #define LED_OFF LOW
  #define RELAY_ON HIGH
  #define RELAY_OFF LOW
  #define BUTTON_ON LOW
  #define BUTTON_OFF HIGH
  #define BUZZER_ON LOW
  #define BUZZER_OFF HIGH

#endif


const char* mqtt_server = "mqtt.ssk3.go.th";  // ใส่ IP ของ Windows Server
const int mqtt_port = 1883;                  // MQTT Port
const char* mqtt_user = "ssk3";              // Username MQTT
const char* mqtt_pass = "33030000";          // Password MQTT
//const char* mqtt_topic = "schoolId/{schoolId}/deviceId/deviceId/status";  // Topic ที่ใช้ Subscribe & Publish
char* mqtt_topic;
const char* clientId = "ESP32_Device";




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
const char* SCHOOL_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/getSchoolId";

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
int AlarmStep[7] = { 1, 1, 1, 1, 1, 0, 0 };
int AlarmStepState = 0;

int LedStep[2] = { 1, 0 };
int LedStepState = 0;


void getAlarmState();
void checkButtonPress();
void countPress();
void ReadRelayState();
void ReadLedState();
void BuzzerOn();
void BuzzerOff();
void LedOn();
void LedOff();
void generateDeviceId();
void ConfigWifi();
void getDeviceState();
void registerNewDevie();
void playSuccessTone();
void upgradeFirmware(String url);
void playConfigTone();
void playErrorTone();
void postStateUpdate(bool btnState);
void postStateUpdateToApi(bool btnState);

  enum { unPress,
         shortPress,
         longPress } btnPress;

enum { Offline,
       Online,
       Config } deviceMode;


bool wifiConfigured = false;
String deviceMAC = "";
String deviceId = "";
unsigned long lastFirmwareCheck = 0;
const unsigned long FIRMWARE_CHECK_INTERVAL = 24 * 60 * 60 * 1000;  // Check every 24 hours

// Custom parameters
String schoolId = "";// "1033530296";
char tokenId[64] = "";

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




void ReadLedState() {
  digitalWrite(LED_PIN, AlarmStep[LedStepState]);
}

void ReadRelayState() {
  if (ButtonState) {
    if (AlarmStepState == 7) {
      AlarmStepState = 0;
    }
    digitalWrite(RELAY_PIN, AlarmStep[AlarmStepState] == 1 ? RELAY_ON : RELAY_OFF);
    AlarmStepState++;
  } else {
    digitalWrite(RELAY_PIN, RELAY_OFF);
    AlarmStepState = 0;
  }
}

void checkButtonPress() {
  if (digitalRead(BUTTON_PIN) == BUTTON_ON) {
    IsPressing = true;
  } else {
    IsPressing = false;
  }
}



// buzzer sound
void BuzzerOn() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
}
void BuzzerOff() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}
void LedOn() {
  digitalWrite(LED_PIN, LED_ON);
}
void LedOff() {
  digitalWrite(LED_PIN, LED_OFF);
}

void clearSchoolId() {
    preferences.begin("config", false);  // เปิด Preferences ในโหมด Read/Write
    preferences.remove("schoolId");      // ลบค่า schoolId
    preferences.end();                    // ปิด Preferences
    Serial.println("schoolId ถูกล้างเรียบร้อยแล้ว");
}
void ResetWifi(){
    wifiManager.resetSettings();
    clearSchoolId();
    ESP.restart();
}
void countPress() {
  if (IsPressing) {
    IsPressing = false;
    bool IsEndLoop = false;
    long startTime = millis();
    long maxPress = 5000;
    long minPress = 100;
    long chkTime = 0;
    while (digitalRead(BUTTON_PIN) == BUTTON_ON) {
      chkTime = millis() - startTime;
      if (chkTime >= maxPress) {
        BuzzerOn();
      }
    }
    BuzzerOff();
    if (chkTime >= maxPress) {
      btnPress = longPress;
      ButtonState = false;
      long chkTime = 0;
      delay(1000);
      ResetWifi();
    } else if (chkTime <= maxPress && chkTime >= minPress) {
      btnPress = shortPress;
      ButtonState = !ButtonState;
      
      digitalWrite(RELAY_PIN, ButtonState ? RELAY_ON : RELAY_OFF);
      
      postStateUpdateToApi(ButtonState);

    } else {
      btnPress = unPress;
    }
  }

  IsPressing = false;
}
void ConfigWifi() {
  Serial.println("Long press detected - entering WiFi config mode");
  playConfigTone();


  if (!wifiManager.startConfigPortal(AP_SSID.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    playErrorTone();
    ESP.restart();
  }

  Serial.println("Connected to WiFi");
  playSuccessTone();
  wifiConfigured = true;
}

// Existing tone functions remain the same
void playSuccessTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void playErrorTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(500);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void playConfigTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void getDeviceState() {
  if (loopcount++ >= 3) {
    loopcount = 0;


    Serial.println("in get device state from api");
    HTTPClient http;
    http.setTimeout(5000);
    String url = String(API_BASE_URL) + "/" + deviceId;
#if defined(ESP8266)
    BearSSL::WiFiClientSecure client2;
    client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
    http.begin(client2, url);
#elif defined(ESP32)
    http.begin(url);
#endif




    Serial.println("device id = " + String(deviceId));
    //String url = String(API_BASE_URL) + "/" + "123456789";


    http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));
    http.addHeader("mac", getMacAddress());

    int httpResponseCode = http.GET();

    Serial.println("respone code = " + String(httpResponseCode));
    if (httpResponseCode == 200) {
      String response = http.getString();
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        bool state = doc["switchState"];
        //digitalWrite(RELAY_PIN, state);
        ButtonState = state;
        IsPressing = false;

        Serial.println("Initial state set from server: " + String(state));
      } else {
        Serial.println("error in post api");
      }
    }

    http.end();
  }
}

void postStateUpdate(bool btnState) {
  // std::unique_ptr<BearSSL::WiFiClientSecure> clientSsl(new BearSSL::WiFiClientSecure);
  //   clientSsl->setInsecure(); // ใช้เมื่อ API เป็น HTTPS แต่ไม่มีใบรับรอง SSL ที่เชื่อถือได้
  Serial.println(deviceId);
  HTTPClient http;
  http.setTimeout(5000);
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client2;
  client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
  http.begin(client2, API_MQTT_URL);
#elif defined(ESP32)
  http.begin(API_MQTT_URL);
#endif

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));

  String swState = "off";
  if (btnState) {
    swState = "on";
  }

  StaticJsonDocument<200> doc;
  doc["deviceId"] = deviceId;                       // Using MD5 hashed MAC address
  doc["relayState"] = btnState ? "true" : "false";  //[{deviceId : deviceId, switchState : swState, hookByUserId : "device", hookDate : "", deviceType : "esp32" }];
  //doc["data"] = deviceMAC;

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("State update response: " + response);
  } else {
    Serial.println("Error sending state update");
  }

  http.end();
}

void postStateUpdateToApi(bool btnState) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000);
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client2;
  client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
  http.begin(client2, API_MQTT_URL);
#elif defined(ESP32)
  http.begin(clientSSL, API_MQTT_URL);
#endif

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));


  StaticJsonDocument<200> doc;
  doc["deviceId"] = deviceId;   
  doc["relayState"] = btnState;  

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode > 0) {
    Serial.print("Response Code: ");
    Serial.println(httpResponseCode);
    Serial.println("Response: " + http.getString());
  } else {
    Serial.print("Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}

void checkFirmwareVersion() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000);
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client2;
  client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
  http.begin(client2, FIRMWARE_BASE_URL);
#elif defined(ESP32)
  http.begin(clientSSL, FIRMWARE_BASE_URL);
#endif  // ไม่ใช้ใบรับรอง SSL

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));  // ✅ ตรวจสอบการต่อ String

  StaticJsonDocument<200> doc;
  doc["deviceId"] = deviceId;         // ✅ MD5 ของ MAC Address
  doc["modelID"] = String(MODEL_ID);  // ✅ Model ID สำหรับ 4s

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("📤 Sending Request: " + requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode == 201) {
    digitalWrite(BUZZER_PIN, BUZZER_ON);
    delay(1000);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    delay(1000);
    digitalWrite(BUZZER_PIN, BUZZER_ON);
    delay(1000);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    delay(1000);
    Serial.println("🚀 Registering new device...");

    registerNewDevice();  // ✅ แก้ไขสะกดผิดจาก registerNewDevie()

  } else if (httpResponseCode == 202) {
    Serial.println("⚠️ Please map device to school.");
  } else if (httpResponseCode > 0) {  // ✅ ป้องกันกรณีที่ responseCode เป็น -1
    String response = http.getString();
    Serial.println("📩 Response: " + response);

    StaticJsonDocument<200> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      Serial.print("✅ Response Code: ");
      Serial.println(httpResponseCode);
      schoolId = String(responseDoc["schoolId"]);
      //Serial.println(schoolId);
      int newVersion = responseDoc["versionNumber"].as<int>();
      ;  // ✅ แก้ไขชื่อเป็น newVersion
      String firmwareUrl = responseDoc["firmwareUrl"];
      Serial.println(newVersion);
      Serial.println(firmwareUrl);

      if (newVersion > CURRENT_VERSION) {
        Serial.println("🔄 Updating Firmware...");
        upgradeFirmware(firmwareUrl);  // ✅ แก้ไขสะกดผิดจาก upgreadFirmware()
      }
    } else {
      Serial.println("❌ JSON Parsing Failed!");
    }
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}


void getSchoolID() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000);
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client2;
  client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
  http.begin(client2, SCHOOL_BASE_URL);
#elif defined(ESP32)
  http.begin(clientSSL, SCHOOL_BASE_URL);
#endif  // ไม่ใช้ใบรับรอง SSL

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));  // ✅ ตรวจสอบการต่อ String

  StaticJsonDocument<200> doc;
  doc["deviceId"] = deviceId;         // ✅ MD5 ของ MAC Address
  doc["modelID"] = String(MODEL_ID);  // ✅ Model ID สำหรับ 4s

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("📤 Sending Request: " + requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {  // ✅ ป้องกันกรณีที่ responseCode เป็น -1
    String response = http.getString();
    Serial.println("📩 Response: " + response);

    StaticJsonDocument<200> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      Serial.print("✅ Response Code: ");
      Serial.println(httpResponseCode);
      schoolId = String(responseDoc["schoolId"]);
      //Serial.println(schoolId);
      
    } else {
      Serial.println("❌ JSON Parsing Failed!");
    }
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}


void LoadSchoolID(){
  preferences.begin("config", false);

    // อ่านค่า schoolId จาก Preferences
    schoolId = preferences.getString("schoolId", "");

    if (schoolId == "") {
        Serial.println("No schoolId found, fetching from API...");
        getSchoolID();  // เรียก API เพื่อดึง schoolId

        if (schoolId != "") { // ถ้า API ส่งค่ามา
            preferences.putString("schoolId", schoolId); // บันทึกลง Preferences
            Serial.println("Saved schoolId: " + schoolId);
            preferences.end();

            Serial.println("Restarting ESP32...");
            delay(2000);
            ESP.restart();  // รีสตาร์ทเพื่อให้ค่าใหม่ถูกใช้
        } else {
            Serial.println("Failed to get schoolId from API!");
        }
    } else {
        Serial.println("Loaded schoolId from Preferences: " + schoolId);
    }

    preferences.end();
}

void registerNewDevice() {
  //เพิ่มอุปกรณ์เข้าไปใหม่
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000);
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client2;
  client2.setInsecure();  // ไม่ใช้ใบรับรอง SSL
  http.begin(client2, API_Register_URL);
#elif defined(ESP32)
  http.begin(clientSSL, API_Register_URL);
#endif  // ไม่ใช้ใบรับรอง SSL

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));


  StaticJsonDocument<200> doc;
  doc["deviceid"] = deviceId;
  doc["modelID"] = MODEL_ID;
  doc["deviceMac"] = deviceMAC;
  doc["relayState"] = "OFF";

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode > 0) {
    Serial.print("Response Code: ");
    Serial.println(httpResponseCode);
    Serial.println("Response: " + http.getString());
    ESP.restart();
  } else {
    Serial.print("Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}
void upgradeFirmware(String url) {
  //
  Serial.println("Starting OTA Update...");
  Serial.println("Downloading: " + url);
  playConfigTone();
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

// ฟังก์ชันที่เรียกเมื่อมีข้อความใหม่จาก MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Payload: ");
  Serial.println(message);

  // แปลงเป็น JSON Object
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  // ตรวจสอบว่าเป็น topic ของ relay หรือ ota
  String topicStr = String(topic);

  if (topicStr.endsWith("/relay")) {
    // อ่านค่าจาก JSON สำหรับ relay
    const char* relay = doc["relayState"];

    if (relay) {
      if (strcmp(relay, "ON") == 0) {
        digitalWrite(RELAY_PIN, RELAY_ON);
        ButtonState = true;
        Serial.println("relay : ON");
        //digitalWrite(LED_PIN, LED_ON);
      } else if (strcmp(relay, "OFF") == 0) {
        ButtonState = false;
        Serial.println("relay : OFF");
        digitalWrite(RELAY_PIN, RELAY_OFF);
        //digitalWrite(LED_PIN, LED_OFF);
      }
      Serial.println("relayState : " + String(ButtonState));
    }
  } else if (topicStr.endsWith("/ota")) {
    // ทำงานสำหรับ OTA
    String firmwareUrl = doc["url"];

    if (firmwareUrl) {
      Serial.println("Received OTA update request.");
      Serial.print("Firmware URL: ");
      Serial.println(firmwareUrl);

      // เรียกฟังก์ชันอัปเดต OTA (ต้องมีการเขียนแยก)
      upgradeFirmware(firmwareUrl);
    }
  }
}


void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    //checkButtonPress();
    //countPress();
    if (client.connect(clientId, mqtt_user, mqtt_pass, ("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(), willQoS, willRetain, willMessage)) {  // ใช้ Username & Password
                                                                                                                                                                     //if (client.connect("ESP_Device", mqtt_user, mqtt_pass)) {
      Serial.println("Connected!");
      String willMessageStrOnline = String("{\"statusState\":\"ONLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
      const char* willMessageOnline = willMessageStrOnline.c_str();
      client.publish(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(), willMessageOnline, willRetain);
      client.subscribe(("schoolId/" + schoolId + "/deviceId/+/relay").c_str());  // Subscribe ไปที่ Topic
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/ota").c_str());    // Subscribe ไปที่ ota firmware
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Hellow " + String(CURRENT_VERSION));
  WiFi.mode(WIFI_STA);
  clientSSL.setInsecure();
  //WiFi.STA.begin();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);  // set default buzzer
  digitalWrite(LED_PIN, LED_OFF);

  playConfigTone();
  

  startMillis = millis();  // set default start millis for loop by timer_duration

  deviceMode = Online;
  btnPress = unPress;

  deviceMAC = getMacAddress();
  Serial.println(deviceMAC);
  //AP_SSID = "4sPlus-" + deviceMAC.substring(6);  // Use last 6 characters of MAC
  AP_SSID = "4s+" + deviceMAC;  // Use last 6 characters of MAC
  generateDeviceId();
  // ใช้ WiFiManager ตั้งค่า WiFi อัตโนมัติ
  //setupWiFiManager();

  //wifiManager.setAPCallback(configModeCallback);  // เรียกเมื่อเข้าโหมดตั้งค่า WiFi
  wifiManager.setConfigPortalTimeout(180);       // ตั้งเวลา 180 วินาที (3 นาที)

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
    reconnectMQTT();
  }
  client.loop();
  checkButtonPress();
  countPress();

  

  elapsedMillis = millis() - startMillis;
  if (elapsedMillis >= timer_duration) {
    //Serial.println("in read relay state");
    ReadRelayState();
    ReadLedState();

    //getDeviceState();
    startMillis = millis();
  }

  // read Relay Stae;
}