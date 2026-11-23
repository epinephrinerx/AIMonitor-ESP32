# AIMonitor → ESP32 serial protocol (v1)

USB serial 115200 8N1, UTF-8, หนึ่งข้อความต่อหนึ่งบรรทัด (`\n`) ยาวไม่เกิน 4096 ไบต์

**ฝั่ง PC ต้องเปิดพอร์ตโดยปิด DTR และ RTS** (`DtrEnable = false`, `RtsEnable = false`)
ไม่เช่นนั้นวงจร auto-reset ของบอร์ดจะรีเซ็ต ESP32 ทุกครั้งที่เปิดพอร์ต

## PC → ESP32

`ping` → ตอบ `pong aimonitor-cyd 1`

Snapshot (ส่งทุกครั้งที่ refresh และส่งซ้ำอย่างน้อยทุก 60 วินาทีเป็น heartbeat):

```json
{"v":1,"now":1764000000,"tz":420,"p":[
  {"id":"claude","name":"Claude","stale":false,"err":"",
   "w":[{"t":"5-hour","pct":42.5,"sev":0,"reset":1764012000,"lock":""}]}
]}
```

| ฟิลด์ | ความหมาย |
|---|---|
| `now` | Unix time (UTC, วินาที) ของ PC ใช้ตั้งนาฬิกาและนับถอยหลัง |
| `tz` | offset เวลาท้องถิ่นเป็นนาที (ไทย = 420) |
| `p[].stale` | `TrayReading.IsStale` — refresh ล่าสุดล้มเหลว ค่าเป็นของรอบก่อน |
| `p[].err` | ข้อความ error สั้น ๆ เมื่อ service ไม่มีข้อมูล (ว่าง = ไม่มี error) |
| `w[].t` | ชื่อสั้นของหน้าต่างโควตา (ไม่เกิน 23 ตัวอักษร) ที่ PC ประกอบให้แยกกันออก — ดูหัวข้อ "ชื่อหน้าต่าง" |
| `w[].pct` | เปอร์เซ็นต์ หรือ `null` เมื่อ service ไม่รายงาน (จอแสดง `--` ไม่เดาค่า) |
| `w[].sev` | `Severity` 0=Normal 1=High 2=VeryHigh 3=Critical |
| `w[].reset` | Unix time ที่หน้าต่างรีเซ็ต หรือ 0 ถ้าไม่ทราบ |
| `w[].lock` | `LockedReason` (ว่าง = ไม่ล็อก) |

จำกัด 6 service × 8 หน้าต่าง ส่วนเกินถูกตัดทิ้ง

### ชื่อหน้าต่าง (`t`)

`MeterDisplayItem.Title` ซ้ำกันได้ (Claude weekly ทุกอันชื่อ "Weekly" ต่างกันที่ Subtitle)
ฝั่ง PC จึงต้องประกอบชื่อสั้นเอง:

| Title / Subtitle จากแอป | `t` ที่ส่ง |
|---|---|
| Session / 5-hour window | `Session` |
| Weekly / All models | `Weekly` |
| Weekly / Fable only | `Weekly Fable` (ตัด " only" แล้วต่อท้าย Title) |
| Weekly / API apps | `Weekly API apps` |

กฎ: ถ้า Subtitle ลงท้ายด้วย " only" ให้ใช้ `Title + " " + ส่วนหน้า`; ถ้าเป็นคำอธิบายทั่วไป
(เช่น "5-hour window", "All models") ใช้ Title อย่างเดียว; กรณีอื่นต่อ Subtitle ท้าย Title

## ESP32 → PC

- ตอนบูต: `hello aimonitor-cyd 1`
- รับ snapshot สำเร็จ: `ok`
- ผิดพลาด: `err <เหตุผล>` (เช่น `err json IncompleteInput`, `err too_long`, `err version`)
