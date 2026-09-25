[🇧🇷 Português](ARCHITECTURE.md) | 🇺🇸 English

# ARCHITECTURE.md — Verified current state (post-stage-5 baseline)

> This document describes the firmware **exactly as it exists right now**, with
> no code changes made during this audit. Every fact here is
> verified by direct reading of this project's source code, of the installed
> Arduino-ESP32 core (`~/.platformio/packages/framework-arduinoespressif32`)
> and/or of `compile_commands.json` (macros actually used in the last
> compilation) — never by assumption or by trusting the Windows screen alone
> (which can have stale driver cache, as we've seen before).
>
> Generated on 2026-09-15. If the code changes after this date, this document
> may become outdated — treat it as a snapshot, not a live source.

---

## 1. How the device shows up in Windows

When connected via USB, Windows enumerates **a single composite USB device**
with two functions (interfaces), recognized by two different drivers:

- **`joy.cpl` (Game controllers)**: shows up as `ESP32S3-SimHub-ButtonBox`,
  status OK, with **32 buttons** and **4 axes** (X, Y, Z, Z-Rotation, X-Rotation,
  Y-Rotation — 6 axes declared in the descriptor, but `main.cpp` only uses the
  button bitmask; axes always stay at 0). Button 32 blinks on its own
  The Button 32 is free — the heartbeat that used to blink on it was removed
  (see item 9).
- **COM port (Device Manager → Ports)**: shows up with the same
  product name, assigned by Windows' standard serial driver (`usbser.sys`),
  usable from any serial terminal (VS Code Serial Monitor, PuTTY, etc.) at
  115200 baud.

Confirmed by a real screenshot from the user (joy.cpl with 32 dots, button
32 lit) and by a Serial Monitor session responding to text commands.

## 2. VID/PID

- **VID = `0x303A`** (Espressif Systems) — fixed, comes from the core (`USB_ESPRESSIF_VID`
  in `esp32-hal-tinyusb.h`), never overridden in this project.
- **PID = `0x1001`** — comes from `variants/esp32s3/pins_arduino.h`
  (`#define USB_PID 0x1001`), which is included **before** the fallback in
  `USB.cpp` (`#ifndef USB_PID #define USB_PID 0x0002`). Since this project's
  board.json uses `"variant": "esp32s3"`, the variant's define wins and the
  app enumerates as **`303A:1001`**.

  > **CORRECTION (2026-09-16).** The previous version of this document claimed
  > the PID was `0x0002` and that any `303A:1001` seen in a `device
  > list` would be "the separate USB-Serial-JTAG interface". **Both
  > claims were wrong.** The mistake came from checking only
  > `compile_commands.json` (where indeed there's no `-DUSB_PID`) and concluding
  > from that, without checking the variant's header. Practical consequence:
  > seeing `303A:1001` on a port does **not** mean the board is in
  > bootloader mode — it's the app running normally.

## 3. Manufacturer

`SimRacing_DIY` — explicitly defined in `platformio.ini`:
```
-DUSB_MANUFACTURER=\"SimRacing_DIY\"
```
Without this flag, the core would use the default `"Espressif Systems"`.

## 4. Product

`ESP32S3-SimHub-ButtonBox` — explicitly defined in `platformio.ini`:
```
-DUSB_PRODUCT=\"ESP32S3-SimHub-ButtonBox\"
```
Without this flag, the core would default to the `ARDUINO_BOARD` macro, which
PlatformIO automatically injects from the `"name"` field in
`boards/esp32-s3-fh4r2.json` (`"ESP32-S3-FH4R2 (Dingyimei S3 Zero)"`) — this was
exactly the leak that caused the odd name in `joy.cpl` before this
fix (already applied and confirmed, part of the current baseline).

## 5. Serial Number

Not fixed in code — it's **automatically derived from the chip's MAC address**
at runtime. Mechanism (`USB.cpp`, `ESPUSB::begin()`):
```
USB_SERIAL default = "__MAC__"
  -> if (serial_number == "__MAC__"): reads esp_efuse_mac_get_default(),
     formats as "%02X%02X%02X%02X%02X%02X" (12 uppercase hex chars)
```
For the board used in testing (MAC `AC:27:6E:CC:FA:B8`), the observed serial is
`AC276ECCFAB8` — matches the formula above exactly.

`platformio.ini` also uses this MAC (`custom_expected_mac`,
`custom_board_mac`) for the port auto-detection scripts — see items 12
and 15.

## 6. USB Interfaces

Composite TinyUSB device with `ARDUINO_USB_MODE=0` (native USB-OTG) and
`ARDUINO_USB_CDC_ON_BOOT=1`:

- **HID interface** — HID class, one interrupt IN endpoint. Gamepad
  reports (see item 7).
- **CDC ACM interface** (2 sub-interfaces: Control + Data, like any standard
  TinyUSB CDC) — virtual serial port, used for text commands and
  logs (see item 10).

There's no hand-written composite descriptor in this project (unlike
the earlier STM32 project, where `usb_descriptors.c` assembled everything manually) —
here the Arduino-ESP32 core assembles the descriptor automatically based on which
classes (`USB.begin()`, `USBHIDGamepad`, `USBCDC` via `ARDUINO_USB_CDC_ON_BOOT`)
get initialized in `setup()`.

## 7. HID report descriptor

Comes ready-made from the Arduino-ESP32 core (`libraries/USB/src/USBHIDGamepad.cpp`), via
TinyUSB's standard template:
```c
static const uint8_t report_descriptor[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(HID_REPORT_ID_GAMEPAD))
};
```
Not a single byte of this descriptor is customized in this project — it's TinyUSB's
standard `TUD_HID_REPORT_DESC_GAMEPAD` template (`class/hid/hid_device.h`), with
**Report ID = 3** (`HID_REPORT_ID_GAMEPAD`, the 4th value in the enum
`HID_REPORT_ID_NONE=0, KEYBOARD=1, MOUSE=2, GAMEPAD=3` in `USBHID.h`).

Report layout (`hid_gamepad_report_t` struct, 8 bytes + 1 Report ID byte):

| Field     | Size    | Type   | Use in firmware               |
|-----------|---------|--------|--------------------------------|
| Report ID | 1 byte  | —      | always `3`, implicit           |
| X         | 1 byte  | int8   | always `0` (unused)            |
| Y         | 1 byte  | int8   | always `0` (unused)            |
| Z         | 1 byte  | int8   | always `0` (unused)            |
| Rz        | 1 byte  | int8   | always `0` (unused)            |
| Rx        | 1 byte  | int8   | always `0` (unused)            |
| Ry        | 1 byte  | int8   | always `0` (unused)            |
| Hat/DPad  | 1 byte  | uint8  | always `0` (centered, unused)  |
| Buttons   | 4 bytes | uint32 | bitmask, bit N = button N+1    |

`main.cpp` only uses the last field:
```cpp
Gamepad.send(0, 0, 0, 0, 0, 0, 0, buttons);
//           x  y  z  rz rx ry hat  buttons
```
The 6 axes and the hat exist because they're part of TinyUSB's fixed template —
there's no way to omit them without writing a custom descriptor (which this
project doesn't do). That's why `joy.cpl` shows axes parked at zero.

## 8. Number of buttons currently exposed

**UPDATED** since the original version of this document (2026-09-15): the
unified `lib/inputs` layer was integrated into HID, replacing the
simulated values. **32 buttons** in the descriptor (`uint32_t buttons`, bits
0–31, `HID_REPORT_COUNT(32)`, unchanged), of which:

- **Bits 0–30 (Buttons 1–31)** = the 31 real Button Box controls
  (MCP23017 + 74HC4067 + 4× KY-040), via `lib/inputs`. Full map
  (INPUT LOGICAL ID → bit) in `docs/INPUTS_PINOUT.en.md` section 10.
- **Bit 31 (Button 32)** = **free**. It used to be the bring-up heartbeat,
  removed as soon as the 31 real controls started responding on the bench. It
  is the first slot available for a new control.

The old "Bit 0 = board's BOOT button" was removed — it was a
simulated bring-up value, replaced by the real `INPUT_BUTTON_01` (MCP23017).
The physical BOOT button still exists only to open the WiFi portal
(hold for 5s), with no more connection to HID.

## 9. The button-32 heartbeat — removed

**It no longer exists in the firmware.** Throughout bring-up, bit 31 toggled on
its own every 1 s, purely in software via `millis()`, with no dependency on
WiFi/OTA/interrupts and no wiring at all:

```cpp
// REMOVED — kept here only as a reusable technique
if (now - lastBeatMs >= 1000) {
  lastBeatMs = now;
  beatOn = !beatOn;
  if (beatOn) buttons |= (1UL << 31);
  else        buttons &= ~(1UL << 31);
}
```

It was a proof of life for the HID before any hardware existed: if Button 32
blinked in `joy.cpl`, then USB, the descriptor and report sending were fine, and
any problem was wiring.

It went away once the 31 real controls started responding. Keeping it would be
noise: a button that fires by itself every second is exactly the kind of thing a
game or SimHub binds by mistake during auto-learn.

It is worth pasting the snippet back temporarily when bringing up a **new
board** — it is the cheapest test there is, and needs nothing soldered.

## 10. How the CDC is used

The CDC (COM port) is used only for a simple line-by-line text console,
processed in `serialCommands()`:

| Command      | Response / effect                                              |
|--------------|------------------------------------------------------------------|
| `PING`       | replies `PONG`                                                   |
| `VERSION`    | replies `ESP32S3_BUTTONBOX_HID_OTA`                               |
| `IP`         | replies with the current IP (`WiFi.localIP()`)                    |
| `RSSI`       | replies `RSSI <dBm> sleep=<ON\|OFF> ip=<ip>`, or `RSSI_OFFLINE`. Link diagnostic: `-50` great, `-67` is the practical floor for reliable OTA, below `-75` OTA breaks. `sleep=ON` together with a flaky OTA is a firmware bug, not a network one |
| `SETLEDS <n>`| sets the number of LEDs on the **strip** (1-192, default 10; the matrix is fixed at 64). Persisted in NVS. Replies `LEDS_SET <n>` or `LEDS_INVALID (1-192)` |
| `BRIGHTNESS` | replies `BRIGHTNESS_GET <n>%` (query only) |
| `BRIGHTNESS <n>` | caps global LED brightness. Clamped to 25-75%, so `BRIGHTNESS 10` replies `BRIGHTNESS_SET 25%` without an error; it only rejects outside 1-100. Persisted in NVS |
| `DUMPLEDS`   | prints what arrived from SimHub (matrix and strip) without needing the physical LEDs lit |
| `BOOTLOADER` | replies `REBOOTING_TO_BOOTLOADER`, then enters download mode via `usb_persist_restart(RESTART_BOOTLOADER)` — see item 15 for why this is **no longer** a direct `REG_WRITE` |

It also emits periodic unsolicited logs (`statusReport()`, every 5s):
WiFi/OTA/portal status. **During an OTA upload (`otaRunning == true`), the CDC
is not used for anything** — no writes happen, on purpose (see item 12).

Commands are case-insensitive (converted to uppercase before `strcmp`),
terminated by `\n` (32-byte buffer, `\r` ignored).

## 11. How WiFi connects

Always station mode (STA), fixed credentials coming from `include/secrets.h`
(`WIFI_SSID`, `WIFI_PASS` — git-ignored file, values never exposed in this
document):

```cpp
// setup()
WiFi.mode(WIFI_STA);
WiFi.setAutoReconnect(true);
WiFi.begin(WIFI_SSID, WIFI_PASS);
```

If the connection drops or never comes up, `wifiKeepAlive()` (called every
`loop()`, but throttled to 1 attempt every 10s) calls `WiFi.disconnect()` +
`WiFi.begin()` again, **forever, without giving up and without opening the portal
on its own**. This is deliberate (comment at the top of `main.cpp`): an earlier
version opened the config portal automatically on every failure and the
board got stuck in AP mode, offline, never reconnecting again.

The config portal (`WiFiManager`, for switching networks without recompiling)
only opens on explicit request: holding the BOOT button for 5 seconds **during
normal operation** (not at boot — GPIO0 low at reset enters download mode
and the app never even runs):
```cpp
if (bootDown && !portalAtivo && now - bootHeldSince >= 5000) {
  wm.setConfigPortalBlocking(false);
  portalAtivo = wm.startConfigPortal(AP_NAME);  // AP_NAME = "ButtonBox-Setup"
}
```

`WiFi.setSleep(false)` is only called inside `startOta()`, **after**
`WiFi.begin()` has already been called in `setup()` — calling it before, with the
radio not yet initialized in STA, prevents the AP portal from coming up (a lesson
documented at the top of the file).

## 12. How OTA is started

`ArduinoOTA` + `ESPmDNS`, initialized once by `startOta()`,
called from `loop()` only when WiFi is connected:
```cpp
void loop() {
  if (WiFi.status() == WL_CONNECTED) startOta();
  if (otaReady) ArduinoOTA.handle();
  if (otaRunning) return;   // nothing else runs during upload
  ...
}
```
`startOta()` is guarded by `otaReady` (only runs once): registers the hostname
(`OTA_HOSTNAME`, from `secrets.h`), `onStart`/`onEnd`/`onError` callbacks, and
**deliberately no `onProgress` callback** — writing to the CDC during
the upload blocks when the host isn't reading the port, which used to stall
`ArduinoOTA.handle()` and kill the transfer halfway through (this was the cause of
the old "Error Uploading" at ~29%, resolved before this baseline).

Once the OTA upload starts (`otaRunning = true` in `onStart`), the
`loop()` returns early right after calling `ArduinoOTA.handle()` — no
other logic (HID, serial, WiFi keep-alive) runs until the upload finishes.

## 13. How the firmware is updated

Two paths, both via the same `platformio.ini`:

- **USB (cable)**: `pio run -e esp32s3-supermini -t upload`. The
  `extra_scripts = pre:scripts/enter_bootloader.py` runs before the upload:
  finds the board's port by MAC (`custom_expected_mac`), opens it with
  `dtr=False, rts=False`, sends `BOOTLOADER\n` (the same command from item 10),
  waits up to 10s for a new port in download mode, and repoints
  `UPLOAD_PORT` to it. **Doesn't require manually holding the BOOT button** —
  this was already a problem resolved before this baseline.
- **OTA (WiFi)**: `pio run -e esp32s3-ota -t upload` (env `[env:ota]`, which
  extends `esp32s3-supermini` with `upload_protocol = espota`). The
  `extra_scripts = pre:scripts/ota_port_by_mac.py` discovers the board's current
  IP by MAC (`arp -a` after a broadcast ping) and replaces
  `UPLOAD_PORT` with it — works around the fact that `espota` doesn't resolve
  `.local` (mDNS) names and the IP can change via DHCP.

In both cases, the binary is uploaded to the idle OTA partition (`ota_0`/`ota_1`,
1.25 MB each, see `default.csv`) and the bootloader switches to it on the next
boot — there's no need to erase the entire flash.

## 14. Which PlatformIO board/env should be used

- **Board**: `esp32-s3-fh4r2` (`boards/esp32-s3-fh4r2.json`, custom, **not** the
  generic `esp32-s3-devkitc-1`) — 4 MB flash + 2 MB quad PSRAM
  (`memory_type=qio_qspi`), `default.csv` partitions. Using the generic devkit
  (8 MB, without this PSRAM) causes a **boot loop** on this physical board (history
  documented in `HANDOFF.md`).
- **Env for normal development (compile + flash + run)**:
  `[env:esp32s3-supermini]` — the only one with the production firmware
  (`src_filter = +<main.cpp>`), full HID + CDC + WiFi + OTA.
- **`[env:diag]` env**: an alternative minimal firmware (`src/diag.cpp`,
  `ARDUINO_USB_MODE=1`, just prints `"alive N"` every 500 ms). Exists only to
  isolate toolchain/board/flashing problems from the HID logic — **not the
  product firmware**, exposes no HID at all.
- **`[env:ota]` env**: the same firmware as `esp32s3-supermini` (via `extends`),
  just swaps the upload protocol to WiFi.

## 15. Which files are critical and shouldn't be changed without need

| File | Why it's critical |
|---|---|
| `src/main.cpp` | All the HID, WiFi, OTA, and serial console logic. Contains, in comments at the top and around the `BOOTLOADER` command, the history of already-resolved bugs (stuck portal, `setSleep` before `begin`, CDC writes stalling OTA, RTC register stuck in bootloader) — removing those comments or undoing those decisions reintroduces already-fixed bugs. |
| `platformio.ini` | `board_build.extra_flags` with `ARDUINO_USB_MODE=0` is what turns on HID (the board.json defaults to `MODE=1`, which **doesn't do HID**). `-DBOARD_HAS_PSRAM` is what fixes OTA's `Flash Read Failed`. `-DUSB_PRODUCT`/`-DUSB_MANUFACTURER` prevent the board.json name from leaking into the USB descriptor. `upload_speed=115200` is deliberate (a higher speed fails esptool's handshake over TinyUSB's CDC). |
| `boards/esp32-s3-fh4r2.json` | Custom board definition (4 MB flash + PSRAM); swapping it for a generic board already caused a boot loop in the past. |
| `scripts/enter_bootloader.py` | Mechanism that allows USB flashing without manually pressing the BOOT button; depends on the `BOOTLOADER` command in `main.cpp` using `usb_persist_restart()` (not the direct RTC register). |
| `scripts/ota_port_by_mac.py` | Mechanism that lets `espota` find the board without a fixed IP or working mDNS. |
| `include/secrets.h` | WiFi credentials and OTA hostname (`WIFI_SSID`, `WIFI_PASS`, `OTA_HOSTNAME`). Git-ignored — should never be committed or have its contents printed/logged. |
| `~/.platformio/packages/framework-arduinoespressif32` (installed core) | Not part of this project, but the USB/HID/CDC behavior described here depends on the installed version (`espressif32@6.12.0` / `framework-arduinoespressif32@3.20017.241212`, see `docs/BASELINE.md`). A core update could change defaults (e.g. `USB_PID`, HID report layout). |

**Not** a critical file, free for exploratory use: `src/diag.cpp` (isolated
diagnostic env, doesn't affect the production firmware).
