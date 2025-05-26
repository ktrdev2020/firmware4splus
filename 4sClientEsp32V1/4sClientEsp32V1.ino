// =========================================
// 4S Plus IoT Device Firmware
// สำหรับควบคุมอุปกรณ์ผ่าน MQTT
// Version: 1.0
// =========================================

// =================== ไลบรารี ===================
#include <WiFiManager.h>       // ไลบรารีสำหรับจัดการ WiFi แบบอัตโนมัติ
#include <Arduino.h>
#include <ArduinoJson.h>       // ไลบรารีสำหรับจัดการข้อมูล JSON
#include <MD5Builder.h>        // ไลบรารีสำหรับสร้าง MD5 hash
#include <EEPROM.h>            // ไลบรารีสำหรับจัดเก็บข้อมูลถาวร
#include <Preferences.h>       // ไลบรารีสำหรับจัดเก็บข้อมูลบน ESP32
#include <iostream>
#include <string>
#include <PubSubClient.h>      // ไลบรารีสำหรับการเชื่อมต่อ MQTT

// ตรวจสอบบอร์ดที่ใช้งานและโหลดไลบรารีที่เหมาะสม
#ifdef ESP32
  #include <WiFiClient.h>
  #include <esp_wifi.h>
  #include <Update.h>
  #include <WebServer.h>
  #include <HTTPClient.h>
  #include <HTTPUpdate.h>
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
  
  // กำหนดค่าพินสำหรับ ESP32
  #define LED_PIN 25           // พินสำหรับไฟ LED
  #define RELAY_PIN 26         // พินสำหรับรีเลย์
  #define BUTTON_PIN 13        // พินสำหรับปุ่มกด
  #define BUZZER_PIN 33        // พินสำหรับลำโพง buzzer
  
  // กำหนดค่าลอจิกสำหรับ ESP32
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
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
  #include <ESP8266httpUpdate.h>
  #include <ESP8266HTTPClient.h>
  
  // กำหนดค่าพินสำหรับ ESP8266
  #define LED_PIN 13           // พินสำหรับไฟ LED
  #define RELAY_PIN 12         // พินสำหรับรีเลย์
  #define BUTTON_PIN 3         // พินสำหรับปุ่มกด
  #define BUZZER_PIN 1         // TX Pin ของ ESP8266 สำหรับลำโพง buzzer
  
  // กำหนดค่าลอจิกสำหรับ ESP8266
  #define LED_ON HIGH
  #define LED_OFF LOW
  #define RELAY_ON HIGH
  #define RELAY_OFF LOW
  #define BUTTON_ON LOW
  #define BUTTON_OFF HIGH
  #define BUZZER_ON LOW
  #define BUZZER_OFF HIGH
#endif

// =================== ค่าคงที่สำหรับ MQTT ===================
const char* mqtt_server = "mqtt.ssk3.go.th";  // เซิร์ฟเวอร์ MQTT
const int mqtt_port = 1883;                   // พอร์ต MQTT
const char* mqtt_user = "ssk3";               // ชื่อผู้ใช้ MQTT
const char* mqtt_pass = "33030000";           // รหัสผ่าน MQTT
char* mqtt_topic;                             // หัวข้อ MQTT (จะกำหนดภายหลัง)
const char* clientId = "ESP32_Device";        // ID ของอุปกรณ์

// =================== ค่าคงที่สำหรับอุปกรณ์ ===================
String AP_SSID;                               // ชื่อ WiFi AP (จะกำหนดภายหลัง)
const int MODEL_ID = 2;                       // รหัสรุ่นอุปกรณ์ (1=sonoffbasic, 2=esp32, 3=esp32(12ch))
const int CURRENT_VERSION = 1;                // เวอร์ชันปัจจุบันของเฟิร์มแวร์
const int FirmwareID = 2;                     // รหัสเฟิร์มแวร์

// =================== ค่าคงที่สำหรับ API ===================
const char* AUTH_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJkZXZpY2VUeXBlIjoiNHNkZXZpY2UiLCJ1c2VybmFtZSI6ImRldnNzazMiLCJleHAiOjE3MzkzNTEzOTEsImlzcyI6ImFpLnNzazMuZ28udGgiLCJhdWQiOiJhaS5zc2szLmdvLnRoIn0.yF74XH5Mo9FWg9i2LUFTn-ztRHYxI1YbfqfORUpnGu0";
const char* API_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus";
const char* API_Register_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/register";
const char* API_MQTT_URL = "https://api.4splus.ssk3.go.th/api/v1/mqtt";
const char* FIRMWARE_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/firmware";
const char* SCHOOL_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/getSchoolId";

