# Roadmap — แผนพัฒนาตู้ฟักไข่อัจฉริยะ

เอกสารนี้ออกแบบมาให้ใช้กับ Claude Code โดยตรง แต่ละ task มีบริบทพอที่จะสั่งงานต่อได้เลย

---

## เฟส 1 — งานเร่งด่วน (1–2 สัปดาห์)

### Task 1.1 — เพิ่มความปลอดภัย (Security Hardening)

**บริบท:** ปัจจุบัน `DHT22Egg.ino` มี `WIFI_PASSWORD`, `USER_PASSWORD`, `LINE_TOKEN`, `API_KEY` hardcode อยู่ในไฟล์และ public บน GitHub

**ขั้นตอน:**

1. สร้างไฟล์ `secrets.h` ใส่ `.gitignore`
   ```cpp
   // secrets.h
   #pragma once
   #define WIFI_SSID "..."
   #define WIFI_PASSWORD "..."
   #define USER_EMAIL "..."
   #define USER_PASSWORD "..."
   #define LINE_TOKEN "..."
   #define LINE_USER "..."
   ```
2. แก้ `DHT22Egg.ino` ให้ `#include "secrets.h"` แทน `#define` ตรง ๆ
3. เปลี่ยนรหัสผ่านทุกตัวที่ Firebase console + LINE Developer console + WiFi router
4. ลบ secrets จาก git history: `git filter-repo --replace-text expressions.txt`
5. ตั้ง Firebase Database Rules:
   ```json
   {
     "rules": {
       "incubator": {
         ".read": "auth != null && auth.uid == 'YOUR_UID'",
         ".write": "auth != null && auth.uid == 'YOUR_UID'"
       }
     }
   }
   ```

**ไฟล์ที่แก้:** `DHT22Egg.ino`, สร้างใหม่ `secrets.h`, `.gitignore`, `database.rules.json`

---

### Task 1.2 — เพิ่ม WiFiManager + ตั้งค่าผ่านหน้าเว็บ

**บริบท:** ตอนนี้ WiFi credentials ฝังในโค้ด ทำให้เปลี่ยน WiFi ต้องอัปโหลดโค้ดใหม่

**ขั้นตอน:**

1. ติดตั้งไลบรารี `WiFiManager` (tzapu)
2. แทน `WiFi.begin(WIFI_SSID, WIFI_PASSWORD)` ด้วย:
   ```cpp
   WiFiManager wm;
   wm.setConfigPortalTimeout(180);
   if (!wm.autoConnect("EggIncubator-Setup")) ESP.restart();
   ```
3. (Optional) เพิ่ม custom parameters เพื่อกรอก LINE token, Firebase user/pass ผ่านหน้าเว็บด้วย

**ไฟล์ที่แก้:** `DHT22Egg.ino`

---

### Task 1.3 — เพิ่ม Watchdog Timer + OTA Update

**บริบท:** ระบบไม่มี watchdog หากค้างจะไม่รีเซ็ตอัตโนมัติ และไม่รองรับ OTA

**ขั้นตอน:**

1. เพิ่ม watchdog ใน `setup()`:
   ```cpp
   #include <esp_task_wdt.h>
   esp_task_wdt_init(30, true); // 30 วินาที
   esp_task_wdt_add(NULL);
   ```
2. เรียก `esp_task_wdt_reset()` ในลูปหลักทุกรอบ
3. เพิ่ม ArduinoOTA:
   ```cpp
   #include <ArduinoOTA.h>
   ArduinoOTA.setHostname("egg-incubator");
   ArduinoOTA.setPassword("strong-pwd");
   ArduinoOTA.begin();
   // ใน loop()
   ArduinoOTA.handle();
   ```

**ไฟล์ที่แก้:** `DHT22Egg.ino`

---

### Task 1.4 — เพิ่มเซ็นเซอร์ตัวสำรอง

**บริบท:** ปัจจุบันถ้า SHT45 ตัวเดียวเสีย → `allRelaysOff()` → ไข่ตายหมด

**ขั้นตอน:**

1. เพิ่ม SHT45 ตัวที่สอง (ตั้ง I2C address ที่ขา ADDR ให้ต่างกัน) หรือใช้ DHT22/BME280 เป็นตัวสำรอง
2. แก้ฟังก์ชัน sensor read:
   - อ่านทั้งสองตัว → หา avg
   - ถ้าตัวหลักพัง → fallback ตัวรอง + แจ้ง LINE
   - ถ้าค่าต่างกัน > 1°C → แจ้งเตือนแต่ยังทำงานต่อด้วยค่า avg
3. แก้ logic `sensorFailed` ให้ตั้งจริงเมื่อทั้งสองตัวพัง

**ไฟล์ที่แก้:** `DHT22Egg.ino`

---

## เฟส 2 — ฟีเจอร์หลักของการฟัก (2–4 สัปดาห์)

