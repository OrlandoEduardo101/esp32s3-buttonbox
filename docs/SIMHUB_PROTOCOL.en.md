[🇧🇷 Português](SIMHUB_PROTOCOL.md) | 🇺🇸 English

# SIMHUB_PROTOCOL.md — SimHub integration over CDC

> Updated 2026-09-16, after a course correction. See the "Previous
> mistake" section at the end — the first implementation used the wrong
> protocol, and it's worth recording why, so no one repeats it.

## Goal

SimHub must recognize the board as a **standard Arduino device**, so that
**every LED effect is configured inside SimHub itself** — no behavior
(RPM, flags, shift light, pit limiter) is written into the firmware.

Split requested by the user:
- **8x8 matrix** → SimHub's "RGB Matrix" feature (iFlag etc.)
- **LED strip** → SimHub's "RGB Leds" feature (RPM etc.)

And **no `SHCustomProtocol`** (command `'P'`) — explicitly dropped.

## Sources (implementations that demonstrably work)

The protocol isn't published byte-by-byte in the official documentation.
It was extracted from two real implementations, both present on the
user's machine and both accepted by their SimHub install:

- `~/Projects/arduino/ESP-SimHub` (upstream) — `src/SHCommands.h`,
  `src/main.cpp` (dispatcher), `src/SHRGBLedsBase.h` (RGB stream format)
- `~/Projects/arduino/ESP-SimHub-ESP32S3-SCREEN` (fork in use) — same
  files, leaner version

## Framing — TWO layers

### 1) ARQ transport

SimHub doesn't send raw commands over the serial line. Everything travels
inside a packet with an id and a CRC8, and the device must acknowledge
each packet:

```
Host  -> device:  0x01 0x01 <packetId> <len(1..32)> <data...> <crc8>
Device -> host:   ACK  = 0x03 <packetId>
                  NACK = 0x04 <lastValidPacket> <reason>
```

The CRC8 is table-driven (256-byte table copied from the reference) and
covers `packetId`, `len`, and the data bytes. The packet is accepted if
`packetId` is the next one in sequence or `255` (broadcast).

Real example, from the reference code itself — Hello:

```
01 01 FF 03 03 31 10 6A
│  │  │  │  └──────┴── data: 0x03 (header) '1' (hello) 0x10 (trailer)
│  │  │  └── len = 3
│  │  └── packetId = 255 (broadcast)
│  └── header (repeated)
└── header
                                              crc8 = 0x6A
```

### 2) Commands (inside the ARQ data)

```
0x03 (MESSAGE_HEADER)  +  1 command char  +  command-specific payload
```

### Device responses (framed, outside the ARQ)

```
byte     -> 0x08 <byte>
string   -> 0x06 <len> <bytes> 0x20
string+\n -> 0x06 <len+1> <bytes> '\n' 0x20
```