// =================== ค่าคงที่สำหรับ LWT (Last Will and Testament) ===================
char* willTopic;                              // หัวข้อสำหรับ LWT
String willMessageStr = String("{\"statusState\":\"OFFLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
const char* willMessage = willMessageStr.c_str();
const int willQoS = 0;
const bool willRetain = true;

// =================== ขนาด EEPROM ===================
#define EEPROM_SIZE 512

// =================== ตัวแปรสำหรับการทำงาน ===================
String Ex_String_Read;

// ตัวแปรสำหรับตั้งเวลา
unsigned long startMillis;                    // เวลาเริ่มต้นสำหรับตัวจับเวลา
unsigned long timer_duration = 1000;          // ระยะเวลาในการทำงานแต่ละรอบ (1 วินาที)
unsigned long elapsedMillis;                  // เวลาที่ผ่านไป

int loopcount = 0;

// ตัวแปรสำหรับการจัดการปุ่มกด
bool IsPressing = false;                      // สถานะการกดปุ่ม
bool ButtonState = false;                     // สถานะปุ่ม (เปิด/ปิด)

// ตัวแปรสำหรับการจัดการเสียงและไฟ
int AlarmStep[6] = { 1, 1, 0, 1, 1, 0 };   // รูปแบบการทำงานของ alarm
int AlarmStepState = 0;                       // สถานะปัจจุบันของ alarm

int LedStep[2] = { 1, 0 };                    // รูปแบบการทำงานของ LED
int LedStepState = 0;                         // สถานะปัจจุบันของ LED

// =================== ประกาศฟังก์ชัน ===================
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
void registerNewDevice();                     // แก้ไขชื่อฟังก์ชันให้ถูกต้อง
void playSuccessTone();
void upgradeFirmware(String url);             // แก้ไขชื่อฟังก์ชันให้ถูกต้อง
void playConfigTone();
void playErrorTone();
void postStateUpdate(bool btnState);
void postStateUpdateToApi(bool btnState);

// =================== Enums ===================
// สถานะการกดปุ่ม
enum { 
  unPress,     // ไม่มีการกดปุ่ม
  shortPress,  // กดปุ่มระยะสั้น
  longPress    // กดปุ่มระยะยาว
} btnPress;

// โหมดการทำงานของอุปกรณ์
enum { 
  Offline,     // ไม่ได้เชื่อมต่อ
  Online,      // เชื่อมต่ออยู่
  Config       // อยู่ในโหมดการตั้งค่า
} deviceMode;

// =================== ตัวแปรสำหรับการตั้งค่า ===================
bool wifiConfigured = false;
String deviceMAC = "";                        // MAC address ของอุปกรณ์
String deviceId = "";                         // ID ของอุปกรณ์ (MD5 hash ของ MAC)
unsigned long lastFirmwareCheck = 0;
const unsigned long FIRMWARE_CHECK_INTERVAL = 24 * 60 * 60 * 1000;  // ตรวจสอบทุก 24 ชั่วโมง

// พารามิเตอร์กำหนดเอง
String schoolId = "";                         // รหัสโรงเรียน
char tokenId[64] = "";                        // โทเค็น ID

// =================== ออบเจ็กต์สำหรับการทำงาน ===================
WiFiManager wifiManager;                      // ออบเจ็กต์สำหรับจัดการ WiFi
bool wifiConnected = false;
Preferences preferences;                      // ออบเจ็กต์สำหรับจัดเก็บข้อมูลถาวร

WiFiClientSecure clientSSL;                   // ออบเจ็กต์สำหรับการเชื่อมต่อ SSL
WiFiClient espClient;                         // ออบเจ็กต์สำหรับการเชื่อมต่อ WiFi
PubSubClient client(espClient);               // ออบเจ็กต์สำหรับการเชื่อมต่อ MQTT

// =================== ฟังก์ชันสำหรับการทำงาน ===================

/**
 * รับ MAC address ของอุปกรณ์
 * @return MAC address ในรูปแบบ String
 */
String getMacAddress() {
  uint8_t baseMac[6];

#if defined(ESP8266)
  WiFi.macAddress(baseMac);
#elif defined(ESP32)
  esp_wifi_get_mac(WIFI_IF_STA, baseMac);
#endif

  char baseMacChr[18] = { 0 };
  sprintf(baseMacChr, "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  return String(baseMacChr);
}

/**
 * สร้าง Device ID จาก MAC address โดยใช้ MD5 hash
 */
void generateDeviceId() {
  if (deviceMAC.length() > 0) {
    MD5Builder md5;
    md5.begin();
    md5.add(deviceMAC);
    md5.calculate();
    deviceId = md5.toString();
  }
}

/**
 * อัพเดทสถานะ LED ตามรูปแบบที่กำหนด
 */
void ReadLedState() {
  digitalWrite(LED_PIN, AlarmStep[LedStepState]);
}

/**
 * อัพเดทสถานะรีเลย์ตามรูปแบบที่กำหนด
 */
void ReadRelayState() {
  if (ButtonState) {
    if (AlarmStepState == 6) {
      AlarmStepState = 0;
    }
    digitalWrite(RELAY_PIN, AlarmStep[AlarmStepState] == 1 ? RELAY_ON : RELAY_OFF);
    AlarmStepState++;
  } else {
    digitalWrite(RELAY_PIN, RELAY_OFF);
    AlarmStepState = 0;
  }
}

/**
 * ตรวจสอบการกดปุ่ม
 */
void checkButtonPress() {
  if (digitalRead(BUTTON_PIN) == BUTTON_ON) {
    IsPressing = true;
  } else {
    IsPressing = false;
  }
}

/**
 * เปิดเสียง buzzer
 */
void BuzzerOn() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
}

/**
 * ปิดเสียง buzzer
 */
void BuzzerOff() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

/**
 * เปิดไฟ LED
 */
void LedOn() {
  digitalWrite(LED_PIN, LED_ON);
}

/**
 * ปิดไฟ LED
 */
void LedOff() {
  digitalWrite(LED_PIN, LED_OFF);
}

/**
 * ล้างข้อมูล schoolId จาก Preferences
 */
void clearSchoolId() {
  preferences.begin("config", false);  // เปิด Preferences ในโหมด Read/Write
  preferences.remove("schoolId");      // ลบค่า schoolId
  preferences.end();                   // ปิด Preferences
  Serial.println("schoolId ถูกล้างเรียบร้อยแล้ว");
}

/**
 * รีเซ็ตการตั้งค่า WiFi และรีสตาร์ทอุปกรณ์
 */
void ResetWifi() {
  wifiManager.resetSettings();         // รีเซ็ตการตั้งค่า WiFi
  clearSchoolId();                     // ล้างข้อมูล schoolId
  ESP.restart();                       // รีสตาร์ทอุปกรณ์
}

/**
 * นับการกดปุ่มและจัดการการทำงานตามระยะเวลาการกด
 */
void countPress() {
  if (IsPressing) {
    IsPressing = false;
    bool IsEndLoop = false;
    long startTime = millis();
    long maxPress = 5000;              // เวลากดนานสุด 5 วินาที
    long minPress = 100;               // เวลากดขั้นต่ำ 0.1 วินาที
    long chkTime = 0;
    
    // รอจนกว่าจะปล่อยปุ่ม
    while (digitalRead(BUTTON_PIN) == BUTTON_ON) {
      chkTime = millis() - startTime;
      if (chkTime >= maxPress) {
        BuzzerOn();                    // ส่งเสียงเตือนเมื่อกดนานเกิน maxPress
      }
    }
    
    BuzzerOff();                       // หยุดเสียงเตือน
    
    // ตรวจสอบระยะเวลาการกด
    if (chkTime >= maxPress) {
      btnPress = longPress;            // กดยาว (>5 วินาที)
      ButtonState = false;
      delay(1000);
      ResetWifi();                     // รีเซ็ต WiFi
    } else if (chkTime <= maxPress && chkTime >= minPress) {
      btnPress = shortPress;           // กดสั้น (0.1-5 วินาที)
      ButtonState = !ButtonState;      // สลับสถานะปุ่ม
      
      digitalWrite(RELAY_PIN, ButtonState ? RELAY_ON : RELAY_OFF);
      
      postStateUpdateToApi(ButtonState); // อัพเดทสถานะไปยัง API
    } else {
      btnPress = unPress;              // ไม่นับเป็นการกด
    }
  }

  IsPressing = false;
}

/**
 * เล่นเสียงเมื่อทำงานสำเร็จ
 */
void playSuccessTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(100);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

/**
 * เล่นเสียงเมื่อเกิดข้อผิดพลาด
 */
void playErrorTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(500);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

/**
 * เล่นเสียงเมื่ออยู่ในโหมดตั้งค่า
 */
void playConfigTone() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(200);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

/**
 * รับสถานะอุปกรณ์จาก API
 */
void getDeviceState() {
  if (loopcount++ >= 3) {
    loopcount = 0;

    Serial.println("ดึงสถานะอุปกรณ์จาก API");
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

    Serial.println("Device ID = " + String(deviceId));

    http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));
    http.addHeader("mac", getMacAddress());

    int httpResponseCode = http.GET();

    Serial.println("รหัสการตอบกลับ = " + String(httpResponseCode));
    if (httpResponseCode == 200) {
      String response = http.getString();
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        bool state = doc["switchState"];
        ButtonState = state;
        IsPressing = false;

        Serial.println("ตั้งค่าสถานะเริ่มต้นจากเซิร์ฟเวอร์: " + String(state));
      } else {
        Serial.println("เกิดข้อผิดพลาดในการอ่านข้อมูล JSON");
      }
    }

    http.end();
  }
}

