# AIMonitor ESP32 desk display

Firmware that turns an **ESP32-2432S028 "Cheap Yellow Display"** (2.8" 320×240 touch screen) into a desk display for
[AIMonitor](https://github.com/epinephrinerx/AIMonitor) (2.0.5 or later). The PC app keeps collecting quota for Claude,
OpenAI / Codex and AGY; the board shows the same readings as the tray as gauges, one page per kind of quota.

The board never talks to any AI service and holds no credentials. It only receives service and window names,
percentages, severity and reset times over USB serial.

## What it shows

- **Session** page: the 5-hour session of every service side by side (Claude, Codex, Antigravity).
  **Weekly** page: the weekly quota of every service. Quota of any other kind goes on an **Other** page.
- One gauge per service on each page: the window named exactly like the page ("Session", "Weekly"), otherwise the
  service's first window of that kind (Antigravity: its first model group, shown in small type under the name).
  Model-only windows such as Weekly Fable are left out.
- Each service sits in its own frame: a 260° arc, colour and word for severity (Normal, High, Very high, Critical),
  the service logo and name (same type size for every service on the page), a countdown to the reset and a bar
  that empties as the reset approaches (5 hours for Session, 7 days for Weekly).
- Countdown: always `2h 05m 09s` on the Session page; `3d 04h 12m` on the Weekly page.
- Light and dark theme: the screen background is one colour, the frames and the header bar the opposite colour.
- `--` when a service reports no percentage (never an invented value), `Locked` for a locked window, grey `Stale`
  when the PC's last refresh of that service failed. A service with no data at all shows `--` and the reason on
  every page.
- Page name, clock and page dots in the header; "no update Nm" when the PC has been silent for 3 minutes.
- The RGB LED on the back: off when all is normal, yellow for High/Very high, red for Critical.

## Controls

| Action | Result |
|---|---|
| Tap anywhere | Next page (pages also turn by themselves every 10 s) |
| Tap ⟳ beside the clock | Rotate the screen 90° (portrait or landscape; remembered across power cycles) |
| Tap ◐ left of ⟳ | Switch between the light and dark theme (remembered across power cycles) |
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
src/ui.*           header, gauges, layout for both orientations, themes
src/logos.h        service logos (generated)
docs/PROTOCOL.md   PC ↔ display protocol v1
tools/send_mock.py synthetic frames for testing
tools/make_logos.py regenerates src/logos.h from the AIMonitor logo files
```

## Licence

GPL-3.0-or-later, the same as AIMonitor. See `LICENSE`.

The Claude, Codex and Antigravity logos in `src/logos.h` are trademarks of their owners and are shown only to
identify each service; they are not covered by the GPL.
