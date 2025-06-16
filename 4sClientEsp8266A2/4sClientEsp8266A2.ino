
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>  // ใช้สำหรับ ESP8266
#include <ESP8266httpUpdate.h>
#include <Preferences.h>

// กำหนดขาที่ใช้งาน
#define LED_PIN 1
#define RELAY_PIN 12
#define BUTTON_PIN 3
//#define BUZZER_PIN 5  // TX Pin ของ ESP8266

// กำหนดสถานะ
#define LED_ON LOW
#define LED_OFF HIGH
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#define BUTTON_ON LOW
#define BUTTON_OFF HIGH
#define BUZZER_ON LOW
#define BUZZER_OFF HIGH

// ตั้งค่า MQTT
const char* mqtt_server = "mqtt.ssk3.go.th";  // ใส่ IP ของ Windows Server
const int mqtt_port = 1883;                   // MQTT Port
const char* mqtt_user = "ssk3";               // Username MQTT
const char* mqtt_pass = "33030000";           // Password MQTT
char* mqtt_topic;                             // Topic ที่ใช้ Subscribe & Publish

// ตั้งค่าอุปกรณ์
String AP_SSID;                 // จะถูกตั้งค่าแบบไดนามิก
const int MODEL_ID = 1;         // 1 = sonoffbasic 2 = esp32 3 = esp32(12ch) รหัสรุ่นอุปกรณ์
const int CURRENT_VERSION = 1;  // เวอร์ชันเฟิร์มแวร์ปัจจุบัน
const int firmwareID = 1;
const char* AUTH_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJkZXZpY2VUeXBlIjoiNHNkZXZpY2UiLCJ1c2VybmFtZSI6ImRldnNzazMiLCJleHAiOjE3MzkzNTEzOTEsImlzcyI6ImFpLnNzazMuZ28udGgiLCJhdWQiOiJhaS5zc2szLmdvLnRoIn0.yF74XH5Mo9FWg9i2LUFTn-ztRHYxI1YbfqfORUpnGu0";
const char* API_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus";
const char* API_MQTT_URL = "https://api.4splus.ssk3.go.th/api/v1/mqtt";
const char* SCHOOL_BASE_URL = "https://api.4splus.ssk3.go.th/api/v1/splus/getSchoolId";