/**
 * ส่งสถานะอัพเดทไปยัง API
 * @param btnState สถานะปุ่ม (true = เปิด, false = ปิด)
 */
void postStateUpdateToApi(bool btnState) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ไม่ได้เชื่อมต่อ WiFi!");
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
  doc["userName"] = "Esp32";  
  doc["mac"] = deviceMAC;  

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode > 0) {
    Serial.print("รหัสการตอบกลับ: ");
    Serial.println(httpResponseCode);
    Serial.println("การตอบกลับ: " + http.getString());
  } else {
    Serial.print("ข้อผิดพลาด: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}


/**
 * ดึงรหัสโรงเรียนจาก API
 */
void getSchoolID() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ไม่ได้เชื่อมต่อ WiFi!");
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
#endif

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));

  StaticJsonDocument<200> doc;
  doc["deviceId"] = deviceId;        // MD5 ของ MAC Address
  doc["modelID"] = MODEL_ID;         // Model ID สำหรับ 4s
  doc["mac"] = deviceMAC;            // MAC address
  doc["firmwareID"] = FirmwareID;    // รหัสเฟิร์มแวร์

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("📤 กำลังส่งคำขอ: " + requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("📩 การตอบกลับ: " + response);

    StaticJsonDocument<200> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      Serial.print("✅ รหัสการตอบกลับ: ");
      Serial.println(httpResponseCode);
      schoolId = String(responseDoc["schoolId"]);
    } else {
      Serial.println("❌ การแปลง JSON ล้มเหลว!");
    }
  } else {
    Serial.print("❌ ข้อผิดพลาด HTTP: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}


/**
 * โค้ดสำหรับการควบคุมอุปกรณ์ ESP32 ผ่าน MQTT
 * รองรับการอัพเดทเฟิร์มแวร์ผ่าน OTA และการจัดการ relay
 * โปรเจค: 4sPLUS
 */

/**
 * ฟังก์ชันโหลด School ID จาก Preferences หรือเรียก API
 * ถ้าไม่มี School ID จะทำการเรียก API และบันทึกค่าไว้
 */
void LoadSchoolID() {
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
      playErrorTone();
      delay(10000);
      ESP.restart();  // รีสตาร์ทเพื่อให้ค่าใหม่ถูกใช้
    }
  } else {
    Serial.println("Loaded schoolId from Preferences: " + schoolId);
  }

  preferences.end();
}

