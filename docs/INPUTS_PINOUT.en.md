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

> **Revision 2 (Sep 2026) — the pinout changed.** Revision 1 put the 8
> quadrature signals on direct GPIO and the five 74HC4067 control lines on
> GPIO 15, 16, 17, 18 and 21. On the **ESP32-S3 SuperMini** those five are
> not header pins: they come out as pads on the underside of the module,
> beneath the board body, which makes hand soldering impractical. Everything
> from GPIO15 up is like that.
>
> **Decision (option B, chosen by the user):** the encoders move to the
> **MCP23017 port A** and the 74HC4067 takes over the button lines, using the
> header pins the encoders freed. Result: **no signal above GPIO14**, with
> GPIO 11, 12 and 13 still spare. Both ICs are the same as before — no new
> parts.

- **MCP23017 (I2C)**: carries the 8 **CLK/DT encoder signals** (port A) and,
  on port B, each **encoder's SW** plus the **4 toggle switches**. The SW
  lines are here for wiring reasons, not electrical ones: they leave the same
  KY-040 connector, so they follow their own encoder instead of crossing the
  enclosure to the mux.
- **74HC4067 (16-channel mux)**: carries the **11 push buttons**, **Start
  Engine**, the **2 ignition contacts** and the **parking brake
  microswitch** — 15 of the 16 channels. All mechanical contacts, which a
  polled scan serves with room to spare.
- **Direct ESP32-S3 GPIO**: only what *needs* a pin is left — I2C (8/9), the
  five mux control lines (4-7 and 10), WS2812 (1), the Start Engine LED (2)
  and BOOT (0).

### The price of this trade (read before touching the sampling)

Quadrature on GPIO had edge interrupts: **no transition is ever missed**, the
hardware wakes the MCU on each one. Over I2C there is no such thing —
transitions only show up if a sample lands between them.

A KY-040 has 20 detents per revolution and 4 Gray transitions per detent. At a
fast hand spin (~1.5 rev/s = 30 detents/s) a detent lasts ~33 ms and its 4
transitions sit ~5-10 ms apart. Sampling at **1 ms** catches them all with
margin. At 5 ms, two neighbouring transitions land in the same sample, the
quadrature state machine sees a 2-bit jump (impossible on a real contact),
treats it as noise and **drops the detent** — it never invents an event in the
wrong direction, but the click simply does not come out.

The main `loop()` ends with `delay(5)`, i.e. it would sample at 200 Hz. That
is why the MCP23017 read **left the loop** and became a **dedicated task**
(`lib/inputs/inputs.cpp`, core 0, priority above the loop), with its period in
`BOARD_MCP_SAMPLE_PERIOD_MS`. A 2-byte read at 400 kHz costs ~150 µs — about
15% of a bus that has no other device on it.

Once that task is up, **it owns I2C exclusively**: `inputs_update()`, in the
loop, only reads the cache it maintains. Do not call
`input_expander_update()` from anywhere else.

The **MCP23017 INT pin is reserved on GPIO14** and is worth wiring now even
though it is unused: if sampling ever becomes event-driven, it is just
`GPINTEN` on the chip plus an `attachInterrupt` — no board respin.

- Both expanders keep some headroom (no free MCP pin, 1 free mux channel and
  3 free header GPIOs) for future expansion.

---

## 1. Input table

