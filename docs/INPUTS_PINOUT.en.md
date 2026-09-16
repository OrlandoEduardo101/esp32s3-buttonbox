[🇧🇷 Português](INPUTS_PINOUT.md) | 🇺🇸 English

# INPUTS_PINOUT.md — Final hardware input architecture

> Pinout definition document — the **reasoning** behind each pin/channel
> choice. Based on the ESP32-S3 chip's real constraints (verified in
> `variants/esp32s3/pins_arduino.h` from the installed Arduino-ESP32
> core) and on the MCP23017 and 74HC4067 datasheet capabilities.
>
> **The numbers that actually apply live in `include/board_config.h`.**
> Switched boards or want a different pinout? Edit only that file — no
> other `.cpp`/`.h` in the project has a fixed pin/channel/address. This
> document still stands as an explanation of *why* each choice was made
> (electrical constraints, expansion headroom, etc.), not as the source
> of the current values.

## Why this distribution

- **Direct ESP32-S3 GPIO**: reserved for what needs the lowest latency
  and interrupt-driven reads — the 8 CLK/DT signals of the 4 encoders
  (quadrature requires fast, delay-sensitive decoding) and the I2C bus +
  interrupt line for the MCP23017.
- **MCP23017 (I2C, with interrupt)**: reserved for the most frequent/
  critical "action" inputs — the 11 push buttons, the Start Engine
  button, and the ignition key. The MCP supports interrupt-on-change
  (INTA/INTB) and internal pull-up on all 16 pins, so the ESP32 doesn't
  poll: it only wakes up when something changes, with debounce done in
  firmware on top of the event.
- **74HC4067 (analog/digital mux, no interrupt)**: reserved for state
  inputs that change slowly and tolerate polling-based scanning — the 4
  encoder SW switches (built-in button click), the 4 toggle switches, and
  the parking brake microswitch. No interrupt of its own, but scanning 9
  channels is trivially fast (ESP32-S3 at 240 MHz), and none of these
  inputs demand a response within a few milliseconds.
- Both expanders keep free channels (2 on the MCP, 7 on the mux) for
  future expansion without redesigning the pinout.

---

## 1. Input table