/**
 * ฟังก์ชันอัพเกรดเฟิร์มแวร์ผ่าน OTA
 * @param url URL ของไฟล์เฟิร์มแวร์ใหม่
 */
void upgradeFirmware(String url) {
  Serial.println("Starting OTA Update...");
  Serial.println("Downloading: " + url);
  playConfigTone(); // เล่นเสียงแจ้งว่ากำลังอัพเดท
  
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

/**
 * ฟังก์ชัน callback สำหรับรับข้อความ MQTT
 * จัดการคำสั่งสำหรับ relay, OTA update และ reset
 * @param topic หัวข้อ MQTT
 * @param payload ข้อมูลที่ได้รับ
 * @param length ความยาวของข้อมูล
 */
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

  // ตรวจสอบว่าเป็น topic ของ relay หรือ ota หรือ reset
  String topicStr = String(topic);

  if (topicStr.endsWith("/relay")) {
    // อ่านค่าจาก JSON สำหรับควบคุม relay
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
    // จัดการคำสั่งอัพเดตเฟิร์มแวร์
    String firmwareUrl = doc["url"];

    if (firmwareUrl) {
      Serial.println("Received OTA update request.");
      Serial.print("Firmware URL: ");
      Serial.println(firmwareUrl);

      // เรียกฟังก์ชันอัปเดต OTA
      upgradeFirmware(firmwareUrl);
    }
  } else if (topicStr.endsWith("/reset")) {
    // จัดการคำสั่งรีเซ็ตการตั้งค่า WiFi
    String Pass = doc["pass"];

    if (Pass == "33030000") {
      ResetWifi(); // เรียกฟังก์ชันรีเซ็ต WiFi
    }
  }
}