| INPUT | HARDWARE | PIN | TYPE | NOTES |
|---|---|---|---|---|
| Encoder 1 — CLK | MCP23017 | GPA0 | Digital, sampled at 1 kHz | Quadrature; decode CLK+DT together, no software debounce (debounce is inherent to the quadrature state machine). **Read through the RAW path** of `input_expander`, outside the 15 ms debounce. Output mapped as a fixed virtual +/- button — see section 8 |
| Encoder 1 — DT | MCP23017 | GPA1 | Digital, sampled at 1 kHz | See CLK note |
| Encoder 1 — SW | MCP23017 | GPB0 | Digital, polling | Encoder click; firmware debounce (15 ms) |
| Encoder 2 — CLK | MCP23017 | GPA2 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 2 — DT | MCP23017 | GPA3 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 2 — SW | MCP23017 | GPB1 | Digital, polling | Firmware debounce |
| Encoder 3 — CLK | MCP23017 | GPA4 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 3 — DT | MCP23017 | GPA5 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 3 — SW | MCP23017 | GPB2 | Digital, polling | Firmware debounce |
| Encoder 4 — CLK | MCP23017 | GPA6 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 4 — DT | MCP23017 | GPA7 | Digital, sampled at 1 kHz | Quadrature |
| Encoder 4 — SW | MCP23017 | GPB3 | Digital, polling | Firmware debounce |
| Push button 1 | 74HC4067 | C0 | Digital, polling | ESP32 internal pull-up on the SIG line |
| Push button 2 | 74HC4067 | C1 | Digital, polling | idem |
| Push button 3 | 74HC4067 | C2 | Digital, polling | idem |
| Push button 4 | 74HC4067 | C3 | Digital, polling | idem |
| Push button 5 | 74HC4067 | C4 | Digital, polling | idem |
| Push button 6 | 74HC4067 | C5 | Digital, polling | idem |
| Push button 7 | 74HC4067 | C6 | Digital, polling | idem |
| Push button 8 | 74HC4067 | C7 | Digital, polling | idem |
| Push button 9 | 74HC4067 | C8 | Digital, polling | idem |
| Push button 10 | 74HC4067 | C9 | Digital, polling | idem |
| Push button 11 | 74HC4067 | C10 | Digital, polling | idem |
| Start Engine (push) | 74HC4067 | C11 | Digital, polling | A full 15-channel scan takes ~450 µs, spread over several `mux4067_scan()` calls — imperceptible for a button |
| Ignition — ON position | 74HC4067 | C12 | Digital, polling | Confirmed with the user: scooter key, 3 physical positions (1=OFF, 2=ON, 3=IGN/crank). The ON contact closes at position 2 and **stays closed** at position 3 (it does not open during cranking) |
| Ignition — IGN position | 74HC4067 | C13 | Digital, polling | **Momentary**: closed only while the key is held at position 3; the key is spring-returned and falls back to position 2 on release — this contact is not bistable, firmware must treat it as a pulse, not a state |
| Parking brake microswitch | 74HC4067 | C14 | Digital, polling | State (not pulse) — down=brake engaged, up=brake released. See mapping logic in section 7 |
| Toggle switch 1 (ON/OFF) | MCP23017 | GPB4 | Digital, polling | State, not urgent |
| Toggle switch 2 (ON/OFF) | MCP23017 | GPB5 | Digital, polling | State, not urgent |
| Toggle switch 3 (ON/OFF) | MCP23017 | GPB6 | Digital, polling | State, not urgent |
| Toggle switch 4 (ON/OFF) | MCP23017 | GPB7 | Digital, polling | State, not urgent |

Total: 31 physical signals (12 from the encoders + 11 push buttons + 1
microswitch + 2 ignition + 1 start + 4 toggle switches). 16 on the MCP23017
(full) and 15 on the 74HC4067 (1 channel free).

---

## 2. Full MCP23017 map (I2C)

| Pin | Use |
|---|---|
| GPA0 | Encoder 1 — CLK |
| GPA1 | Encoder 1 — DT |
| GPA2 | Encoder 2 — CLK |
| GPA3 | Encoder 2 — DT |
| GPA4 | Encoder 3 — CLK |
| GPA5 | Encoder 3 — DT |
| GPA6 | Encoder 4 — CLK |
| GPA7 | Encoder 4 — DT |
| GPB0 | Encoder 1 — SW |
| GPB1 | Encoder 2 — SW |
| GPB2 | Encoder 3 — SW |
| GPB3 | Encoder 4 — SW |
| GPB4 | Toggle switch 1 |
| GPB5 | Toggle switch 2 |
| GPB6 | Toggle switch 3 |
| GPB7 | Toggle switch 4 |