// ตั้งค่า Last Will and Testament (LWT)
char* willTopic;
String willMessageStr = String("{\"statusState\":\"OFFLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
const char* willMessage = willMessageStr.c_str();
const int willQoS = 0;
const bool willRetain = true;

// ตัวจับเวลา
unsigned long startMillis;
unsigned long timer_duration = 1000;
unsigned long elapsedMillis;
int loopcount = 0;

// ตัวแปรสถานะ
bool IsPressing = false;
bool ButtonState = false;
int AlarmStep[6] = { 1, 0, 1, 0, 1, 0 };
int AlarmStepState = 0;
int LedStep[2] = { 0, 1 };
int LedStepState = 1;


// ตัวแปรระบุตัวตนอุปกรณ์
String deviceMAC = "";
String deviceId = "";
String schoolId = "";
//int schoolId = 0;
// วัตถุนำมาใช้งาน
WiFiManager wifiManager;
Preferences preferences;
//WiFiClientSecure ClientSSL;
WiFiClient espClient;            // ใช้ WiFiClient แทน WiFiClientSecure สำหรับพอร์ต 1883
PubSubClient client(espClient);  // ตั้งค่า PubSubClient ใช้ espClient
std::unique_ptr<BearSSL::WiFiClientSecure> clientSSL(new BearSSL::WiFiClientSecure);


// ประกาศฟังก์ชัน
void checkButtonPress();
void countPress();
void ReadRelayState();
void ReadLedState();
void LedOn();
void LedOff();
void generateDeviceId();
void upgradeFirmware(String url);
void postStateUpdateToApi(bool btnState);
void getSchoolID();
void LoadSchoolID();
void clearSchoolId();
void ResetWifi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
String getMacAddress();

/**
 * ดึง MAC Address ของอุปกรณ์
 */
String getMacAddress() {
  uint8_t baseMac[6];
  WiFi.macAddress(baseMac);

  char baseMacChr[18] = { 0 };
  sprintf(baseMacChr, "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  return String(baseMacChr);
}

/**
 * สร้าง device ID จาก MD5 hash ของ MAC address
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
 * ควบคุมสถานะ LED ตามขั้นตอน
 */
void ReadLedState() {
  digitalWrite(LED_PIN, LED_ON);
}

/**
 * ควบคุมสถานะรีเลย์ตามสถานะปุ่ม
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
 * ตรวจสอบว่าปุ่มถูกกดหรือไม่
 */
void checkButtonPress() {
  if (digitalRead(BUTTON_PIN) == BUTTON_ON) {
    IsPressing = true;
  } else {
    IsPressing = false;
  }
}

/**
 * เปิด LED
 */
void LedOn() {
  digitalWrite(LED_PIN, LED_ON);
}

/**
 * ปิด LED
 */
void LedOff() {
  digitalWrite(LED_PIN, LED_OFF);
}

/**
 * ล้างค่า school ID จาก preferences
 */
void clearSchoolId() {
  preferences.begin("config", false);  // เปิด Preferences ในโหมด Read/Write
  preferences.remove("schoolId");      // ลบค่า schoolId
  preferences.end();                   // ปิด Preferences
  //Serial.println("schoolId ถูกล้างเรียบร้อยแล้ว");
}

/**
 * รีเซ็ตการตั้งค่า WiFi และรีสตาร์ท
 */
void ResetWifi() {

  // ล้างค่า WiFi ทั้งหมด (SSID, Password)
  wifiManager.resetSettings();

  delay(100);
  

  // คุณอาจต้องการล้างค่า Config ของคุณเอง (เช่น schoolId) ถ้ามี
  clearSchoolId();
  
  Serial.println("รีเซ็ตการตั้งค่า WiFi แล้ว!");

  delay(100);
  yield();
  WiFi.disconnect(true);
  Serial.println("ล้างการตั้งค่า WiFi แล้ว");
  
  ESP.eraseConfig();
  Serial.println("ล้างการตั้งค่าทั้งหมดแล้ว");
  delay(1000);
  yield();
  // รีสตาร์ทเพื่อเริ่มต้นใหม่
  ESP.restart();
}

/**
 * จัดการกับการกดปุ่มและกำหนดประเภทการกด
 */
void countPress() {
  if (IsPressing) {
    IsPressing = false;
    long startTime = millis();
    long maxPress = 5000;
    long minPress = 100;
    long chkTime = 0;

    while (digitalRead(BUTTON_PIN) == BUTTON_ON) {
      chkTime = millis() - startTime;
      if (chkTime >= maxPress) {
        for (int i = 0; i < 5; i++) {
          digitalWrite(LED_PIN, LED_ON);
          delay(200);
          digitalWrite(LED_PIN, LED_OFF);
          delay(200);
        }
        delay(1000);
      }
      delay(100);
    }

    if (chkTime >= maxPress) {
      //btnPress = longPress;
      
      ButtonState = false;
      chkTime = 0;
      //Serial.println("in Long press");
      ResetWifi();
    } else if (chkTime <= maxPress && chkTime >= minPress) {
      //btnPress = shortPress;
      ButtonState = !ButtonState;
      chkTime = 0;
      digitalWrite(RELAY_PIN, ButtonState ? RELAY_ON : RELAY_OFF);
      postStateUpdateToApi(ButtonState);
    } else {
      //btnPress = unPress;
    }
  }

  IsPressing = false;
}

/**
 * ส่งสถานะอัพเดตไปยัง API
 */
void postStateUpdateToApi(bool btnState) {
  if (WiFi.status() != WL_CONNECTED) {
    //Serial.println("WiFi ไม่ได้เชื่อมต่อ!");
    return;
  }

  HTTPClient http;
  // std::unique_ptr<BearSSL::WiFiClientSecure> client2(new BearSSL::WiFiClientSecure);
  // client2->setInsecure();
  http.begin(*clientSSL, API_MQTT_URL);
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));

  StaticJsonDocument<64> doc;
  doc["deviceId"] = deviceId;
  doc["relayState"] = btnState;
  doc["userName"] = "esp8266";
  doc["mac"] = deviceMAC;
  //doc["firmwareID"] = firmwareID;

  String requestBody;
  serializeJson(doc, requestBody);
  ////Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);
  if (httpResponseCode > 0) {
    //Serial.print("รหัสการตอบกลับ: ");
    //Serial.println(httpResponseCode);
    //Serial.println("การตอบกลับ: " + http.getString());
  } else {
    //Serial.print("ข้อผิดพลาด: ");
    //Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}

/**
 * ดึง school ID จาก API
 */
void getSchoolID() {
  if (WiFi.status() != WL_CONNECTED) {
    //Serial.println("WiFi ไม่ได้เชื่อมต่อ!");
    return;
  }

  HTTPClient http;
  // std::unique_ptr<BearSSL::WiFiClientSecure> client2(new BearSSL::WiFiClientSecure);
  // client2->setInsecure();  // ข้ามการตรวจสอบ SSL certificate
  http.begin(*clientSSL, SCHOOL_BASE_URL);
  http.setTimeout(5000);  // ตั้งเวลา Timeout

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(AUTH_TOKEN));

  StaticJsonDocument<128> doc;
  doc["deviceId"] = deviceId;      // MD5 ของ MAC Address
  doc["modelID"] = MODEL_ID;       // Model ID สำหรับ 4s
  doc["mac"] = deviceMAC;          // mac address
  doc["firmwareID"] = firmwareID;  // Model ID สำหรับ 4s

  String requestBody;
  serializeJson(doc, requestBody);
  Serial.println("📤 กำลังส่งคำขอ: " + requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("📩 การตอบกลับ: " + response);

    StaticJsonDocument<64> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);

    if (!error) {
      //Serial.print("✅ รหัสการตอบกลับ: ");
      //Serial.println(httpResponseCode);
      schoolId = String(responseDoc["schoolId"]);
      Serial.println(schoolId);
    } else {
      //Serial.println("❌ การแยกวิเคราะห์ JSON ล้มเหลว!");
    }
  } else {
    //Serial.print("❌ ข้อผิดพลาด HTTP: ");
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}

/**
 * โหลดหรือดึง school ID
 */
void LoadSchoolID() {
  preferences.begin("config", false);
  // อ่านค่า schoolId จาก Preferences
  schoolId = preferences.getString("schoolId", "");

  if (schoolId == "") {
    //Serial.println("ไม่พบ schoolId กำลังดึงจาก API...");
    getSchoolID();  // เรียก API เพื่อดึง schoolId

    if (schoolId != "") {
      preferences.putString("schoolId", schoolId);  // บันทึกลง Preferences
      //Serial.println("บันทึก schoolId: " + schoolId);
      preferences.end();

      //Serial.println("กำลังรีสตาร์ท ESP...");
      delay(2000);
      ESP.restart();  // รีสตาร์ทเพื่อให้ค่าใหม่ถูกใช้
    } else {
      //Serial.println("การดึง schoolId จาก API ล้มเหลว!");
       delay(2000);
      ESP.restart();  // รีสตาร์ทเพื่อให้ค่าใหม่ถูกใช้
    }
  } else {
    //Serial.println("โหลด schoolId จาก Preferences: " + schoolId);
  }

  preferences.end();
}

/**
 * อัพเกรดเฟิร์มแวร์จาก URL
 */
void upgradeFirmware(String url) {
  Serial.println("กำลังเริ่มการอัพเดต OTA...");
  Serial.println("กำลังดาวน์โหลด: " + url);

  //กระพริบ LED เพื่อแสดงว่ากำลังอัพเดตเฟิร์มแวร์
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, LED_ON);
    delay(200);
    digitalWrite(LED_PIN, LED_OFF);
    delay(200);
  }

  delay(1000);

  //อัพเดตเฟิร์มแวร์ผ่าน HTTP
 
  t_httpUpdate_return ret = ESPhttpUpdate.update(*clientSSL, url);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED ข้อผิดพลาด (%d): %s\n",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      Serial.println("การอัพเดตเฟิร์มแวร์สำเร็จ กำลังรีสตาร์ท...");
      ESP.restart();  // รีบูตอุปกรณ์หลังอัปเดตสำเร็จ
      break;
  }
}

