#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_SHT4x.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ===== Credentials =====
#define WIFI_SSID     "Yaowaratt_Egg"
#define WIFI_PASSWORD "yao071256"
#define API_KEY       "AIzaSyCnBjSbD4zZpc-t0QfK7i5PlfQfuSgdyRw"
#define DATABASE_URL  "https://eggproject-cf0bc-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL    "comblack02@gmail.com"
#define USER_PASSWORD "rattanan1230z"
#define LINE_TOKEN    "+djF1vzQA1GT7PrnhY+HjiqLwIbbO9CxncPhtovCgSMcxHjuNW1Rpax9fSHeoq/Tmypoa+RxcrFbwAStWHYjuekXMMUwNUX32Sw3LCxbjwFKsQca9xMoqpk6bxTfrFSxllMCwI1PI2nKYrqI+FWHMwdB04t89/1O/w1cDnyilFU="
#define LINE_USER     "Uc176bf54f51b607d1c74ed32abac46bf"

// ===== Firebase =====
FirebaseData fbdo, fbdoCtrl, fbdoLog;
FirebaseAuth auth;
FirebaseConfig config;

// ===== Hardware =====
Adafruit_SHT4x sht45;
SemaphoreHandle_t i2cMutex;
Servo myServo;

#define RELAY1 26
#define RELAY2 27
#define RELAY3 14
#define RELAY4 25
#define RELAY5 33
#define SERVO_PIN 13

// ===== Servo =====
// Cycle: 135°(hold) → 90°(hold) → 45°(hold) → 90°(hold) → repeat
// hold time = servoHoldMs (กำหนดจาก Dashboard, default 1 ชั่วโมง)
const int CENTER = 90, LEFT = 45, RIGHT = 135;
volatile int currentPos = 90;
volatile int servoState = 9; // เริ่มที่ STOPPED
volatile unsigned long servoHoldTimer = 0;
volatile unsigned long servoMoveTimer = 0;
const unsigned long SERVO_STEP_MS = 60;
volatile unsigned long servoHoldMs = 3600000UL; // อ่านจาก Firebase (servoHoldHours)
String servoStatus = "STOPPED";
SemaphoreHandle_t servoStatusMutex;
volatile bool servoResetRequest = false;
volatile bool servoStartRequest = false;

// ===== Threshold (จาก Dashboard เท่านั้น) =====
float hum_on = 0, hum_off = 0;
float heater_off_temp = 0, fan_temp = 0, fan_hum = 0;
bool thresholdReady = false;

// ===== Incubation Timer =====
long long endTimeMs = 0;
bool alertedOneDay = false, alertedThirtyMin = false, alertedDone = false;

// ===== Timing =====
unsigned long shtTimer = 0, firebaseTimer = 0, logTimer = 0, controlTimer = 0;
const unsigned long SHT_INTERVAL      = 3000;
const unsigned long FIREBASE_INTERVAL  = 5000;
const unsigned long LOG_INTERVAL       = 3600000UL;
const unsigned long CONTROL_INTERVAL   = 3000;

// ===== State =====
float latestTemp = 0, latestHum = 0;
bool fogOn = false, fanMainOn = false, fan3On = true, fan4On = false, heaterOn = false;
String systemState = "STOP";
int shtErrorCount = 0;
const int SHT_MAX_ERROR = 5;
bool sensorFailed = false, wasRunning = false;

// ===== LINE =====
unsigned long lineAlertTimer = 0;
const unsigned long LINE_COOLDOWN = 600000UL;
volatile bool lineSending = false;

// ─── forward declarations ───
void handleServo();
void sendLineAlert(String message);
void sendLineForce(String message);
long long getNTPTime();

// ===== LINE Task =====
void lineTask(void* param) {
  String* msg = (String*)param;
  vTaskDelay(pdMS_TO_TICKS(2000));
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);
  HTTPClient http;
  http.begin(client, "https://api.line.me/v2/bot/message/push");
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", "Bearer " + String(LINE_TOKEN));
  http.setTimeout(10000);
  String body = "{\"to\":\"" + String(LINE_USER) + "\","
                "\"messages\":[{\"type\":\"text\",\"text\":\"" + *msg + "\"}]}";
  int code = http.POST(body);
  Serial.println(code == 200 ? "[LINE] ส่งสำเร็จ" : "[LINE] error: " + String(code));
  http.end(); client.stop();
  delete msg;
  lineSending = false;
  vTaskDelete(NULL);
}