All 16 pins are taken. Future expansion goes through the free C15 mux
channel, the free GPIO 11/12/13, or a second MCP23017 on the same bus
(`0x21`, A0 to 3V3) — which costs no new pin at all.

**Port A is a quadrature path, not a button path.** The firmware reads
GPA0-GPA7 through the **raw** accessor (`input_expander_get_raw()`), outside
the layer's 15 ms debounce window — debouncing there would erase exactly the
transitions that make up a detent. Port B goes through debounce normally.

Configuration applied by `mcp23017_init()`: all 16 pins as inputs with the
internal pull-up (~100 kΩ) enabled, no polarity inversion (`IPOL = 0`, keeping
open=HIGH), `SEQOP` enabled so GPIOA+GPIOB come back in a single I2C
transaction, and `IOCON.MIRROR = 1` (INTA/INTB mirrored).

`MIRROR` is already on, but **INT is neither used nor wired**: sampling is
periodic (dedicated task, see "The price of this trade" at the top). Moving to
event-driven reads later would require enabling `GPINTEN` on the desired pins
with `INTCON = 0` (interrupt-on-change, not compare-against-`DEFVAL`), a new
wire from INTA to a free header GPIO, and an `attachInterrupt` on it. In other
words: **that is a hardware change**, not just firmware.

> Careful if you do: if a change happens between the GPIO read and the re-arm,
> INT can latch active and no new edge ever arrives. Whoever implements it must
> keep a safety poll in parallel. That race is precisely why periodic sampling
> was chosen first.

I2C address: 7 bits, `0x20`–`0x27` depending on the chip's A0/A1/A2
address pins (wire according to the board; with a single MCP, just tie
A0/A1/A2 to GND → address `0x20`).

---

## 3. Full 74HC4067 map (digital mux)

| Channel | Use |
|---|---|
| C0 | Push button 1 |
| C1 | Push button 2 |
| C2 | Push button 3 |
| C3 | Push button 4 |
| C4 | Push button 5 |
| C5 | Push button 6 |
| C6 | Push button 7 |
| C7 | Push button 8 |
| C8 | Push button 9 |
| C9 | Push button 10 |
| C10 | Push button 11 |
| C11 | Start Engine |
| C12 | Ignition — ON |
| C13 | Ignition — IGN |
| C14 | Parking brake microswitch |
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

The 74HC4067 has no pull-up of its own (unlike the MCP23017), but the
firmware enables the ESP32's internal pull-up on the SIG line, which serves
every channel — no per-input resistor is needed. Details and the one
optional recommendation are in section 11.

---

### Ignition key logic (confirmed with the user)

Scooter-style key, 3 physical positions — **not** an ordinary 3-stable-
state rotary switch:

| Physical position | C12 (ON) | C13 (IGN) | Mechanical behavior |
|---|---|---|---|
| 1 — OFF | inactive | inactive | Stable — stays until the user moves it |
| 2 — ON | **active** | inactive | Stable — stays until the user moves it |
| 3 — IGN/start | **active** | **active** | **Momentary** — spring return; releases on its own and the key returns to position 2 as soon as the user lets go |

Consequences for the firmware (not implemented yet, only recorded here
for when the logic gets ported):
- C13 (IGN) must be treated as a **start pulse**, not a state — it never
  stays "latched" active on its own, always returns to 0 once the user's
  hand leaves the key.
- If the user moves the key directly from position 3 to position 1
  (skipping position 2), the firmware sees C12 and C13 drop practically
  together — the vehicle should be treated as OFF in that case, exactly
  as if it had passed through ON first.
- There's no valid combination of "IGN active with ON inactive" on this
  key — if that's ever read, it's a transient electrical transition
  (debounce), not a real state to report.

---

## 4. Direct ESP32-S3 GPIOs used