### Task 2.1 — โปรไฟล์การฟักแบบหลายช่วง

**บริบท:** ปัจจุบันใช้ `tempMin/tempMax/humMin/humMax` ค่าเดียวตลอด แต่ระยะ Lockdown ต้องเปลี่ยนค่าและหยุดพลิก

**Schema ใหม่บน Firebase:**

```json
/incubator/profiles/chicken: {
  "stages": [
    { "name": "Incubation", "dayStart": 1, "dayEnd": 18,
      "tempMin": 37.3, "tempMax": 37.7, "humMin": 50, "humMax": 60,
      "servoHoldHours": 2, "turning": true },
    { "name": "Lockdown", "dayStart": 19, "dayEnd": 21,
      "tempMin": 37.0, "tempMax": 37.4, "humMin": 65, "humMax": 75,
      "turning": false }
  ],
  "totalDays": 21
}
```

**ฝั่งเฟิร์มแวร์:**

1. ใน `readControlFromFirebase()` อ่าน profile ที่ active
2. คำนวณ `currentDay = (now - startTime) / 86400000`
3. หา stage ที่ `dayStart <= currentDay <= dayEnd`
4. ใช้ค่าจาก stage นั้นแทนค่ากลาง

**ฝั่ง Dashboard:**

1. เพิ่ม dropdown เลือก profile (ไก่ / เป็ด / นกกระทา / กำหนดเอง)
2. แสดง progress bar: วันที่ X จาก Y วัน
3. แสดงข้อความว่าตอนนี้อยู่ stage ไหน เหลือกี่วันก่อนเข้า Lockdown
4. หน้าจัดการ profile (เพิ่ม/แก้/ลบ stage)

**ไฟล์ที่แก้:** `DHT22Egg.ino`, `index.html`

---

### Task 2.2 — หยุดพลิกไข่อัตโนมัติช่วง Lockdown

**บริบท:** ต่อยอดจาก Task 2.1

**ขั้นตอน:**

1. ตรวจค่า `turning` จาก stage ปัจจุบัน
2. ถ้า `turning == false` ให้ตั้ง `servoResetRequest = true` และเก็บ servo ที่ 90°
3. แสดงสถานะ "หยุดพลิก (Lockdown)" ใน Dashboard

**ไฟล์ที่แก้:** `DHT22Egg.ino`

---

### Task 2.3 — บันทึกจำนวนไข่และอัตราการฟัก

**บริบท:** ตอนนี้ไม่บันทึกผลรอบฟัก

**Schema:**

```json
/incubator/runs/{startTimeMs}: {
  "startTime": 1234567890,
  "endTime": 1234567890,
  "profile": "chicken",
  "eggCount": 30,
  "hatchCount": 25,
  "badCount": 5,
  "hatchRate": 83.3,
  "avgTemp": 37.5,
  "avgHum": 56.2,
  "tempStdDev": 0.3,
  "excursions": [{ "time": ..., "type": "tempHigh", "value": 38.2 }],
  "notes": "..."
}
```

**ขั้นตอน:**

1. หน้า Start: เพิ่มฟิลด์จำนวนไข่ ชนิดไข่ แหล่งที่มา → บันทึกลง `/incubator/runs/`
2. หน้า End (เมื่อ `alertedDone == true`): แสดง modal ให้กรอกจำนวนไข่ที่ฟัก
3. คำนวณค่าสรุปจาก logs ของรอบนั้น
4. หน้า "ประวัติการฟัก" ใน Dashboard เปรียบเทียบทุกรอบ

**ไฟล์ที่แก้:** `index.html` (เพิ่ม view), อาจมี Cloud Function aggregate

---

### Task 2.4 — กราฟครอบคลุมทั้งรอบ + Export CSV

**บริบท:** ปัจจุบันกราฟแสดงแค่ 24 ชม. ดูรอบฟัก 21 วันไม่ได้

**ขั้นตอน:**

1. เพิ่ม `chartjs-plugin-zoom` ผ่าน CDN
2. เพิ่มปุ่มสลับช่วงเวลา: 1 ชม. / 24 ชม. / 7 วัน / ทั้งรอบ
3. ปรับ logging ในเฟิร์มแวร์ให้มีหลาย granularity:
   - ทุก 1 นาที — เก็บ 1 วัน
   - ทุก 1 ชม. — เก็บ 30 วัน (เดิม)
   - ทุก 1 วัน — เก็บถาวร
4. เพิ่มปุ่ม "Export CSV" ใน Dashboard:
   ```javascript
   const csv = entries.map(([id, v]) => `${v.timestamp},${v.temp},${v.humidity},${v.fog},${v.heater}`).join('\n');
   const blob = new Blob([csv], { type: 'text/csv' });
   const url = URL.createObjectURL(blob);
   ```
5. Cloud Function cleanup log เก่าอัตโนมัติ

**ไฟล์ที่แก้:** `index.html`, `DHT22Egg.ino`, สร้างใหม่ `functions/cleanup.js`