void sendLineAlert(String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lineAlertTimer < LINE_COOLDOWN) return;
  lineAlertTimer = now;
  struct tm ti; String timeStr = "--";
  if (getLocalTime(&ti)) { char buf[20]; strftime(buf, sizeof(buf), "%d/%m/%y %H:%M", &ti); timeStr = String(buf); }
  String full = "แจ้งเตือนตู้ฟักไข่\n-----------------\n" + message +
                "-----------------\nFog: " + (fogOn ? "ON" : "OFF") +
                "\nHeater: " + (heaterOn ? "ON" : "OFF") + "\nเวลา: " + timeStr;
  full.replace("\\", "\\\\"); full.replace("\"", "\\\""); full.replace("\n", "\\n");
  String* copy = new String(full);
  lineSending = true;
  xTaskCreatePinnedToCore(lineTask, "lineTask", 16384, copy, 1, NULL, 0);
}

// ส่ง LINE ทันทีโดยข้ามช่วง cooldown (ใช้สำหรับ start/stop ระบบ)
void sendLineForce(String message) {
  lineAlertTimer = 0;
  sendLineAlert(message);
}

// ===== Servo Task =====
void servoTask(void* param) {
  for (;;) { handleServo(); vTaskDelay(pdMS_TO_TICKS(5)); }
}

// ===== Relay =====
void relayWrite(int pin, bool on) { digitalWrite(pin, on ? LOW : HIGH); }

void allRelaysOff() {
  relayWrite(RELAY1, false); relayWrite(RELAY2, false); relayWrite(RELAY3, false);
  relayWrite(RELAY4, false); relayWrite(RELAY5, false);
  fogOn = fanMainOn = fan3On = fan4On = heaterOn = false;
}

// ===== Servo =====
void resetServo() {
  servoResetRequest = true;
}

void moveTo(int target) {
  if (currentPos == target) return;
  unsigned long now = millis();
  if (now - servoMoveTimer < SERVO_STEP_MS) return;
  servoMoveTimer = now;
  currentPos += (currentPos < target) ? 1 : -1;
  myServo.write(currentPos);
}

// Servo state machine:
//   0: Move to 135°
//   1: Hold 135° for servoHoldMs
//   2: Move to 90° (กลาง)
//   3: Hold 90° ครั้งที่ 1
//   4: Move to 45°
//   5: Hold 45° for servoHoldMs
//   6: Move to 90° (กลาง)
//   7: Hold 90° ครั้งที่ 2 → state 0 (วนซ้ำ)
//   8: Return to 90° (reset)
//   9: STOPPED
void handleServo() {
  static int lastState = -1;
  unsigned long now = millis();

  if (servoResetRequest) {
    servoResetRequest = false;
    servoState     = 8;
    servoMoveTimer = now;
    lastState      = -1;
    return;
  }
  if (servoStartRequest) {
    servoStartRequest = false;
    servoState = 0;
    lastState  = -1;
    return;
  }

  if (servoState != lastState) {
    lastState = servoState;
    const char* s = "";
    switch (servoState) {
      case 0: s="Move to 135";    break;
      case 1: s="Hold 135";       break;
      case 2: s="Move to 90";     break;
      case 3: s="Hold 90 (1/2)";  break;
      case 4: s="Move to 45";     break;
      case 5: s="Hold 45";        break;
      case 6: s="Move to 90";     break;
      case 7: s="Hold 90 (2/2)";  break;
      case 8: s="Return to 90";   break;
      case 9: s="STOPPED";        break;
    }
    xSemaphoreTake(servoStatusMutex, pdMS_TO_TICKS(50));
    servoStatus = s;
    xSemaphoreGive(servoStatusMutex);
  }

  unsigned long holdMs = servoHoldMs;
  switch (servoState) {
    case 0: moveTo(RIGHT);  if(currentPos==RIGHT)  { servoHoldTimer=now; servoState=1; } break;
    case 1: if(now-servoHoldTimer>=holdMs) servoState=2; break;
    case 2: moveTo(CENTER); if(currentPos==CENTER) { servoHoldTimer=now; servoState=3; } break;
    case 3: if(now-servoHoldTimer>=holdMs) servoState=4; break;
    case 4: moveTo(LEFT);   if(currentPos==LEFT)   { servoHoldTimer=now; servoState=5; } break;
    case 5: if(now-servoHoldTimer>=holdMs) servoState=6; break;
    case 6: moveTo(CENTER); if(currentPos==CENTER) { servoHoldTimer=now; servoState=7; } break;
    case 7: if(now-servoHoldTimer>=holdMs) servoState=0; break;
    case 8: moveTo(CENTER); if(currentPos==CENTER) { servoState=9; } break;
    case 9: break;
  }
}