| INPUT | HARDWARE | PIN | TYPE | NOTE |
|---|---|---|---|---|
| Encoder 1 — CLK | Direct ESP32-S3 | GPIO4 | Digital, interrupt (edge) | Quadrature; decode CLK+DT together, no software debounce (debounce is inherent to the quadrature state machine). Output mapped as a fixed virtual +/- button — see section 8 |
| Encoder 1 — DT | Direct ESP32-S3 | GPIO5 | Digital, interrupt (edge) | See CLK note |
| Encoder 1 — SW | 74HC4067 | C0 | Digital, polling | Encoder click; firmware debounce (~10–20 ms) |
| Encoder 2 — CLK | Direct ESP32-S3 | GPIO6 | Digital, interrupt (edge) | Quadrature |
| Encoder 2 — DT | Direct ESP32-S3 | GPIO7 | Digital, interrupt (edge) | Quadrature |
| Encoder 2 — SW | 74HC4067 | C1 | Digital, polling | Firmware debounce |
| Encoder 3 — CLK | Direct ESP32-S3 | GPIO10 | Digital, interrupt (edge) | Quadrature |
| Encoder 3 — DT | Direct ESP32-S3 | GPIO11 | Digital, interrupt (edge) | Quadrature |
| Encoder 3 — SW | 74HC4067 | C2 | Digital, polling | Firmware debounce |
| Encoder 4 — CLK | Direct ESP32-S3 | GPIO12 | Digital, interrupt (edge) | Quadrature |
| Encoder 4 — DT | Direct ESP32-S3 | GPIO13 | Digital, interrupt (edge) | Quadrature |
| Encoder 4 — SW | 74HC4067 | C3 | Digital, polling | Firmware debounce |
| Push button 1 | MCP23017 | GPA0 | Digital, interrupt (I2C) | MCP internal pull-up |
| Push button 2 | MCP23017 | GPA1 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 3 | MCP23017 | GPA2 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 4 | MCP23017 | GPA3 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 5 | MCP23017 | GPA4 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 6 | MCP23017 | GPA5 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 7 | MCP23017 | GPA6 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 8 | MCP23017 | GPA7 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 9 | MCP23017 | GPB0 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 10 | MCP23017 | GPB1 | Digital, interrupt (I2C) | Internal pull-up |
| Push button 11 | MCP23017 | GPB2 | Digital, interrupt (I2C) | Internal pull-up |
| Start Engine (push) | MCP23017 | GPB3 | Digital, interrupt (I2C) | Internal pull-up; critical action, low latency via interrupt |
| Ignition — ON position | MCP23017 | GPB4 | Digital, interrupt (I2C) | Confirmed by the user: scooter-style key, 3 physical positions (1=OFF, 2=ON, 3=IGN/start). ON contact closes at position 2 and **stays closed** at position 3 (doesn't open during cranking) |
| Ignition — IGN position | MCP23017 | GPB5 | Digital, interrupt (I2C) | **Momentary**: only closes while the key is held at position 3; the key is spring-loaded and returns to position 2 on its own when released — this contact is not bistable, the firmware must treat it as a pulse, not a state |
| Parking brake microswitch | 74HC4067 | C8 | Digital, polling | State (not a pulse) — down=brake engaged, up=brake released. See mapping logic in section 7 |
| Toggle switch 1 (ON/OFF) | 74HC4067 | C4 | Digital, polling | State, no urgency |
| Toggle switch 2 (ON/OFF) | 74HC4067 | C5 | Digital, polling | State, no urgency |
| Toggle switch 3 (ON/OFF) | 74HC4067 | C6 | Digital, polling | State, no urgency |
| Toggle switch 4 (ON/OFF) | 74HC4067 | C7 | Digital, polling | State, no urgency |

Total: 31 physical signals (12 from the encoders + 11 push buttons + 1
microswitch + 2 from the ignition + 1 start + 4 toggle switches).

---

## 2. Full MCP23017 map (I2C)

| Pin | Use |
|---|---|
| GPA0 | Push button 1 |
| GPA1 | Push button 2 |
| GPA2 | Push button 3 |
| GPA3 | Push button 4 |
| GPA4 | Push button 5 |
| GPA5 | Push button 6 |
| GPA6 | Push button 7 |
| GPA7 | Push button 8 |
| GPB0 | Push button 9 |
| GPB1 | Push button 10 |
| GPB2 | Push button 11 |
| GPB3 | Start Engine |
| GPB4 | Ignition — ON |
| GPB5 | Ignition — IGN |
| GPB6 | **Free** (future expansion) |
| GPB7 | **Free** (future expansion) |

Recommended configuration (for when it gets implemented): internal
pull-ups enabled on all used pins, `GPINTEN` enabled on the 14 used pins,
`INTCON`/`DEFVAL` for interrupt-on-change (not fixed level), banks A and
B with mirrored interrupt (`IOCON.MIRROR = 1`) to use a single INT line
on the ESP32-S3.

### Ignition key logic (confirmed with the user)

Scooter-style key, 3 physical positions — **not** an ordinary 3-stable-
state rotary switch:

| Physical position | GPB4 (ON) | GPB5 (IGN) | Mechanical behavior |
|---|---|---|---|
| 1 — OFF | inactive | inactive | Stable — stays until the user moves it |
| 2 — ON | **active** | inactive | Stable — stays until the user moves it |
| 3 — IGN/start | **active** | **active** | **Momentary** — spring return; releases on its own and the key returns to position 2 as soon as the user lets go |

Consequences for the firmware (not implemented yet, only recorded here
for when the logic gets ported):
- GPB5 (IGN) must be treated as a **start pulse**, not a state — it never
  stays "latched" active on its own, always returns to 0 once the user's
  hand leaves the key.
- If the user moves the key directly from position 3 to position 1
  (skipping position 2), the firmware sees GPB4 and GPB5 drop practically
  together — the vehicle should be treated as OFF in that case, exactly
  as if it had passed through ON first.
- There's no valid combination of "IGN active with ON inactive" on this
  key — if that's ever read, it's a transient electrical transition
  (debounce), not a real state to report.

I2C address: 7 bits, `0x20`–`0x27` depending on the chip's A0/A1/A2
address pins (wire according to the board; with a single MCP, just tie
A0/A1/A2 to GND → address `0x20`).

---

## 3. Full 74HC4067 map (digital mux)

| Channel | Use |
|---|---|
| C0 | Encoder 1 — SW |
| C1 | Encoder 2 — SW |
| C2 | Encoder 3 — SW |
| C3 | Encoder 4 — SW |
| C4 | Toggle switch 1 |
| C5 | Toggle switch 2 |
| C6 | Toggle switch 3 |
| C7 | Toggle switch 4 |
| C8 | Parking brake microswitch |
| C9 | **Free** (future expansion) |
| C10 | **Free** (future expansion) |
| C11 | **Free** (future expansion) |
| C12 | **Free** (future expansion) |
| C13 | **Free** (future expansion) |
| C14 | **Free** (future expansion) |
| C15 | **Free** (future expansion) |

Control lines (not input channels, counted separately in the ESP32-S3 GPIO
budget — see section 4):

| Line | Function |
|---|---|
| S0 | Bit 0 of the channel address |
| S1 | Bit 1 of the channel address |
| S2 | Bit 2 of the channel address |
| S3 | Bit 3 of the channel address |
| SIG | Common output — the ESP32 reads the state of the currently selected channel here |

Every input on the mux needs a pull-up (or pull-down, depending on the
chosen logic) resistor **on the channel itself**, since the 4067 only
connects the selected channel to SIG — it provides no internal pull-up
like the MCP23017 does.

---

## 4. Direct ESP32-S3 GPIOs used

| GPIO | Function |
|---|---|
| GPIO4 | Encoder 1 — CLK |
| GPIO5 | Encoder 1 — DT |
| GPIO6 | Encoder 2 — CLK |
| GPIO7 | Encoder 2 — DT |
| GPIO8 | I2C SDA (for the MCP23017) — Arduino-ESP32 core default pin (`pins_arduino.h`) |
| GPIO9 | I2C SCL (for the MCP23017) — core default pin |
| GPIO10 | Encoder 3 — CLK |
| GPIO11 | Encoder 3 — DT |
| GPIO12 | Encoder 4 — CLK |
| GPIO13 | Encoder 4 — DT |
| GPIO14 | MCP23017 — INT (mirrored A+B interrupt) |
| GPIO15 | 74HC4067 — S0 |
| GPIO16 | 74HC4067 — S1 |
| GPIO17 | 74HC4067 — S2 |
| GPIO18 | 74HC4067 — S3 |
| GPIO21 | 74HC4067 — SIG |

16 GPIOs used. All within the GPIO0–21 range, which is universally
exposed on any ESP32-S3 board variant (including Super Mini/S3 Zero) —
none of them depend on pins whose availability varies by manufacturer.

Pins deliberately **avoided** in this allocation (don't use for inputs
without a strong reason):
- **GPIO0** — already the board's physical BOOT button, already mapped as
  HID Button 1 in the current firmware (`docs/ARCHITECTURE.md` item 8).
  Strapping pin.
- **GPIO3, GPIO45, GPIO46** — ESP32-S3 strapping pins (affect boot mode/
  flash voltage); GPIO46 is also *input-only*. Usable only with extra
  care, not recommended for the first hardware revision.
- **GPIO19, GPIO20** — native USB (D-/D+), in use by HID/CDC. Never use
  as GPIO in this project.
- **GPIO26–GPIO32** — internal bus for the in-package flash/PSRAM
  (SPI0/1). Don't exist as usable GPIO on this board.
- **GPIO43, GPIO44** — UART0 TX/RX (default). Free in the current setup
  (the project uses native USB CDC, not UART0), but reserved for
  alternative serial debugging if ever needed.
- **GPIO48** — already the board's built-in RGB LED (NeoPixel)
  (`PIN_NEOPIXEL` in the core). Reusable if we give up the status LED,
  not recommended right now.

---

## 5. GPIOs still free

| GPIO | Note |
|---|---|
| GPIO1 | **Used** — WS2812 data output (8x8 matrix + strip), see `docs/SYSTEM_INTEGRATION.en.md`. No longer an input, it's the project's only addressable data output |
| GPIO33 | Free — confirm it's physically exposed on the specific module (only reserved for PSRAM/flash in Octal mode; this board uses Quad, `qspi_2m`, so the pin is available at the chip level) |
| GPIO34 | Free — same note as GPIO33 |
| GPIO35 | Free — same note |
| GPIO36 | Free — same note |
| GPIO37 | Free — same note |
| GPIO38 | Free |
| GPIO39 | Free |
| GPIO40 | Free |
| GPIO41 | Free |
| GPIO42 | Free |
| GPIO43 | Free (UART0 TX, see note above) |
| GPIO44 | Free (UART0 RX, see note above) |
| GPIO45 | Free with caution (strapping — avoid unless there's a reason) |
| GPIO46 | Free with caution (strapping, input-only) |
| GPIO47 | Free |
| GPIO48 | Free with caution (built-in RGB LED) |

This leaves comfortable headroom for future expansion (paddle shifters,
analog handbrake, clutch/pedal potentiometers, etc.) without needing to
redesign this allocation — including the MCP23017's 2 free channels and
the 74HC4067's 7 free channels, before even touching extra GPIO.

---

## 6. Outputs / actuators (outside the scope of inputs, recorded because it affects the GPIO budget)

### Start Engine button LED

Illuminated momentary button (chrome ring, red "ENGINE START" cap,
internal LED), a common model from Chinese sim racing sites, **3
terminals**: the LED and the switch share a common pin (COM), which is
part of both the switch circuit and the LED's cathode.

Identifying the 3 pins with a multimeter (before soldering):
1. Continuity mode, test the 3 pairs **with the button pressed** — the
   pair that closes only at that moment is COM + the switch pin.
2. Diode mode, test the remaining pairs without pressing — the pair that
   lights up faintly and shows ~1.8–2.2V (in one polarity) is the LED;
   red probe in this test = anode (+), black probe = cathode (COM).
3. The pin that shows up in both tests is the shared COM.

Wiring:

| Button terminal | Goes to | Note |
|---|---|---|
| COM (common, switch+LED) | GND | A single GND wire serves both circuits |
| Switch (NO) | MCP23017 GPB3 | Already allocated in section 1/2 — MCP internal pull-up, reads LOW when pressed |
| LED (anode, +) | ESP32-S3 **GPIO2**, through a **220 Ω** resistor (or 150 Ω for more brightness) | Dedicated digital output — lets the firmware decide when to light it up (always on, only with ignition in ON/IGN, blinking during cranking, etc.) instead of wiring it straight to a power rail |

Resistor calculated for the ESP32-S3 GPIO's 3.3V (don't use the 5V rail
directly on the GPIO): `R = (3.3V − Vf_LED) / I_desired`, with a typical
red LED `Vf` ≈ 2.0V and a target current of 6–9 mA → 220–150 Ω. Don't use
a resistor smaller than ~100 Ω, to avoid exceeding the ESP32-S3's safe
per-pin current.

GPIO2 comes off the "free" list (section 5) — reserved for this LED.

---

## 7. Mapping logic — parking brake (Euro Truck Simulator)

Behavior confirmed by the user: the lever is a **stable** 2-position
switch (not momentary like the ignition key) — **down = parking brake
engaged, up = brake released**.

Point of attention for when the firmware gets ported: Euro/American Truck
Simulator's default bind for the parking brake is a **toggle** (the key/
button flips the state on each press, it doesn't hold a level). A
physical 2-stable-position switch doesn't directly match that model — if
the firmware simply mirrored the switch's level onto the HID bit (low
level = bit held at 1, high level = bit held at 0), the game would
receive that as "holding the button down", which isn't what a toggle
expects.

