[🇧🇷 Português](BASELINE.md) | 🇺🇸 English

# BASELINE.md — Tests that demonstrably passed

> Regression checklist for this firmware. Every item below was observed
> working, with the evidence described. If a future change breaks any of
> these, it's a regression — compare against this document before
> investigating from scratch. Generated on 2026-09-15, referring to the
> code state described in `docs/ARCHITECTURE.md`.

## Verified build environment

```
$ pio run -e esp32s3-supermini
PLATFORM: Espressif 32 (6.12.0) > ESP32-S3-FH4R2 (Dingyimei S3 Zero)
HARDWARE: ESP32S3 240MHz, 320KB RAM, 4MB Flash
PACKAGES:
 - framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b
 - tool-esptoolpy @ 2.40900.250804 (4.9.0)
 - toolchain-xtensa-esp32s3 @ 8.4.0+2021r2-patch5
RAM:   18.9% (61768 / 327680 bytes)
Flash: 67.2% (881097 / 1310720 bytes)   <- within the 1.25 MB OTA partition
[SUCCESS]
```
Reference board: ESP32-S3 (QFN56) rev v0.2, 4 MB XMC flash, 2 MB quad
PSRAM, 40 MHz crystal, MAC `AC:27:6E:CC:FA:B8`.

---

## 1. Compiles without errors

`pio run -e esp32s3-supermini` finishes with `[SUCCESS]`, RAM/Flash
within the partition limits (see above). Re-run during this audit
(2026-09-15) — clean build, no conflicting-flags warnings.

## 2. HID recognized on Windows

- Device shows up in `joy.cpl` as `ESP32S3-SimHub-ButtonBox`, status OK.
- **32 buttons**, **4-6 axes** listed (X, Y, Z, Rotation X/Y/Z, all
  parked at zero — expected, see `ARCHITECTURE.md` item 7).
- **Button 32 blinks on its own** every ~1 s (software heartbeat, with no
  wiring connected).
- Evidence: user-captured screenshot showing exactly this state.

## 3. Button 1 = physical BOOT — **OUTDATED, no longer applies**

Valid only up until the `lib/inputs` layer was integrated (see
`docs/SYSTEM_INTEGRATION.md`): BOOT **no longer feeds any HID bit** — it
was a simulated bring-up value, replaced by the real `INPUT_BUTTON_01`
(MCP23017). The physical BOOT button still exists only to open the WiFi
portal (5s hold). Kept here struck through, instead of deleted, so the
history of why it existed isn't lost.

## 4. CDC (COM port) responds

Serial Monitor (VS Code / `pio device monitor`, 115200 baud) shows the
periodic status lines (`[status] HID ok | WiFi ip=... rssi=... | OTA=pronto`)
and responds to commands:
- `PING` → `PONG`
- `VERSION` → `ESP32S3_BUTTONBOX_HID_OTA`
- `IP` → current IP
Evidence: Serial Monitor session captured by the user on a real Windows
COM port, simultaneous with HID working (proof that HID + CDC coexist on
the same composite device).

## 5. WiFi connects and reconnects on its own

Board connects to the network configured in `secrets.h` (SSID
`Orlando_tplink` in the reference session), reports IP and RSSI in the
serial status. Reconnects on its own after a drop, never opening the
config portal on its own initiative (previous, corrected behavior used to
trap the board in AP mode).

## 6. OTA — 5 consecutive successful cycles

After the `-DBOARD_HAS_PSRAM` fix (which eliminated the old
`Flash Read Failed` on post-upload verification), **5 consecutive OTA
uploads** completed successfully (`pio run -e esp32s3-ota -t upload`),
with no verification failure. This is the single most important
regression test for any change to `platformio.ini` or the build flags.

## 7. USB flashing without holding the BOOT button

`scripts/enter_bootloader.py` (runs as a `pre:` step of the
`esp32s3-supermini` env) can put the board into download mode on its
own, via the `BOOTLOADER` serial command, with no manual button
intervention. Confirmed working end to end.

## 8. The `BOOTLOADER` command no longer traps the board

This is the most recent and most critical test in this baseline.
Manually verified sequence:
1. Send `BOOTLOADER` over serial while the fixed firmware is running.
2. Board enters download mode (confirmed: a new port appears,
   `device list` shows `Description: USB JTAG/serial debug unit`,
   `VID:PID=303A:1001`).
