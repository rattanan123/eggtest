# ขั้นตอนอัปโหลดไฟล์ขึ้น GitHub

ไฟล์ที่จะอัปโหลด 3 ไฟล์ในโฟลเดอร์ `docs/`:
- `REVIEW.md` — สรุปภาพรวม
- `ROADMAP.md` — แผนพัฒนา 4 เฟส
- `CLAUDE.md` — บริบทสำหรับ Claude Code

## ขั้นตอน (Windows PowerShell หรือ Command Prompt)

```powershell
# 1) ไปยังโฟลเดอร์โปรเจคของคุณ
cd C:\path\to\your\eggtest

# 2) ดึงโค้ดล่าสุดมาก่อน
git pull

# 3) สร้างโฟลเดอร์ docs (ถ้ายังไม่มี)
mkdir docs

# 4) คัดลอกไฟล์ทั้ง 3 ตัวเข้าโฟลเดอร์ docs
#    (เปิด File Explorer ไปที่ outputs folder ของ Cowork แล้ว copy/paste)

# 5) ตรวจดูว่ามีไฟล์ครบ
git status

# 6) เพิ่มไฟล์เข้า staging
git add docs/REVIEW.md docs/ROADMAP.md docs/CLAUDE.md

# 7) commit
git commit -m "docs: เพิ่มรายงานวิเคราะห์และ roadmap สำหรับการพัฒนา

- REVIEW.md: สรุปภาพรวมระบบและจุดที่ควรปรับปรุง
- ROADMAP.md: แผนพัฒนา 4 เฟส 15 ฟีเจอร์ พร้อมขั้นตอน
- CLAUDE.md: บริบทพื้นฐานสำหรับ Claude Code"

# 8) push ขึ้น GitHub
git push
```

## ถ้าอยากให้ Claude Code อ่าน CLAUDE.md เป็นค่าตั้งต้น

GitHub Copilot/Claude Code จะมองหาไฟล์ `CLAUDE.md` ที่ **root ของ repo** เป็นหลัก ไม่ใช่ใน `docs/` ดังนั้นแนะนำให้:

```powershell
# วาง CLAUDE.md ที่ root, ส่วน REVIEW/ROADMAP ไว้ใน docs/
copy outputs\docs\CLAUDE.md CLAUDE.md
copy outputs\docs\REVIEW.md docs\REVIEW.md
copy outputs\docs\ROADMAP.md docs\ROADMAP.md

git add CLAUDE.md docs/REVIEW.md docs/ROADMAP.md
git commit -m "docs: add review, roadmap, and Claude Code context"
git push
```

## ⚠️ ก่อน push อย่าลืม

จากที่ `REVIEW.md` แจ้งไว้ — credentials ในโค้ดถูก expose แล้ว ก่อนทำงานต่อในโปรเจค:

1. **เปลี่ยนรหัสผ่าน** ทุกตัวก่อน:
   - WiFi router password
   - Firebase Authentication user password
   - LINE Messaging API channel token (regenerate)

2. **ลบ secrets เก่าออกจาก git history** (ทำหลัง push docs ก็ได้):
   ```bash
   # ติดตั้ง git-filter-repo ก่อน: pip install git-filter-repo
   git filter-repo --replace-text expressions.txt
   ```
   ไฟล์ `expressions.txt`:
   ```
   yao071256==>REMOVED
   rattanan1230z==>REMOVED
   AIzaSyCnBjSbD4zZpc-t0QfK7i5PlfQfuSgdyRw==>REMOVED
   +djF1vzQA1GT7PrnhY+HjiqLwIbbO9CxncPhtovCgSMcxHjuNW1Rpax9fSHeoq/Tmypoa+RxcrFbwAStWHYjuekXMMUwNUX32Sw3LCxbjwFKsQca9xMoqpk6bxTfrFSxllMCwI1PI2nKYrqI+FWHMwdB04t89/1O/w1cDnyilFU===>REMOVED
   ```
   แล้ว force push:
   ```bash
   git push --force origin main
   ```