**Decided with the user: option 1.** The firmware generates a pulse only
on the switch's state transition (it doesn't mirror the level):
- **up→down** edge: fires a single "toggle" pulse — assumes this engages
  the brake in the game;
- **down→up** edge: fires another single "toggle" pulse — assumes this
  releases the brake.

This keeps the lever's physical position always consistent with the
brake's state in the game, as long as the two start in sync (e.g., a
session always starts with the lever down / brake engaged, which matches
the real default of a parked truck). Not implemented yet — this is only
the behavior spec, per the current scope (architecture/pinout only).

---

## 8. Mapping logic — encoders (decided: fixed virtual +/-)

**Decided with the user: the 4 KY-040 encoders always run in fixed
virtual +/- button mode, with no axis mode and no mode switching.** There
will be no toggle combo or mode-feedback LED — deliberately dropped as
not worth the extra complexity (4 LEDs, wiring, state-sync logic) for a
use case most simulation games don't use (almost every relevant bind —
cruise control, wipers, mirror, radio, etc. — accepts a discrete key/
button; very few accept an axis directly).

Behavior (not implemented yet, only specified):
- Each detent rotated **clockwise** generates a momentary pulse on a
  virtual "+" button (press and release).
- Each detent **counter-clockwise** generates a momentary pulse on a
  separate virtual "−" button.
