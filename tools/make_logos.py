"""แปลงไฟล์โลโก้เป็น src/logos.h (RGB565 + alpha 8 บิต) สำหรับวาดบนพื้นขาว/ดำของแต่ละ meter

ใช้: python tools/make_logos.py <โฟลเดอร์ assets ของ AIMonitor>
ต้องมี Pillow

- claude: ภาพสีเต็ม ใช้ alpha จากไฟล์ PNG
- codex: ภาพขาวดำ เก็บเป็น mask อย่างเดียว แล้ววาดด้วยสีตัวอักษรของช่อง (ดำบนพื้นขาว ขาวบนพื้นดำ)
- antigravity: JPEG พื้นขาว แยกพื้นขาวออกเป็น alpha
"""
import sys
from pathlib import Path

from PIL import Image

SIZES = (20, 14)
OUT = Path(__file__).resolve().parent.parent / "src" / "logos.h"


def load_rgba(path: Path, white_is_clear: bool) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    if white_is_clear:
        px = img.load()
        for y in range(img.height):
            for x in range(img.width):
                r, g, b, a = px[x, y]
                # ระยะห่างจากสีขาว → ความทึบ; คืนสีเดิมก่อนผสมกับพื้นขาว
                alpha = min(255, (255 - min(r, g, b)) * 3)
                if alpha == 0:
                    px[x, y] = (0, 0, 0, 0)
                    continue
                k = alpha / 255
                un = [max(0, min(255, round((c - 255 * (1 - k)) / k))) for c in (r, g, b)]
                px[x, y] = (*un, min(a, alpha))
    return img


def clear_outer_white(img: Image.Image) -> Image.Image:
    """พื้นขาวรอบไอคอน (ต่อเนื่องจากขอบภาพ) ให้โปร่งใส ส่วนสีขาวในไอคอนคงไว้"""
    px = img.load()
    w, h = img.size
    stack = [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]
    seen = set()
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        r, g, b, a = px[x, y]
        if min(r, g, b) < 200:
            # ขอบ anti-alias: ลด alpha ตามความขาว
            px[x, y] = (r, g, b, min(a, max(0, (255 - min(r, g, b)) * 4)))
            continue
        px[x, y] = (0, 0, 0, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return img


def crop_square(img: Image.Image) -> Image.Image:
    box = img.getchannel("A").getbbox() or (0, 0, img.width, img.height)
    l, t, r, b = box
    side = max(r - l, b - t)
    cx, cy = (l + r) // 2, (t + b) // 2
    return img.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))


def mask_from_dark(path: Path) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            dark = 255 - (r + g + b) // 3
            px[x, y] = (0, 0, 0, a * dark // 255)
    return img


def emit(name: str, img: Image.Image, mono: bool, lines: list[str]) -> None:
    for size in SIZES:
        small = img.resize((size, size), Image.LANCZOS)
        alpha, rgb565 = [], []
        for r, g, b, a in small.get_flattened_data():
            alpha.append(a)
            rgb565.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        lines.append(f"const uint8_t LOGO_{name}_{size}_A[{size * size}] PROGMEM = {{")
        lines.extend("  " + ",".join(str(v) for v in alpha[i:i + 24]) + "," for i in range(0, len(alpha), 24))
        lines.append("};")
        if not mono:
            lines.append(f"const uint16_t LOGO_{name}_{size}_C[{size * size}] PROGMEM = {{")
            lines.extend("  " + ",".join(f"0x{v:04x}" for v in rgb565[i:i + 12]) + "," for i in range(0, len(rgb565), 12))
            lines.append("};")


def main() -> None:
    assets = Path(sys.argv[1])
    lines = [
        "// สร้างโดย tools/make_logos.py — อย่าแก้มือ",
        "// โลโก้เป็นเครื่องหมายการค้าของเจ้าของแต่ละราย ใช้เพื่อระบุบริการที่แสดงเท่านั้น",
        "#pragma once",
        "#include <Arduino.h>",
        "",
    ]
    emit("CLAUDE", crop_square(clear_outer_white(load_rgba(assets / "claude Icon.png", False))), False, lines)
    emit("CODEX", crop_square(mask_from_dark(assets / "codex.png")), True, lines)
    emit("AGY", crop_square(load_rgba(assets / "antigravity.jpeg", True)), False, lines)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