/**
 * ฟังก์ชันเรียกกลับข้อความ MQTT
 */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("ได้รับข้อความ: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  //Serial.print("เนื้อหา: ");
  //Serial.println(message);

  // แปลงเป็น JSON Object
  StaticJsonDocument<64> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    //Serial.print("การแยกวิเคราะห์ JSON ล้มเหลว: ");
    //Serial.println(error.c_str());
    return;
  }

  // ตรวจสอบว่าเป็น topic ของ relay หรือ ota
  String topicStr = String(topic);

  if (topicStr.endsWith("/relay")) {
    // ประมวลผลคำสั่งรีเลย์
    const char* relay = doc["relayState"];

    if (relay) {
      if (strcmp(relay, "ON") == 0) {
        digitalWrite(RELAY_PIN, RELAY_ON);
        ButtonState = true;
        //Serial.println("รีเลย์ : เปิด");
      } else if (strcmp(relay, "OFF") == 0) {
        ButtonState = false;
        //Serial.println("รีเลย์ : ปิด");
        digitalWrite(RELAY_PIN, RELAY_OFF);
      }
      //Serial.println("สถานะรีเลย์ : " + String(ButtonState));
    }
  } else if (topicStr.endsWith("/ota")) {
    // ประมวลผลการอัพเดต OTA
    String firmwareUrl = doc["url"];

    if (firmwareUrl) {
      //Serial.println("ได้รับคำขออัพเดต OTA");
      //Serial.print("URL เฟิร์มแวร์: ");
      //Serial.println(firmwareUrl);
      upgradeFirmware(firmwareUrl);
    }
  } else if (topicStr.endsWith("/reset")) {
    // ประมวลผลคำสั่งรีเซ็ต
    String Pass = doc["pass"];
    //Serial.println(Pass);

    if (Pass) {
      if (Pass == "33030000") {
        //Serial.println("รหัสผ่านแอดมิน");
        //Serial.println("ตั้งค่า wifi และข้อมูล");
        ResetWifi();
      }
    }
  } else if (topicStr.endsWith("/config")) {
    // ประมวลผลคำสั่งตั้งค่า
    String Pass = doc["pass"];
    //Serial.println(Pass);

    if (Pass == "33030000") {
      //Serial.println("รหัสผ่านแอดมิน");
      //Serial.println("กำลังตั้งค่า schoolID ใหม่");
      clearSchoolId();
      LoadSchoolID();
    }
  }
}