- Therefore each encoder occupies **2 bits of the HID button bitmask**
  (one for +, one for −), plus the bit for its own SW (click), which is
  already mapped as a normal button on the 74HC4067 (section 1/3).
- No axis, no alternate mode, no mode-status LED — closed decision for
  this project.

---

## 9. HID bit budget (capacity check)

The firmware's current HID report descriptor (`docs/ARCHITECTURE.md`
item 7) **must not be altered** — it exposes a fixed 32-button bitmask
(`uint32_t buttons`). Check that everything decided so far fits within
that limit without touching the descriptor:

| Source | Bits |
|---|---|
| Push buttons 1–11 | 11 |
| SW of the 4 encoders (click) | 4 |
| Start Engine | 1 |
| Ignition — ON | 1 |
| Ignition — IGN | 1 |
| Toggle switches 1–4 | 4 |
| Parking brake (toggle pulse) | 1 |
| Encoders — virtual +/- button (4 × 2) | 8 |
| **Total used** | **31** |
| Free bits in the current HID | **1** |

Fits within the existing 32 bits, with no need to change the HID
descriptor — compatible with the "don't alter HID/USB descriptors"
constraint inherited from the earlier baseline. Only **1 free bit**
remains in the current descriptor.

**Expansion option confirmed by the user**: if the 32-bit budget turns
out to be insufficient in the future, the user already has a reference
profile/descriptor with **64 button slots** available to use as a base.
In other words, the 32-bit limit **isn't final** — it's just what the
current firmware uses; expanding to 64 buttons is a real, already
available option, whenever/if it's needed.