/**
 * ฟังก์ชันเชื่อมต่อ MQTT Server
 * พยายามเชื่อมต่อและ subscribe หัวข้อที่เกี่ยวข้อง
 */
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    //checkButtonPress();
    //countPress();
    if (client.connect(deviceId.c_str(), mqtt_user, mqtt_pass, 
                      ("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(), 
                      willQoS, willRetain, willMessage)) {  // ใช้ Username & Password และ Last Will
      Serial.println("Connected!");
      
      // ส่งข้อความแจ้งสถานะออนไลน์
      String willMessageStrOnline = String("{\"statusState\":\"ONLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
      const char* willMessageOnline = willMessageStrOnline.c_str();
      client.publish(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(), willMessageOnline, willRetain);
      
      // Subscribe หัวข้อต่างๆ
      //client.subscribe(("schoolId/" + schoolId + "/deviceId/+/#").c_str());  // Subscribe หัวข้อ relay
      client.subscribe(("schoolId/" + schoolId + "/deviceId/+/relay").c_str());  // Subscribe หัวข้อ relay
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/ota").c_str());  // Subscribe หัวข้อ ota
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/reset").c_str());  // Subscribe หัวข้อ reset
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/config").c_str());  // Subscribe หัวข้อ reset
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

/**
 * ฟังก์ชัน setup() - รันครั้งเดียวตอนเริ่มต้น
 * ตั้งค่าพื้นฐานและเชื่อมต่อ WiFi/MQTT
 */
void setup() {
  // เริ่มต้น Serial และแสดงเวอร์ชัน
  Serial.begin(115200);
  Serial.println("4sPLUS V " + String(CURRENT_VERSION));
  
  // ตั้งค่า WiFi และ SSL
  WiFi.mode(WIFI_STA);
  clientSSL.setInsecure();
  
  // กำหนด pinMode สำหรับขาต่างๆ
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);  // ตั้งค่าเริ่มต้นสำหรับ buzzer
  digitalWrite(LED_PIN, LED_OFF);        // ตั้งค่าเริ่มต้นสำหรับ LED

  // เล่นเสียงแจ้งการตั้งค่า
  playConfigTone();
  
  // ตั้งค่าตัวแปรเริ่มต้น
  startMillis = millis();  // ตั้งค่าเวลาเริ่มต้นสำหรับ timer
  deviceMode = Online;     // ตั้งค่าโหมดอุปกรณ์เป็น Online
  btnPress = unPress;      // ตั้งค่าสถานะปุ่มเป็น unPress

  // ดึง MAC address และสร้าง Device ID
  deviceMAC = getMacAddress();
  Serial.println(deviceMAC);
  AP_SSID = "4s+" + deviceMAC;  // ตั้งชื่อ AP เป็น "4s+" ตามด้วย MAC address
  generateDeviceId();           // สร้าง Device ID
  Serial.println(deviceId);
  
  // ตั้งค่า WiFiManager
  wifiManager.setConfigPortalTimeout(90);  // ตั้งเวลา timeout 90 วินาที

  // เชื่อมต่อ WiFi หรือเข้าสู่โหมดตั้งค่า
  if (!wifiManager.autoConnect(AP_SSID.c_str(), "12345678")) {
    Serial.println("Failed to connect WiFi");
    ESP.restart();  // รีสตาร์ทถ้าเชื่อมต่อไม่สำเร็จ
  }
  
  // โหลด School ID
  LoadSchoolID();

  // ตั้งค่า MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);  // กำหนด callback สำหรับรับข้อความ MQTT
  
  // เปิด LED แสดงสถานะว่าเชื่อมต่อสำเร็จ
  LedOn();
}

/**
 * ฟังก์ชัน loop() - ทำงานวนซ้ำตลอดเวลา
 * ตรวจสอบการเชื่อมต่อ MQTT และจัดการการทำงานของอุปกรณ์
 */
void loop() {
  // ตรวจสอบและเชื่อมต่อ MQTT ถ้าจำเป็น
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();  // ประมวลผลข้อความ MQTT
  
  // ตรวจสอบการกดปุ่ม
  checkButtonPress();
  countPress();

  // ตรวจสอบตามรอบเวลา
  elapsedMillis = millis() - startMillis;
  if (elapsedMillis >= timer_duration) {
    // อ่านสถานะ relay และ LED
    ReadRelayState();
    ReadLedState();
    
    // รีเซ็ตเวลาเริ่มต้น
    startMillis = millis();
  }
}