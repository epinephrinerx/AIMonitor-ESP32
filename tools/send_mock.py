"""ส่ง snapshot จำลองไปยังจอ ESP32 ตาม docs/PROTOCOL.md เพื่อทดสอบโดยไม่ต้องใช้แอป AIMonitor

ใช้: python tools/send_mock.py [--port COM5] [--interval 5] [--once]
ต้องมี pyserial (มีอยู่แล้วใน Python ของ PlatformIO)
"""
import argparse
import json
import math
import time

import serial


def build_frame(tick: int) -> dict:
    now = int(time.time())
    tz = -time.timezone // 60 if time.localtime().tm_isdst == 0 else -time.altzone // 60

    def wave(base: float, amp: float, period: int) -> float:
        return round(max(0.0, base + amp * math.sin(tick * 2 * math.pi / period)), 1)

    return {
        "v": 1,
        "now": now,
        "tz": tz,
        "p": [
            {
                "id": "claude", "name": "Claude", "stale": False, "err": "",
                "w": [
                    # ชื่อตาม ClaudeQuotaParser: Session, Weekly (All models), Weekly <model> only
                    {"t": "Session", "pct": wave(60, 35, 20), "sev": 0, "reset": now + 2 * 3600 + 14 * 60, "lock": ""},
                    {"t": "Weekly", "pct": 38.0, "sev": 0, "reset": now + 3 * 86400 + 5 * 3600, "lock": ""},
                    {"t": "Weekly Fable", "pct": 81.0, "sev": 0, "reset": now + 3 * 86400 + 5 * 3600, "lock": ""},
                ],
            },
            {
                "id": "openai", "name": "OpenAI / Codex", "stale": tick % 12 >= 9, "err": "",
                "w": [
                    # ชื่อตาม CodexQuotaMapper: Session (primary), Weekly (secondary 7 วัน)
                    {"t": "Session", "pct": 12.0, "sev": 0, "reset": now + 4 * 3600 + 2 * 60, "lock": ""},
                    {"t": "Weekly", "pct": None, "sev": 0, "reset": 0, "lock": ""},
                ],
            },
            {
                "id": "agy", "name": "Gemini / AGY", "stale": False, "err": "",
                "w": [
                    # ชื่อตาม AgyUsageMapper: "<กลุ่ม> · Session/Weekly" ส่งมาเป็น "<กลุ่ม> Session/Weekly"
                    {"t": "Gemini Session", "pct": 95.0, "sev": 3, "reset": now + 25 * 60, "lock": ""},
                    {"t": "Claude / GPT Session", "pct": 47.0, "sev": 0, "reset": now + 3 * 3600, "lock": ""},
                    {"t": "Gemini Weekly", "pct": 100.0, "sev": 3, "reset": now + 2 * 86400, "lock": "limit reached"},
                ],
            },
        ],
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--interval", type=float, default=5.0)
    ap.add_argument("--once", action="store_true")
    args = ap.parse_args()

    # ปิด DTR/RTS ก่อนเปิดพอร์ต ไม่เช่นนั้นบอร์ดจะรีเซ็ต
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = args.port, 115200, 1
    s.dtr = False
    s.rts = False
    s.open()

    tick = 0
    try:
        while True:
            payload = json.dumps(build_frame(tick), separators=(",", ":"))
            s.write(payload.encode() + b"\n")
            reply = s.readline().decode(errors="replace").strip()
            print(f"[{tick}] {len(payload)} bytes -> {reply or '(no reply)'}")
            if args.once:
                break
            tick += 1
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


if __name__ == "__main__":
    main()