| GPIO | Function |
|---|---|
| GPIO0 | Board BOOT button — only the "hold 5 s to open the WiFi portal" gesture. Does not feed HID |
| GPIO1 | WS2812 — chain data (8x8 matrix + strip), see `docs/SYSTEM_INTEGRATION.md` |
| GPIO2 | Start Engine button LED (output, through 220 Ω) — reserved, firmware does not drive it yet |
| GPIO4 | 74HC4067 — S0 |
| GPIO5 | 74HC4067 — S1 |
| GPIO6 | 74HC4067 — S2 |
| GPIO7 | 74HC4067 — S3 |
| GPIO8 | I2C SDA (MCP23017) — Arduino-ESP32 core default (`pins_arduino.h`) |
| GPIO9 | I2C SCL (MCP23017) — core default |
| GPIO10 | 74HC4067 — SIG |

10 GPIOs used, **all within GPIO0-10**. No underside pad is required.

> **The MCP23017 INT is not wired.** An earlier version of this document
> reserved GPIO14 for it. That was doubly wrong: the firmware never enables
> `GPINTEN` nor attaches an interrupt (sampling is periodic, see "The price
> of this trade"), and **GPIO14 is also an underside pad** on this board —
> exactly what this revision exists to avoid. Leave the MCP23017 INTA and
> INTB unconnected.

Pins deliberately **avoided** in this allocation:
- **GPIO3, GPIO45, GPIO46** — ESP32-S3 strapping pins (affect boot mode / flash
  voltage); GPIO46 is also *input-only*.
- **GPIO19, GPIO20** — native USB (D-/D+), in use by HID/CDC. Never use as GPIO
  in this project.
- **GPIO15-18, GPIO21 and everything from GPIO33 up** — they exist on the chip,
  but on the SuperMini they come out as underside pads. This is the constraint
  that drove this revision; do not go back to them without changing boards.
- **GPIO26-GPIO32** — internal bus to the in-package flash/PSRAM (SPI0/1). Not
  usable as GPIO on this board.
- **GPIO43, GPIO44** — UART0 TX/RX (default). Free in current use (the project
  uses native USB CDC, not UART0), but reserved for alternative serial
  debugging if ever needed.
- **GPIO48** — the board's built-in RGB LED (`PIN_NEOPIXEL` in the core).

---

## 5. GPIOs still free

| GPIO | Notes |
|---|---|
| GPIO11 | **Free on header** — first candidate for any new input/output |
| GPIO12 | **Free on header** |
| GPIO13 | **Free on header** |
| GPIO3 | Present on header, but it is a strapping pin — avoid without a reason |
| GPIO15-18, 21, 33-48 | Present on the chip; on the SuperMini they are underside pads. Treat as unavailable |

Three free header pins, plus mux channel C15, plus the option of a second
MCP23017 on the same I2C (16 inputs at zero pin cost). That comfortably covers
paddle shifters, an analog handbrake or a pedal set — bearing in mind that on
the HID side the bit budget is the real limit (section 9).

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
| Switch (NO) | 74HC4067 C11 | Already allocated in sections 1/3 — ESP32 internal pull-up on the SIG line, reads LOW when pressed |
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
| **free** (was the bring-up heartbeat) | 31 | 32 | — |

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

**Heartbeat (bit 31 / Button 32): REMOVED.** It existed only to prove the HID
was alive while the hardware was not assembled. It went away once the 31 real
controls started responding — a button that fires by itself every second is
exactly what a game or SimHub binds by mistake during auto-learn. Bit 31 is now
**free** and is the first slot available for a new control (the descriptor still
declares 32 buttons).

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

---

## 11. Wiring / soldering diagram — GND or VCC?

**General rule: every button/switch has one terminal on the input pin/channel
and the other terminal on GND. Never on VCC.** The logic is *active-low*: at
rest the pin reads `HIGH` (pulled up by a pull-up), and when the contact
closes it's pulled to GND and reads `LOW`. Everything runs at 3.3 V. What
differs between subsystems is *who provides the pull-up* — and in most cases
**you don't solder any resistor at all**, because the firmware enables
internal pull-ups.

> **Correction (2026-09-24).** An earlier version of this document (and a
> comment in `lib/mux4067`) said **each** 74HC4067 input needed an external
> 10 kΩ resistor. That was overstated: the firmware already enables the
> ESP32's internal pull-up on the SIG line (`lib/mux4067/mux4067.cpp`,
> `pinMode(g_sig, INPUT_PULLUP)`), and the mux connects the selected channel
> to SIG, so that one pull-up already serves every channel. No per-input
> resistor is needed.