// ===== Control Logic =====
void controlSystem(float t, float h) {
  float tempMid = (heater_off_temp + fan_temp) / 2.0f;
  heaterOn = (t < tempMid);

  float humMid = (hum_on + hum_off) / 2.0f;
  if      (h >= hum_off) { fogOn = false; fanMainOn = false; }
  else if (h < humMid)   { fogOn = true;  fanMainOn = true;  }

  fan4On = (t >= fan_temp || h > fan_hum);
  fan3On = !(t >= fan_temp && h > fan_hum);

  relayWrite(RELAY1, fogOn);   relayWrite(RELAY2, fanMainOn);
  relayWrite(RELAY3, fan3On);  relayWrite(RELAY4, fan4On);
  relayWrite(RELAY5, heaterOn);
}

// ===== Firebase Read =====
void readControlFromFirebase() {
  if (!Firebase.ready()) return;
  if (!Firebase.RTDB.getJSON(&fbdoCtrl, "/incubator/control")) return;
  FirebaseJson& json = fbdoCtrl.jsonObject();
  FirebaseJsonData r;

  if (json.get(r, "system") && r.success) systemState = r.stringValue;

  bool ok1=false, ok2=false, ok3=false, ok4=false;
  if (json.get(r,"tempMin")&&r.success&&r.floatValue>=30&&r.floatValue<=42) { heater_off_temp=r.floatValue; ok1=true; }
  if (json.get(r,"tempMax")&&r.success&&r.floatValue>=30&&r.floatValue<=42) { fan_temp=r.floatValue;        ok2=true; }
  if (json.get(r,"humMin") &&r.success&&r.floatValue>=30&&r.floatValue<=90) { hum_on=r.floatValue;          ok3=true; }
  if (json.get(r,"humMax") &&r.success&&r.floatValue>=30&&r.floatValue<=90) { hum_off=r.floatValue; fan_hum=r.floatValue+5.0f; ok4=true; }
  if (ok1&&ok2&&ok3&&ok4) thresholdReady = true;

  if (json.get(r,"servoHoldHours")&&r.success&&r.floatValue>=0.1f&&r.floatValue<=24.0f) {
    servoHoldMs = (unsigned long)(r.floatValue * 3600000.0f);
  }

  if (json.get(r,"endTime")&&r.success)   endTimeMs = (long long)r.doubleValue;
  if (json.get(r,"startTime")&&r.success) {
    static long long lastST = 0;
    long long st = (long long)r.doubleValue;
    if (st != lastST) { lastST=st; alertedOneDay=alertedThirtyMin=alertedDone=false; }
  }
}

// ===== Send Current =====
void sendCurrentData() {
  if (!Firebase.ready() || lineSending) return;
  FirebaseJson json;
  json.set("temp",           latestTemp);
  json.set("humidity",       latestHum);
  json.set("systemState",    systemState);
  json.set("tempMin",        heater_off_temp);
  json.set("tempMax",        fan_temp);
  json.set("humMin",         hum_on);
  json.set("humMax",         hum_off);
  json.set("relay/fog",      fogOn     ? "ON":"OFF");
  json.set("relay/fanMain",  fanMainOn ? "ON":"OFF");
  json.set("relay/fan3",     fan3On    ? "ON":"OFF");
  json.set("relay/fan4",     fan4On    ? "ON":"OFF");
  json.set("relay/heater",   heaterOn  ? "ON":"OFF");
  xSemaphoreTake(servoStatusMutex, pdMS_TO_TICKS(50));
  String _ss = servoStatus;
  xSemaphoreGive(servoStatusMutex);
  json.set("servo/status", _ss);
  json.set("servo/position", currentPos);
  json.set("servo/holdMs",   (int)servoHoldMs);
  json.set("online",         true);
  json.set("sensorFailed",   sensorFailed);
  Firebase.RTDB.setJSON(&fbdo, "/incubator/current", &json);
}

