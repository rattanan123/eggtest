# คำแนะนำสำหรับ Claude Code

> ไฟล์นี้ Claude Code อ่านอัตโนมัติเมื่อเปิด repo นี้ — ใช้เป็นบริบทพื้นฐาน

## โปรเจคนี้คืออะไร

ระบบตู้ฟักไข่อัจฉริยะ (Smart Egg Incubator) ทำงานบน ESP32 + Firebase + LINE มี 2 ไฟล์หลัก:

- **`DHT22Egg.ino`** — เฟิร์มแวร์ ESP32 (Arduino C++) ประมาณ 485 บรรทัด
- **`index.html`** — Web Dashboard (JavaScript + Firebase Web SDK + Chart.js) ประมาณ 1,358 บรรทัด

## ฮาร์ดแวร์

- บอร์ด ESP32
- เซ็นเซอร์ SHT45 (I2C) อ่านอุณหภูมิ/ความชื้น
- รีเลย์ 5 ตัว: หมอก (GPIO26), พัดลมหลัก (27), พัดลม 3 (14), พัดลมเสริม (25), ฮีตเตอร์ (33)
- Servo SG90 ที่ GPIO13 สำหรับพลิกไข่

## บริการภายนอก

- **Firebase Realtime Database** (asia-southeast1) — โครงสร้าง `/incubator/{control,current,logs,systemLogs}`
- **LINE Messaging API** — push notification

## ข้อควรระวัง

⚠️ **Credentials ในโค้ดถูก expose บน GitHub แล้ว** — ก่อนทำ feature ใหม่ใด ๆ ให้ทำ Task 1.1 ใน `ROADMAP.md` ก่อนเสมอ:

- เปลี่ยนรหัสผ่านทุกตัว
- ย้าย secrets ไป `secrets.h` + `.gitignore`
- ลบ git history เก่า

## วิธีทำงานกับโปรเจค

อ่าน 2 ไฟล์นี้ตามลำดับเสมอ:

1. **`REVIEW.md`** — สรุปภาพรวม จุดที่ควรปรับ และ priority
2. **`ROADMAP.md`** — task แบ่งเฟส 1-4 พร้อมขั้นตอน

แต่ละ task ใน ROADMAP.md มี **บริบท + ขั้นตอน + ไฟล์ที่ต้องแก้** เขียนไว้แล้ว ใช้สั่งงานต่อได้เลย

## Convention

- เฟิร์มแวร์: ใช้ FreeRTOS task แยกงาน (servo, LINE) จาก main loop
- ใช้ `xSemaphoreTake/Give` สำหรับ shared state (มี `i2cMutex`, `servoStatusMutex`)
- ทุกค่าตั้งค่าอ่านจาก Firebase `/incubator/control` ไม่ใส่ในโค้ด
- LINE alert มี cooldown 10 นาที (ยกเว้น `sendLineForce()` สำหรับ start/stop)
- รีเลย์ทำงาน active-low: `digitalWrite(pin, LOW)` = ON

## วิธี build/upload

- **Arduino IDE / PlatformIO** — เลือกบอร์ด ESP32 Dev Module
- ไลบรารีที่ต้องติดตั้ง: `WiFi`, `Firebase ESP Client` (mobizt), `Adafruit_SHT4x`, `ESP32Servo`
- **หน้าเว็บ** — `index.html` เปิดตรงในเบราว์เซอร์ได้เลย หรือ deploy ผ่าน Firebase Hosting