Important: this **is**, by definition, a change to the HID report
descriptor (the `USBHIDGamepad`'s `uint32_t buttons` would have to become
a larger bitmask — 64 bits, typically 2× `uint32_t` or a `uint8_t[8]` in
a custom report, since the Arduino-ESP32 core's `USBHIDGamepad` lib used
today is fixed at 32 buttons via `TUD_HID_REPORT_DESC_GAMEPAD`).
Therefore this change:
- **Isn't necessary now** — the current mapping fits within the existing
  32 bits.
- When it does happen, it's a deliberate, isolated firmware decision
  (swapping the report descriptor + the sending logic), not an
  incidental change — exactly the kind of change the baseline asks to be
  treated with caution, just now with the user's explicit authorization
  to make it **if and when** the 32nd bit ends up being insufficient.

---

## 10. HID integration — definitive INPUT LOGICAL ID → HID bit map

**Implemented.** The `lib/inputs` layer (INPUT_*) was integrated into the
main firmware (`src/main.cpp`), replacing the simulated/heartbeat values
with the real state of the 31 inputs. The HID report descriptor **was not
altered** (still `uint32_t buttons` via `USBHIDGamepad`, 32 bits, no
axes/hat used) — the 31 IDs fit exactly within the space already budgeted
in section 9 above.