/**
 * เชื่อมต่อกับเซิร์ฟเวอร์ MQTT
 */
void reconnectMQTT() {
  while (!client.connected()) {
    //Serial.print("กำลังเชื่อมต่อกับ MQTT...");
    checkButtonPress();
    countPress();

    if (client.connect(deviceId.c_str(), mqtt_user, mqtt_pass,
                       ("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(),
                       willQoS, willRetain, willMessage)) {
      Serial.println("เชื่อมต่อสำเร็จ!");

      // ประกาศสถานะออนไลน์
      String willMessageStrOnline = String("{\"statusState\":\"ONLINE\",\"version\":\"") + CURRENT_VERSION + "\"}";
      const char* willMessageOnline = willMessageStrOnline.c_str();
      client.publish(("schoolId/" + schoolId + "/deviceId/" + deviceId + "/status").c_str(),
                     willMessageOnline, willRetain);

      // สมัครสมาชิกหัวข้ออุปกรณ์
      client.subscribe(("schoolId/" + schoolId + "/deviceId/+/relay").c_str());
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId +  "/ota").c_str());
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId +  "/reset").c_str());
      client.subscribe(("schoolId/" + schoolId + "/deviceId/" + deviceId +  "/config").c_str());
    } else {
      //Serial.print("ล้มเหลว, rc=");
      //Serial.print(client.state());
      //Serial.println(" ลองอีกครั้งใน 5 วินาที");
      delay(5000);
    }
  }
}

/**
 * ฟังก์ชัน setup
 */
void setup() {
  Serial.begin(115200);
  
  delay(5000);

  Serial.println("4sPLUS เวอร์ชัน  " + String(CURRENT_VERSION));

  // ตั้งค่า WiFi
  WiFi.mode(WIFI_STA);
  clientSSL->setInsecure();
  //ClientSSL.setInsecure();

  // ตั้งค่าขา
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  //pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
  //digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  // เริ่มต้นตัวจับเวลา
  startMillis = millis();

  // รับข้อมูลระบุตัวตนอุปกรณ์
  deviceMAC = getMacAddress();
  
  AP_SSID = "4s+" + deviceMAC;
  generateDeviceId();
  Serial.println(deviceMAC + " : " + deviceId);
  // ตั้งค่า WiFi Manager
  wifiManager.setConfigPortalTimeout(90);  // หมดเวลา 90 วินาที

  if (!wifiManager.autoConnect(AP_SSID.c_str(), "12345678")) {
    //Serial.println("การเชื่อมต่อ WiFi ล้มเหลว");
    ESP.restart();
  }

  // โหลด school ID
  LoadSchoolID();

  // ตั้งค่าการเชื่อมต่อ MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // เปิด LED เพื่อแสดงว่าการตั้งค่าเสร็จสมบูรณ์
  LedOn();
}

/**
 * ลูปหลัก
 */
void loop() {
  checkButtonPress();

  if (IsPressing) {
    countPress();
  } else {
    if (!client.connected()) {
      reconnectMQTT();
    }

    client.loop();

    elapsedMillis = millis() - startMillis;
    if (elapsedMillis >= timer_duration) {
      ReadRelayState();
      ReadLedState();
      startMillis = millis();
    }
  }
}