A payload bigger than 32 bytes (the matrix's 192, for example) arrives
split across several ARQ packets — the parser pulls the next packets as
the command needs more bytes.

### Implemented commands

| Cmd | Name | Payload received | Response |
|---|---|---|---|
| `'1'` | Hello | 1 byte (trailer, discarded) | `0x08 'j'` |
| `'0'` | Features | — | `"NIXR\n"` |
| `'4'` | RGB LED count | — | 1 byte = **strip** LED count |
| `'6'` | RGB LED data | RGB stream | `0x15` (ACK) |
| `'R'` | RGB Matrix data | RGB stream (64 px) | `0x15` (ACK) |
| `'N'` | Device name | — | `"ESP32S3-ButtonBox\n"` |
| `'I'` | Unique ID | — | MAC in hex + `"\n"` |
| `'A'` | Acq | — | `0x03` |
| `'X'` | Expanded | string until `' '`/`'\n'` | `list` → list; `mcutype` → `1E 98 01`; else → `0x15` |
| `'J'` `'2'` `'B'` | Counters | — | `0x00` (buttons go through native HID) |
| `'G'` | Gear | 1 char | `0x15` |
| `'8'` | Baud rate | 1 byte (code) | — (irrelevant over USB CDC) |

### Advertised features

`N` (name) · `I` (unique id) · `X` (expanded commands) · `R` (RGB Matrix).

Deliberately **left out**: `P` (SHCustomProtocol, dropped by the user),
`J`/`G` (buttons and gear go through native HID, not this protocol),
`M`/`L`/`K`/`V` (displays and motors that don't exist here).

The strip ("RGB Leds") has no feature letter — SimHub discovers it from
the `'4'` command's response being greater than zero.

### RGB stream format (commands `'6'` and `'R'`)

Identical to `SHRGBLedsBase::read()` in the reference implementations:

```
mode = byte
while mode > 0:
    mode 1 -> all LEDs in sequence: (r,g,b) × ledCount
    mode 2 -> startLed, numLeds, then (r,g,b) × numLeds
    mode 3 -> startLed, numLeds, one (r,g,b) repeated over the range
    mode = byte
```

The stream ends when a `0` arrives (or the timeout expires).

## LED configuration

- **Matrix**: fixed at **64** (8x8). The protocol has no matrix-count
  command — the reference driver instantiates 64 fixed pixels.
- **Strip**: configurable at runtime via the serial command
  **`SETLEDS <n>`** (persisted in NVS, survives reboot and OTA). This is
  the value returned by command `'4'`.

## Physical LED layout

A **single WS2812 chain** on GPIO1: the **8x8 matrix first** (pixels
0-63), the **strip right after** (matrix DOUT → strip DIN). For SimHub
these remain two separate logical devices; the code that joins them into
a single physical chain lives only in `src/main.cpp`.

The matrix uses **serpentine** remapping (alternating rows reversed),
which is how most 8x8 WS2812 panels are wired. If your panel uses
straight rows instead, just flip `MATRIX_SERPENTINE` to `false` in
`src/main.cpp`.

## Connection behavior

With no command for **5 s**, the firmware considers SimHub disconnected
and clears both framebuffers (same idea as `Command_Shutdown` in the
reference implementations). This way the LEDs don't stay frozen on the
last color if SimHub closes.

## Implementation

- `lib/simhub/` — protocol parser + two framebuffers (matrix and strip)
- `lib/ws2812/` — WS2812 driver over RMT (knows nothing about SimHub or telemetry)
- `src/main.cpp` — dispatches `0x03` in the serial reader (coexisting
  with the text console), and the bridge from framebuffers to the
  physical chain
- `src/simhub_test.cpp` + `[env:simhub-test]` — isolated test
- `scripts/simhub_test_send.py` — speaks the real protocol and checks
  the responses, without needing to open SimHub

---

## Previous mistake (recorded on purpose)

The first implementation followed the `0xFF×6 + "proto"/"ledsc"/"sleds"`
protocol, documented in the official wiki as *"SIMHUB STANDARD ARDUINO
PRO MICRO LEDs sketch"*. That protocol **exists and was correctly
implemented** — the `scripts/simhub_test_send.py` of that time confirmed
`proto`, `ledsc`, and `sleds` working on the real board. But it belongs to
the **legacy LED-only sketch**, and it isn't what the SimHub "Arduino" tab
scanner speaks.

What proved the mistake: the user's SimHub log
(`Arduino scan COM27 ... Hello (sending)` → `Unrecognized (5x)`) against a
firmware that responded perfectly to the old protocol. SimHub was sending
`0x03 '1'` (Hello) and getting nothing back.

Lesson: the public documentation described a real protocol, but not *the*
protocol for the feature the user actually wanted to use. The verification
step that was missing was testing against the real consumer (SimHub
itself) instead of only against a script that spoke the protocol I had
implemented myself.

### Second correction: the ARQ layer was missing

The next version already used the right commands (`0x03` + char), but
still failed — because I was treating the `0x03` as if it arrived raw on
the serial line. What revealed the mistake was the stack trace in
SimHub's log: `ArqSerialLib.ArqSerial.Open()`. SimHub wraps everything in
the ARQ layer described above, and expects an ACK per packet. Without
that, the device never sent back anything SimHub would recognize.

### Third piece: the DTR gate

Independent of the protocol, there was a second real problem: the
Arduino-ESP32 core's `USBCDC::write()` **silently discards** any write
when `tud_cdc_n_connected()` is false — and that depends on the host
asserting **DTR**. SimHub opens the port without asserting DTR (asserting
it resets real Arduino boards). That's why the responses are now written
directly via `tud_cdc_n_write()`, which bypasses that gate. The text
console (`PING`/`SETLEDS`/`DUMPLEDS`) still runs over the normal `Serial`,
where terminals always assert DTR.
