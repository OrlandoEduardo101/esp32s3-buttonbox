[🇧🇷 Português](SYSTEM_INTEGRATION.md) | 🇺🇸 English

# SYSTEM_INTEGRATION.md — Final system integration

> Generated on 2026-09-15. Covers the requested final architecture, the
> audit of the 12 rules against the actual code (not from memory — each
> one was verified by reading/grepping the code in this session), and the
> full test plan. **Physical hardware (MCP23017, 74HC4067, encoders,
> WS2812 matrix+strip) is not assembled yet** — so the hardware part of
> the requested full test could not be *executed*, only prepared and
> documented as a checklist. This is stated explicitly instead of assumed
> as done.

## Final architecture (as requested, plus what was added in this stage)

```
                    ESP32-S3
                       │
        ┌──────────────┼───────────────┐
        │              │               │
       HID            CDC             WiFi
        │              │               │
        │           SimHub            OTA
        │              │
        │         RGB framebuffer
        │              │
        │              ▼
        │           WS2812   <- NEW in this stage (lib/ws2812, via RMT)
        │
        ▼
      INPUTS
        │
   ┌────┴─────────────┐
   │                  │
ESP32 GPIO        Expanders
   │              ┌────┴────┐
   │              │         │
Encoders       MCP23017   74HC4067
   │              │         │
   └──────────────┴─────────┘
                  │
            physical controls
```

The only component still missing to close this diagram was the WS2812
driver (RGB framebuffer → physical strip) — everything else (HID, CDC,
WiFi, OTA, SimHub, inputs, MCP23017, 74HC4067, encoders) already existed
from previous stages and wasn't rewritten, only verified.

## New in this stage

- **`lib/ws2812/`** — output driver for the 8x8 matrix + strip (~10 LEDs),
  via the ESP32-S3's RMT peripheral (`rmtInit`/`rmtWrite`, the same bit
  timing already used and validated by Espressif in the core's
  `neopixelWrite()`, generalized from 1 pixel to N). Non-blocking —
  `rmtWrite()` is asynchronous, the actual transmission runs in hardware
  after the function returns. Doesn't know about SimHub or telemetry
  (rule 9) — has its own `Ws2812Color` type.
- **GPIO1** — new pinout decision: WS2812 data output (was free,
  documented in `docs/INPUTS_PINOUT.md`).