Map rule: **bit = the numeric value of `InputId`** (0-30) — the enum is
already 0-based and sequential in `lib/inputs/inputs.h`, so there's no
separate indirection table to go stale. The full table (identical to the
comment at the top of `src/main.cpp`):

| INPUT LOGICAL ID | Bit | Button in joy.cpl | Type |
|---|---|---|---|
| INPUT_BUTTON_01..11 | 0-10 | 1-11 | level |
| INPUT_BUTTON_12 (encoder 1 SW) | 11 | 12 | level |
| INPUT_BUTTON_13 (encoder 2 SW) | 12 | 13 | level |
| INPUT_BUTTON_14 (encoder 3 SW) | 13 | 14 | level |
| INPUT_BUTTON_15 (encoder 4 SW) | 14 | 15 | level |
| INPUT_IGNITION_ON | 15 | 16 | level |
| INPUT_IGNITION_IGN | 16 | 17 | level |
| INPUT_START_ENGINE | 17 | 18 | level |
| INPUT_HANDBRAKE | 18 | 19 | level |
| INPUT_KILL_SWITCH_01..04 | 19-22 | 20-23 | level |
| INPUT_ENCODER_01_CW / _CCW | 23 / 24 | 24 / 25 | pulse |
| INPUT_ENCODER_02_CW / _CCW | 25 / 26 | 26 / 27 | pulse |
| INPUT_ENCODER_03_CW / _CCW | 27 / 28 | 28 / 29 | pulse |
| INPUT_ENCODER_04_CW / _CCW | 29 / 30 | 30 / 31 | pulse |
| bring-up heartbeat (temporary) | 31 | 32 | level |

**Level** inputs (buttons/switches/ignition): the already-debounced
`inputs_get_state()` is mirrored directly onto the bit — no extra logic
in `main.cpp`.

**Encoder** inputs (CW/CCW): since `lib/inputs` only exposes an EVENT for
rotation (never a "held level"), `main.cpp` translates each pending event
into a **momentary pulse** on the bit: it rises for 30ms, falls and stays
LOW for at least 20ms before the next pulse can start
(`updateEncoderPulses()`). This guarantees a rising AND a falling edge
visible to the host even with several detents in quick succession — the
game/SimHub always sees "press and release" an exact number of times,
never a "stuck" button.

**BOOT no longer feeds the HID.** The old `BOOT -> bit 0 (Button 1)` was
a bring-up simulated value (pre-real-hardware); it was removed and bit 0
is now the real `INPUT_BUTTON_01`, coming from the MCP23017. The board's
physical BOOT button still exists only for the gesture that opens the
WiFi portal (hold for 5s) — that's WiFi logic, untouched.

**Heartbeat (bit 31 / Button 32): kept on purpose.** It should only be
removed from `main.cpp` once the 31 real controls above are demonstrably
working on the physical bench (hardware wasn't assembled yet at the time
of this integration) — an explicit decision, not an oversight.

### What's left to close this stage's success criterion

Test on `joy.cpl`, with the hardware physically assembled:
- each of the 31 buttons individually;
- multiple simultaneous buttons (no "ghosting" — there shouldn't be any,
  since there's no matrix, but worth confirming);
- the 4 encoders, CW and CCW direction for each;
- the 4 toggle switches, the ignition (3 positions), and the parking
  brake;
- the Start Engine button;
- confirm the HID keeps responding normally with the computer without
  SimHub open (the firmware doesn't depend on it — SimHub just consumes
  the HID that Windows already exposes).