// ===== Hourly Log =====
long long getNTPTime() {
  struct tm ti; if (!getLocalTime(&ti)) return 0;
  return (long long)mktime(&ti) * 1000LL;
}

void sendHourlyLog() {
  if (!Firebase.ready()) return;
  long long ts = getNTPTime();
  if (ts < 1577836800000LL) { Serial.println("[WARN] NTP fail"); return; }
  FirebaseJson json;
  json.set("temp",          latestTemp);
  json.set("humidity",      latestHum);
  json.set("fog",           fogOn    ? "ON":"OFF");
  json.set("fan3",          fan3On   ? "ON":"OFF");
  json.set("heater",        heaterOn ? "ON":"OFF");
  xSemaphoreTake(servoStatusMutex, pdMS_TO_TICKS(50));
  String _logSS = servoStatus;
  xSemaphoreGive(servoStatusMutex);
  json.set("servoStatus",   _logSS);
  json.set("servoPosition", currentPos);
  json.set("timestamp",     ts);
  String path = "/incubator/logs/" + String(ts);
  if (Firebase.RTDB.setJSON(&fbdoLog, path.c_str(), &json))
    Serial.println("[LOG] OK");
  else
    Serial.println("[LOG ERR] " + fbdoLog.errorReason());
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  i2cMutex         = xSemaphoreCreateMutex();
  servoStatusMutex = xSemaphoreCreateMutex();
  Wire.begin();

  if (!sht45.begin()) Serial.println("[ERR] SHT45 ไม่พบ");
  else { sht45.setPrecision(SHT4X_HIGH_PRECISION); sht45.setHeater(SHT4X_NO_HEATER); Serial.println("SHT45 OK"); }

  pinMode(RELAY1,OUTPUT); pinMode(RELAY2,OUTPUT); pinMode(RELAY3,OUTPUT);
  pinMode(RELAY4,OUTPUT); pinMode(RELAY5,OUTPUT);
  allRelaysOff();

  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2500);
  myServo.write(CENTER);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  for (int i=0; i<40 && WiFi.status()!=WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  if (WiFi.status() != WL_CONNECTED) { Serial.println("\nWiFi FAIL"); ESP.restart(); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("NTP");
  struct tm ti;
  for (int i=0; i<20 && !getLocalTime(&ti); i++) { delay(500); Serial.print("."); }
  Serial.println(" OK");

  config.api_key = API_KEY; config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL; auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.print("Firebase");
  for (int i=0; i<20 && !Firebase.ready(); i++) { delay(500); Serial.print("."); }
  Serial.println(Firebase.ready() ? " OK" : " TIMEOUT");

  xTaskCreatePinnedToCore(servoTask, "servoTask", 8192, NULL, 3, NULL, 0);

  logTimer = millis() - LOG_INTERVAL;
  Serial.println("=== SYSTEM START ===");
}

// ===== LOOP =====
void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] reconnecting...");
    WiFi.reconnect(); delay(5000); return;
  }

  if (now - controlTimer >= CONTROL_INTERVAL) {
    controlTimer = now;
    readControlFromFirebase();
  }

  static uint8_t stopCount = 0;
  if (systemState == "STOP") {
    stopCount++;
    if (stopCount >= 2 && wasRunning) {
      allRelaysOff();
      servoResetRequest = true;
      wasRunning = false;
      Serial.println("[STOP] ปิด relay + reset servo");
      // LINE alert เมื่อระบบหยุดทำงาน
      sendLineForce("ระบบตู้ฟักไข่หยุดทำงานแล้ว\n");
    }
    if (now - firebaseTimer >= FIREBASE_INTERVAL) {
      firebaseTimer = now;
      sendCurrentData();
    }
    delay(50);
    return;
  }
  stopCount = 0;

  if (!wasRunning) {
    wasRunning = true;
    if (servoState == 9) {
      servoStartRequest = true;
    }
    Serial.println("[RUN] ระบบเริ่มทำงาน");
    // LINE alert เมื่อระบบเริ่มทำงาน
    sendLineForce("ระบบตู้ฟักไข่เริ่มทำงานแล้ว\n"
                  "Servo: 135°→hold→90°→hold→45°→hold→90°→hold→วน\n");
  }

  if (now - shtTimer >= SHT_INTERVAL) {
    shtTimer = now;
    sensors_event_t hev, tev;
    bool ok = false;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      ok = sht45.getEvent(&hev, &tev);
      xSemaphoreGive(i2cMutex);
    }
    float t = tev.temperature, h = hev.relative_humidity;
    if (ok && !isnan(t) && !isnan(h) && t > 0 && t < 60 && h > 0 && h <= 100) {
      latestTemp = t; latestHum = h;
      shtErrorCount = 0; sensorFailed = false;
      if (thresholdReady) {
        controlSystem(t, h);
        String alert = "";
        if (t >= fan_temp)        alert += "อุณหภูมิสูงเกิน: " + String(t,1) + "C\n";
        if (t <= heater_off_temp) alert += "อุณหภูมิต่ำเกิน: " + String(t,1) + "C\n";
        if (h > hum_off + 5.0f)  alert += "ความชื้นสูงเกิน: " + String(h,1) + "%\n";
        if (h < hum_on  - 5.0f)  alert += "ความชื้นต่ำเกิน: " + String(h,1) + "%\n";
        if (alert != "") sendLineAlert(alert);
      } else {
        Serial.println("[WAIT] รอค่าจาก Dashboard...");
      }
      xSemaphoreTake(servoStatusMutex, pdMS_TO_TICKS(50));
      String _dbgSS = servoStatus;
      xSemaphoreGive(servoStatusMutex);
      Serial.printf("[OK] T:%.1f H:%.1f Fog:%s Heat:%s Servo:%s\n",
        t, h, fogOn?"ON":"OFF", heaterOn?"ON":"OFF", _dbgSS.c_str());
    } else {
      shtErrorCount++;
      if (shtErrorCount >= SHT_MAX_ERROR && !sensorFailed) {
        sensorFailed = true;
        allRelaysOff();
        Serial.println("[CRITICAL] Sensor fail");
        sendLineAlert("เซ็นเซอร์ขัดข้อง Relay ปิดทั้งหมดแล้ว\n");
      }
    }
  }

  if (now - firebaseTimer >= FIREBASE_INTERVAL && !lineSending) {
    firebaseTimer = now;
    sendCurrentData();
  }

  if (now - logTimer >= LOG_INTERVAL && latestTemp > 0 && !sensorFailed) {
    logTimer = now;
    sendHourlyLog();
  }

  static unsigned long incubTimer = 0;
  if (endTimeMs > 0 && now - incubTimer >= 30000UL) {
    incubTimer = now;
    long long nowMs = getNTPTime();
    if (nowMs > 0) {
      long long diff = endTimeMs - nowMs;
      if (!alertedOneDay   && diff <= 86400000LL && diff > 0) { alertedOneDay=true;   sendLineAlert("เหลืออีก 1 วัน ตู้จะหยุดทำงาน\n"); }
      if (!alertedThirtyMin&& diff <= 1800000LL  && diff > 0) { alertedThirtyMin=true; sendLineAlert("เหลืออีก 30 นาที ตู้จะหยุดทำงาน\n"); }
      if (!alertedDone     && diff <= 0) {
        alertedDone = true;
        systemState = "STOP";
        allRelaysOff(); resetServo();
        Firebase.RTDB.setString(&fbdo, "/incubator/control/system", "STOP");
        sendLineAlert("ตู้ฟักไข่ครบกำหนดแล้ว หยุดทำงานเรียบร้อย\n");
        Serial.println("[DONE] ครบกำหนด");
      }
    }
  }

  delay(5);
}