---

## เฟส 3 — ปรับปรุงการใช้งาน (2–3 สัปดาห์)

### Task 3.1 — PWA + Push Notification (FCM)

**บริบท:** Dashboard ติดตั้งบนมือถือเป็นแอปไม่ได้ และ LINE token เปราะบาง

**ขั้นตอน:**

1. สร้าง `manifest.json`:
   ```json
   {
     "name": "Egg Incubator",
     "short_name": "Egg",
     "start_url": "/",
     "display": "standalone",
     "background_color": "#ffffff",
     "theme_color": "#16a34a",
     "icons": [...]
   }
   ```
2. สร้าง `service-worker.js` สำหรับ cache + handle push event
3. เพิ่ม Firebase Cloud Messaging:
   ```javascript
   import { getMessaging, getToken } from "firebase/messaging";
   const messaging = getMessaging(app);
   const token = await getToken(messaging, { vapidKey: '...' });
   await set(ref(db, `incubator/fcmTokens/${token}`), true);
   ```
4. Cloud Function ส่ง notification เมื่อ `/incubator/alerts/` ถูกเขียน

**ไฟล์ที่แก้:** `index.html`, สร้างใหม่ `manifest.json`, `service-worker.js`, `functions/notify.js`

---

### Task 3.2 — แจ้งเตือนวันส่องไข่ + บันทึกรูป

**บริบท:** การฟักไก่ส่องไข่วัน 7, 14, 18

**ขั้นตอน:**

1. เพิ่ม `candlingDays: [7, 14, 18]` ใน profile
2. เฟิร์มแวร์ตรวจทุก hourly tick ถ้าวันใหม่ตรงกับ candling day → `sendLineAlert("ส่องไข่วันนี้")`
3. Dashboard เพิ่มหน้า "ภาพการส่องไข่" upload รูปไป Firebase Storage
4. (ขั้นสูง) เพิ่ม ESP32-CAM ถ่ายอัตโนมัติ

---

### Task 3.3 — Hardware Add-ons

| อุปกรณ์ | จุดประสงค์ | ขา/Pin |
|---|---|---|
| Water level sensor | แจ้งเติมน้ำหมอก | ADC pin |
| Reed switch ฝาตู้ | แจ้งเปิดฝา > 30 วินาที | Digital pin |
| INA219 + แบตเตอรี่ | ตรวจไฟดับ | I2C |
| DS18B20 | วัดอุณหภูมิน้ำ | 1-Wire |
| RTC DS3231 | เวลาสำรอง | I2C |
| Buzzer + LED RGB | feedback ทางกายภาพ | Digital + PWM |

---

### Task 3.4 — PID + Auto-tune

**บริบท:** อุณหภูมิแกว่งจาก bang-bang control

**ขั้นตอน:**

1. ติดตั้ง `QuickPID`
2. ต่อ SSR (Solid State Relay) แทน relay เดิมสำหรับฮีตเตอร์ — รองรับ PWM
3. เขียน control loop:
   ```cpp
   QuickPID heaterPID(&tempInput, &heaterOutput, &setpoint, Kp, Ki, Kd, QuickPID::Action::direct);
   heaterPID.SetOutputLimits(0, 255);
   heaterPID.SetSampleTimeUs(1000000); // 1 second
   heaterPID.Compute();
   // slow PWM at 1Hz กับ SSR
   ```
4. Auto-tune ครั้งแรกด้วย `PID_AutoTune_v0`
5. เก็บค่า PID ใน Firebase ปรับจาก Dashboard ได้

---

## เฟส 4 — ฟีเจอร์ขั้นสูง (Optional)

- **ESP32-CAM** stream ผ่าน WebSocket + ถ่ายภาพการพัฒนาของลูกไก่อัตโนมัติ
- **Anomaly Detection** — Cloud Function วิเคราะห์ pattern อุณหภูมิ/ความชื้น แจ้งเตือนเมื่อผิดปกติ
- **Voice Assistant** — Google Home / Alexa สั่งงานด้วยเสียง
- **Multi-incubator dashboard** — ฟาร์มที่มีหลายตู้ ดูจอเดียวกัน

---

## คำแนะนำสำหรับ Claude Code

หากใช้ Claude Code ทำ task ในไฟล์นี้ แนะนำให้:

1. เริ่มจาก **Task 1.1 (ความปลอดภัย)** ก่อนเสมอ — อย่าเริ่ม feature ใหม่ขณะ secrets ยัง public
2. ทำทีละ task ให้ครบ แล้ว commit แยกแต่ละ task
3. แต่ละ task ใช้ branch ใหม่: `feature/1.1-security-hardening` เป็นต้น
4. หลังจบ Task 2.1 ให้รัน integration test กับฮาร์ดแวร์จริง อย่างน้อย 1 รอบฟัก (เพราะ logic ควบคุมเปลี่ยน)