### Power and control connections (what usually makes "nothing work")

| Chip | Pin | Goes to | Notes |
|---|---|---|---|
| ESP32-S3 | 3V3 / GND | 3.3 V rail / common GND | everything referenced to the same GND |
| MCP23017 | VDD / VSS | 3.3 V / GND | |
| MCP23017 | **RESET** | **3.3 V** | do not leave floating — the chip can sit stuck in reset. Some breakouts already handle this; check yours |
| MCP23017 | A0, A1, A2 | **GND** | I2C address `0x20` (what `board_config.h` expects) |
| MCP23017 | SDA / SCL | GPIO8 / GPIO9 | need I2C pull-ups (~4.7 kΩ → 3.3 V). Breakouts usually have them; a bare chip does not. **With quadrature now riding on I2C, a marginal bus stops being "a button glitches sometimes" and becomes a dropped detent** — if in doubt, fit the 4.7 kΩ |
| MCP23017 | INTA and INTB | **leave unconnected** | the firmware does not use the expander's interrupt; sampling is periodic. On many breakouts (the CJMCU-2317 included) those outputs do not even reach the header |
| 74HC4067 | VCC / GND | 3.3 V / GND | |
| 74HC4067 | **EN (/E)** | **GND** | active low: at `HIGH` (or floating) it disables every channel. Some boards already ground it; check yours |
| 74HC4067 | S0 / S1 / S2 / S3 | GPIO4 / GPIO5 / GPIO6 / GPIO7 | per `include/board_config.h` |
| 74HC4067 | SIG | GPIO10 | idem |
| KY-040 | `+` | **3.3 V (not 5 V)** | the module's pull-up hangs off this pin; at 5 V it would inject 5 V into the MCP23017 inputs |

> **Wire length matters more than before.** The 8 CLK/DT lines now land on the
> MCP23017, and SDA/SCL carries the quadrature. Keep the MCP23017 close to the
> ESP32 (short I2C) and run the long wires to the encoders, not to the bus.

### Push buttons 1–11, Start Engine, Ignition, parking brake (74HC4067)

Bare switches (two terminals). The firmware enables the ESP32 internal pull-up
on the SIG line (`pinMode(SIG, INPUT_PULLUP)`), and the mux connects the
selected channel to SIG — **that single pull-up serves every channel**. No
per-input resistor.

```
74HC4067 channel Cx ──→  Switch terminal 1
                         Switch terminal 2 ──→  GND
```

| Channel | What to wire |
|---|---|
| C0–C10 | Push buttons 1 to 11 |
| C11 | Start Engine (switch NO terminal; COM goes to GND) |
| C12 | Ignition — ON contact |
| C13 | Ignition — IGN contact (crank) |
| C14 | Parking brake microswitch |
| C15 | free |

Logic: the channel reads `HIGH` at rest → `LOW` when closed to GND.

**Optional, only if `mux-test` shows unstable readings** (more likely with long
wires): **a single** 10 kΩ resistor between the **SIG line (GPIO10)** and
3.3 V. Since SIG is common to every channel, that one resistor serves all
fifteen. *This is an expectation from circuit physics; it has not been measured
on assembled hardware — test without it, and only add it if you need it.*

**Parking brake:** the firmware treats "contact closed to GND" as engaged.
Which lever position closes the contact depends on whether you use the
microswitch NO or NC terminal; if it ends up inverted, swap the terminal.

### Ignition (3 positions, scooter key switch) — 74HC4067