- **`src/main.cpp`** — the only bridge between `lib/simhub` and
  `lib/ws2812` (~10 lines of glue in `loop()`: reads SimHub's framebuffer,
  converts it to the driver's type, sends it to the strip). Neither
  simhub nor ws2812 know about each other.
- **`src/ws2812_test.cpp`** + `[env:ws2812-test]` — isolated, sweeps fixed
  colors (red/green/blue/white/off) to validate the RMT channel without
  depending on SimHub.

## Centralized pinout — `include/board_config.h`

Added on 2026-09-16, by explicit request: the user chose a very small
ESP32-S3 because the expanders were on hand, but wants to be able to swap
boards (a bigger one, or a different variant) in the future, and also
wants the project reusable by other people with a different pinout.

**Before**: each driver (`lib/mcp23017`, `lib/mux4067`, `lib/encoders`,
`lib/ws2812`) had its own default pins, and `lib/inputs`/`main.cpp` simply
used those defaults without passing anything explicit — changing a pin
meant editing the `.h` inside the library.

**After**: `include/board_config.h` is the only file in this project with
a pin/channel/address. `lib/inputs/inputs.cpp` and `src/main.cpp` read the
values from there and pass them explicitly to each `_init()`. The
low-level drivers keep their own defaults (now only used if someone takes
a library standalone, outside this project, without `board_config.h`) —
keeping each driver generic and independently reusable.

Purely structural change, no runtime effect: `esp32s3-supermini`'s
RAM/Flash are identical before/after (89624 B / 911849 B).

A build detail that was needed: PlatformIO's `include/` directory isn't
visible by default to private libraries in `lib/` (only to `src/`) — it
was necessary to add `-I include` to the `build_flags` of the envs that
transitively `#include board_config.h` (`esp32s3-supermini`, which `ota`
inherits via `extends`, and `inputs-test`).

To swap boards/pinout: edit only `include/board_config.h`. No other file
needs to change (unless the board itself requires a different
`board.json` — see `boards/esp32-s3-fh4r2.json`, which is about the
chip/flash/PSRAM, not about the input/output pins).

## Audit of the 12 rules (verified by grep/reading, not from memory)

| # | Rule | Verification | Result |
|---|---|---|---|
| 1 | HID works without SimHub | `Gamepad.send()` at the end of `loop()` runs unconditionally; nothing in `simhub.*` is read by the button bitmask | ✅ |
| 2 | SimHub works without altering HID | `grep "Gamepad\|USBHID" lib/simhub/*` → no matches | ✅ |
| 3 | OTA works without breaking HID | OTA block (`ArduinoOTA.handle()`, callbacks) untouched since the baseline; `if (otaRunning) return;` already existed and keeps protecting the rest (inputs/simhub/ws2812 included) | ✅ |
| 4 | OTA doesn't depend on the CDC being read | OTA is 100% WiFi (UDP/TCP); `grep "Serial.read\|Serial.available"` inside `startOta()`/callbacks → no matches | ✅ |
| 5 | MCP23017 doesn't access HID directly | `grep "Gamepad\|USBHID" lib/mcp23017/*` → no matches | ✅ |
| 6 | 74HC4067 doesn't access HID directly | `grep "Gamepad\|USBHID" lib/mux4067/*` → no matches | ✅ |
| 7 | Encoders don't access HID directly | `grep "Gamepad\|USBHID" lib/encoders/*` → no matches | ✅ |
| 8 | SimHub doesn't know about GPIO | `grep "pinMode\|digitalRead\|digitalWrite" lib/simhub/*` → no matches (only uses `Serial`) | ✅ |
| 9 | LED driver doesn't know about telemetry | `lib/ws2812` doesn't include `simhub.h`; has its own color type (`Ws2812Color`); the bridge lives only in `main.cpp` | ✅ |
| 10 | No button matrix | No row/column scan in any module; "matrix" in the code only refers to the physical 8x8 LED matrix (output, not input) | ✅ |
| 11 | No blocking delays | See the detailed table below — every `delay()`/`delayMicroseconds()` in the project is one-off/documented, none new in this stage | ✅ (with already-known caveats, none new) |
| 12 | No unnecessary refactoring of the validated USB | `[env:esp32s3-supermini]` in `platformio.ini` (USB flags, `USB_PRODUCT`/`USB_MANUFACTURER`, board) identical; `USB.h`/`USBHIDGamepad`/`Gamepad.begin()`/`USB.begin()` untouched | ✅ |

### Rule 11 detail — full inventory of `delay()`

| Location | Duration | When it runs | Already existed before this stage? |
|---|---|---|---|
| `main.cpp`, `BOOTLOADER` command | 100ms | Only when rebooting into download mode (explicit user action) | Yes (stage 5) |
| `main.cpp`, `setup()` | 300ms | Only at boot, before `loop()` starts | Yes (original baseline) |
| `main.cpp`, end of `loop()` | 5ms | Every iteration — it's the main loop's overall pace | Yes (original baseline) |
| `mux4067_init()` | 30µs × channels in use | Only once, at initialization | Yes (74HC4067 stage) |
| `simhub_process_packet()` (`readByteUntil`) | up to 100ms **total**, not per byte | Only when a SimHub frame has already started (6×0xFF header seen) and stalls midway | Yes (SimHub stage) — **not a `delay()`**, it's a busy-wait with a time budget, documented as an engineering decision in `docs/SIMHUB_PROTOCOL.md` |

None of these are new in this stage. `ws2812_show()` (new) explicitly
**does not** block — uses async `rmtWrite()`, not `rmtWriteBlocking()`.

### Known residual risk (not fixed, per "don't refactor unnecessarily")

During the window where WiFi is connected and OTA is already ready
(`otaReady=true`) but no transfer is in progress (`otaRunning=false`), a
corrupted/stalled SimHub frame can hold `serialCommands()` for up to
100ms before the next `ArduinoOTA.handle()`. This doesn't break OTA (rule
4 is about *dependency*, not latency, and OTA's network handshake
tolerates far more than 100ms of jitter), but it is a real latency
coupling between the two layers. Not changed now because (a) it's a rare
case (SimHub would have to be sending corrupted/incomplete data and never
complete it), (b) any fix would be a refactor of the already-validated
SimHub parser, and rule 12 asks not to refactor without proven need.
Recorded here in case OTA ever seems to "stutter" while SimHub is
connected.

## Final numbers (compiled in this session)

| Env | RAM | Flash |
|---|---|---|
| `esp32s3-supermini` (full production) | 90264 B (27.5%) | 909481 B (69.4%) |
| `ota` (same binary, OTA protocol) | identical to the above | identical to the above |
| `diag` | 18664 B (5.7%) | 261061 B (19.9%) |
| `mcp-test` | 19068 B (5.8%) | 281201 B (21.5%) |
| `mux-test` | 18760 B (5.7%) | 262981 B (20.1%) |
| `encoder-test` | 19448 B (5.9%) | 264065 B (20.1%) |
| `inputs-test` | 20152 B (6.1%) | 285373 B (21.8%) |
| `simhub-test` | 32492 B (9.9%) | 302649 B (23.1%) |
| `ws2812-test` | 43472 B (13.3%) | 261937 B (20.0%) |

