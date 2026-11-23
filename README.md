# AIMonitor ESP32 desk display

Firmware that turns an **ESP32-2432S028 "Cheap Yellow Display"** (2.8" 320×240 touch screen) into a desk display for
[AIMonitor](https://github.com/epinephrinerx/AIMonitor) (2.0.5 or later). The PC app keeps collecting quota for Claude,
OpenAI / Codex and AGY; the board shows the same readings as the tray, one service per page, as gauges.

The board never talks to any AI service and holds no credentials. It only receives service and window names,
percentages, severity and reset times over USB serial.

## What it shows

- One page per service with a gauge per quota window (260° arc, colour and word for severity: Normal, High,
  Very high, Critical), the window name and a countdown to the reset.
- `--` when a service reports no percentage (never an invented value), `Locked` for a locked window, grey `Stale`
  when the PC's last refresh of that service failed.
- Clock and page dots in the header; "no update Nm" when the PC has been silent for 3 minutes.
- The RGB LED on the back: off when all is normal, yellow for High/Very high, red for Critical.

## Controls

| Action | Result |
|---|---|
| Tap anywhere | Next service (pages also turn by themselves every 10 s) |
| Tap ⟳ beside the clock | Rotate the screen 90° (portrait or landscape; remembered across power cycles) |
| Hold 2 s anywhere | Rotate as well |

## Hardware

Tested on the two-USB variant (USB-C + micro-USB) with an **ILI9341** panel and resistive **XPT2046** touch:

| Part | Pins |
|---|---|
| Display (HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, backlight 21 |
| Touch (VSPI) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| RGB LED (active low) | 4, 16, 17 |

Some two-USB boards ship with an ST7789 panel instead; change `-DILI9341_DRIVER` in `platformio.ini` if the
screen stays blank.

## Install

**Prebuilt (release):** download `aimonitor-cyd-v0.1.0.bin` from the release and flash it at offset 0:

```
python -m esptool --port COM5 write-flash 0 aimonitor-cyd-v0.1.0.bin
```

**From source** with [PlatformIO](https://platformio.org/): set `upload_port` in `platformio.ini`, then

```
pio run -t upload
```

Then in AIMonitor open **Settings › ESP32 desk display**, tick *Send quota to an ESP32 display over USB*
and pick the port (or *Find automatically*).

## Protocol

Newline-delimited JSON at 115200 8N1; see [`docs/PROTOCOL.md`](docs/PROTOCOL.md). The host must open the port with
DTR and RTS off, otherwise the board's auto-reset circuit reboots it.

`tools/send_mock.py` sends synthetic frames so the display can be tried without the PC app:

```
python tools/send_mock.py --port COM5 --interval 5
```

## Layout

```
platformio.ini     board, TFT_eSPI configuration, libraries
src/main.cpp       serial input, touch, rotation, LED
src/model.*        frame parsing and severity rule (same thresholds as the PC app: 75% / 90%)
src/ui.*           header, gauges, layout for both orientations
docs/PROTOCOL.md   PC ↔ display protocol v1
tools/send_mock.py synthetic frames for testing
```

## Licence

GPL-3.0-or-later, the same as AIMonitor. See `LICENSE`.