3. Flash a new firmware through that port.
4. **Board goes back to running the app on its own**, with no physical
   power cycle — `device list` shows `Description: ESP32S3-SimHub-ButtonBox`
   again.

Before the fix (swapping `REG_WRITE(RTC_CNTL_OPTION1_REG, ...)` for
`usb_persist_restart(RESTART_BOOTLOADER)`), this same test would
permanently trap the board in the bootloader — only a full physical power
cycle (removing power entirely) would fix it. **Any regression that goes
back to writing the RTC register directly reintroduces this bug.**

## 9. SimHub protocol — recognized by SimHub itself (Arduino tab)

**Replaces the previous version of this item**, which documented evidence
from the old, discarded protocol (`proto`/`ledsc`/`sleds` — see
`docs/SIMHUB_PROTOCOL.en.md`, "Previous mistake" section). The current
evidence is from the real protocol (ARQ transport + commands), tested on
2026-09-16 directly in the user's SimHub (not just via
`scripts/simhub_test_send.py`), with the already-integrated production
firmware:

- SimHub's **Arduino** tab showed the device as **`Connected`** (before:
  `Unrecognized`/`Port not scanned` — see the correction history in
  `docs/SIMHUB_PROTOCOL.en.md`).
- **Connected device informations**: `Device name = ESP32S3-ButtonBox`,
  `Firmware Revision = j`, `Features list = NIXR`, `RGB Leds = 10`,
  `RGB Matrix = True`, `Unique Id = AC276ECCFAB8`.
- **Communication statistics**: `FPS ≈ 55-58`, **`Corrupted = 0`**,
  **`Reemited = 0`**, `Reemited af. wait = 0` — confirms the ARQ transport
  (CRC8 checksum + per-packet acknowledgment) is healthy, not just that
  the initial handshake worked.
- Evidence: user's screenshot of SimHub's Arduino tab.

Also confirmed in that session: the strip's LED count configured via
`SETLEDS` (persisted in NVS) **survived an OTA upload** between one test
and the next — the NVS partition is unaffected by an OTA update.

Pending: actually lighting up the physical LEDs and configuring the
effects inside SimHub (matrix + strip aren't physically mounted yet at
the time of this test) — next step, not a failure of this test.

---

## Known issue, not fixed in this baseline

- **PlatformIO sometimes auto-detects the wrong serial port** during a
  USB upload (observed picking `/dev/cu.G900A`, which was the user's
  phone connected to the Mac, not the board). Current workaround: the
  scripts in `scripts/` already filter by MAC
  (`custom_expected_mac`/`custom_board_mac`), which resolves most cases;
  when it still fails, pass `--upload-port` explicitly or use OTA instead
  of USB. **Not investigated in depth nor fixed** — out of scope for this
  audit (documentation only, no code change).

---

## Quick guide for a new developer

**1. Build**
```
cd ~/Projects/arduino/esp32s3-buttonbox-test
pio run -e esp32s3-supermini
```

**2. Flash (first time, or whenever USB is available)**
```
pio run -e esp32s3-supermini -t upload
```
No button-holding needed — `scripts/enter_bootloader.py` handles it on
its own by sending the `BOOTLOADER` command over CDC.

**3. Connect**
Plug the board in via USB-C. It should show up on Windows as two
devices: a game controller (`joy.cpl`) and a COM port.

**4. Verify HID**
Open `joy.cpl` on Windows → `ESP32S3-SimHub-ButtonBox` should appear, 32
buttons, status OK, with **Button 32 blinking on its own** every second
(no wiring needed for this test). Holding the board's physical BOOT
button should light up Button 1.

**5. Verify CDC**
```
pio device monitor -e esp32s3-supermini
```
(or any serial terminal on the matching COM port, 115200 baud). Typing
`PING` and pressing Enter should reply `PONG`. A status line should also
show up every 5 seconds.

**6. Do an OTA update**
With the board already running the firmware and connected to WiFi
(`secrets.h` filled in with the right credentials):
```
pio run -e esp32s3-ota -t upload
```
The `scripts/ota_port_by_mac.py` script finds the board's IP on its own
by MAC. Watch the Serial Monitor before disconnecting: it should show
`[OTA] iniciando` and then `[OTA] concluido`.

**Success criterion for this baseline**: any future firmware change must
keep passing all 8 tests above, with no exceptions, before it's
considered ready.