The production binary's RAM jump (63512B → 90264B, +26752B) comes almost
entirely from the RMT fixed buffer (`WS2812_MAX_LEDS * 24 * 4 bytes` =
24576B) — expected and documented in `lib/ws2812/ws2812.h`.

---

## Requested full test — what was executed vs. what's still pending

### Executed in this session (no hardware — build and static audit)

- [x] Compilation of **all 9 envs** (production + 7 isolated tests + OTA)
  with no errors.
- [x] Audit of the 12 architecture rules (table above).
- [x] Confirmation that no new module is even *linked* into the
  production binary without going through explicit integration in
  `main.cpp` (LDF "chain mode": only what's `#include`d gets pulled in).

### Pending — needs the physical hardware assembled (could not be executed)

This section is the definitive checklist. Each row states **what to
test**, **the success criterion**, and, if it fails, **which isolated env
to use to find the cause** (per the task's own rule: "isolate the
responsible layer before modifying code").

| Test | Success criterion | If it fails, isolate with |
|---|---|---|
| Each push button (11) individually | The corresponding bit in `joy.cpl` lights up/turns off exactly on press/release | `mcp-test` (reads the MCP23017 directly, without going through `inputs`/HID) |
| SW switches of the 4 encoders | Same, 4 buttons (12-15) | `mux-test` (74HC4067 directly) |
| 4 encoders — CW/CCW | Each physical detent generates exactly 1 event in the correct direction | `encoder-test` (isolated quadrature decoder) |
| Parking brake | State follows the lever's position | `mux-test` (channel C8) |
| Ignition (3 positions) | ON and IGN correct, IGN only during cranking (see `docs/INPUTS_PINOUT.md` section 2) | `mcp-test` (GPB4/GPB5) |
| Start Engine | Button + LED (if already wired) | `mcp-test` (GPB3) for the button; the LED is separate wiring, no firmware involved |
| 4 toggle switches | State follows the switch's position | `mux-test` (channels C4-C7) |
| Multiple simultaneous buttons | No interference between bits (shouldn't be any — there's no matrix) | `inputs-test` (several inputs at once, check the log) |
| HID on Windows | `joy.cpl` shows the 31 real controls + heartbeat on bit 32 | `docs/BASELINE.md` (original test) + `docs/INPUTS_PINOUT.md` section 10 (bit map) |
| CDC | `PING`→`PONG`, `VERSION`, `IP`, `SETLEDS <n>` respond | Any serial terminal on the COM port |
| SimHub — protocol | `proto`/`ledsc`/`sleds` respond correctly, RGB arrives correctly | `simhub-test` + `scripts/simhub_test_send.py` |
| LEDs (WS2812) | Colors appear correctly on the matrix/strip, no flicker and no swapped channels (R/G/B) | `ws2812-test` (fixed color sweep, no SimHub) |
| WiFi | Connects on its own, reconnects on its own, portal only via 5s BOOT hold | `docs/BASELINE.md` (already validated before, untouched) |
| OTA | Full upload via `pio run -e ota -t upload`, HID keeps working during and after | `docs/BASELINE.md` (5 cycles already validated before this integration) |
| **HID + SimHub together** | Board connected on Windows as HID **and** in SimHub at the same time, neither interfering with the other | If it fails, isolate: first `simhub-test` alone (confirms the parser works), then the full binary — if only the full one fails, the problem is in the coexistence (`serialCommands()` dispatching to both protocols), not in any individual parser |

> Note carried over unmodified from the Portuguese source: the "SimHub —
> protocol" row above still refers to the `proto`/`ledsc`/`sleds` legacy
> LED-sketch protocol. That protocol was later found not to be what
> SimHub's "Arduino" tab actually speaks — see `docs/SIMHUB_PROTOCOL.en.md`
> for the real ARQ + `0x03`-command protocol. This translation preserves
> the source document's current (outdated) wording rather than silently
> correcting it.

### Recommended bring-up order (minimizes the risk of not knowing which layer failed)

1. `mcp-test` — only the MCP23017, nothing else.
2. `mux-test` — only the 74HC4067.
3. `encoder-test` — only the 4 KY-040 encoders.
4. `inputs-test` — all three together, via the unified API.
5. `ws2812-test` — only the strip/matrix, fixed colors.
6. `simhub-test` — only the protocol, with `scripts/simhub_test_send.py`.
7. `esp32s3-supermini` (full production) — everything together. If
   something that worked in isolation fails here, the problem is
   **integration** (resource contention, timing, memory), not the module
   itself — go back to the table above to know where to start isolating
   again.