```
COM (common)            ──→  GND
ON contact              ──→  74HC4067 C12
IGN contact (crank)     ──→  74HC4067 C13
```

Before wiring, confirm with a multimeter that the ON contact stays closed at
position 3 (crank) — see section 3.

### Start Engine button (3 terminals) and its LED

```
COM            ──→  GND               (serves both the switch and the LED)
Switch (NO)    ──→  74HC4067 C11
LED anode (+)  ──→  220 Ω ──→  ESP32-S3 GPIO2
```

Resistor for 3.3 V: `R = (3.3 V − Vf) / I` with `Vf ≈ 2.0 V` (red LED) and
`I ≈ 6–9 mA` → 220–150 Ω. Recommended minimum: 100 Ω.

> **Note:** the firmware **does not drive GPIO2 yet**. Wired like this the LED
> will not light on its own — the logic (e.g. light it with ignition ON) is
> still unimplemented.

### KY-040 encoders — CLK, DT and SW, all on the MCP23017

The MCP23017 has an internal pull-up (~100 kΩ) on all 16 pins, and the KY-040
module already carries its own on the PCB. No resistor to solder.

```
KY-040 #1   CLK ──→ GPA0    DT ──→ GPA1    SW ──→ GPB0
KY-040 #2   CLK ──→ GPA2    DT ──→ GPA3    SW ──→ GPB1
KY-040 #3   CLK ──→ GPA4    DT ──→ GPA5    SW ──→ GPB2
KY-040 #4   CLK ──→ GPA6    DT ──→ GPA7    SW ──→ GPB3
all         GND ──→ GND     +  ──→ 3.3 V
```

Check your module with a multimeter (between `SW` and `+` should read ~10 kΩ);
some clones lack the `SW` pull-up — the MCP23017 internal one covers that case.

> **If one physical detent does not produce exactly one event**, suspect number
> one is now the sampling period (`BOARD_MCP_SAMPLE_PERIOD_MS`) or a marginal
> I2C bus — not the decoder. Run the isolated `encoder-test` before touching the
> quadrature state machine, which did not change at all in this revision.

### Toggle switches 1–4 — MCP23017

```
Toggle switch 1 ──→ GPB4      Toggle switch 3 ──→ GPB6
Toggle switch 2 ──→ GPB5      Toggle switch 4 ──→ GPB7
the other terminal of each ──→ GND
```

### WS2812 matrix + strip

**External 5 V** supply straight to the matrix/strip (~4.4 A peak with 74 LEDs
at full white — the ESP32 USB rail cannot take it), **common GND** with the
ESP32, data `GPIO1 → matrix DIN → matrix DOUT → strip DIN`. Recommended: a
~1000 µF capacitor between +5 V and GND near the first LED and a ~330–470 Ω
series resistor on the data wire.

### Quick-reference table

| Component | Terminal 1 goes to | Terminal 2 goes to | Pull-up |
|---|---|---|---|
| Push buttons 1–11 | 74HC4067 C0–C10 | **GND** | ESP32 internal, on the SIG line |
| Start Engine (switch) | 74HC4067 C11 | **GND** (via COM) | ESP32 internal, on the SIG line |
| Ignition — ON / IGN | 74HC4067 C12 / C13 | **GND** (via COM) | ESP32 internal, on the SIG line |
| Parking brake | 74HC4067 C14 | **GND** | ESP32 internal, on the SIG line |
| KY-040 CLK / DT | MCP23017 GPA0–GPA7 | KY-040 module (GND / `+`) | KY-040 PCB + MCP internal |
| Encoder SW | MCP23017 GPB0–GPB3 | KY-040 module (GND / `+`) | KY-040 PCB + MCP internal |
| Toggle switches 1–4 | MCP23017 GPB4–GPB7 | **GND** | MCP internal (~100 kΩ) |
| Start Engine (LED) | GPIO2 through 220 Ω | **GND** (via COM) | — (n/a; GPIO2 is not driven yet) |
