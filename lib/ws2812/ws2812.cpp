#include "ws2812.h"
#include <Arduino.h>
#include "esp32-hal-rmt.h"

namespace {

rmt_obj_t *g_rmt = nullptr;

// Buffer de bits RMT, tamanho fixo pro teto de LEDs (24 bits = 3 bytes
// GRB por pixel). 256*24*4 bytes = 24KB — folga confortável no RAM da
// ESP32-S3 (320KB), reservada uma única vez, sem alocação por frame.
rmt_data_t g_bits[WS2812_MAX_LEDS * 24];

// Timing idêntico ao neopixelWrite() do core (mesmo tick de 100ns, mesmas
// durações) — já validado pela Espressif para WS2812/WS2812B, só
// generalizado aqui pra escrever N pixels numa única chamada de
// rmtWrite() em vez de sempre 24 bits fixos (1 pixel).
constexpr uint8_t T0H_TICKS = 4; // 0.4us
constexpr uint8_t T0L_TICKS = 8; // 0.8us
constexpr uint8_t T1H_TICKS = 8; // 0.8us
constexpr uint8_t T1L_TICKS = 4; // 0.4us

} // namespace

bool ws2812_init(uint8_t pin) {
  g_rmt = rmtInit(pin, RMT_TX_MODE, RMT_MEM_64);
  if (!g_rmt) {
    return false;
  }
  rmtSetTick(g_rmt, 100); // 100ns por tick, mesma base do neopixelWrite()
  return true;
}

void ws2812_show(const Ws2812Color *colors, uint16_t count) {
  if (!g_rmt || colors == nullptr) {
    return;
  }
  if (count > WS2812_MAX_LEDS) {
    count = WS2812_MAX_LEDS;
  }

  uint32_t bit = 0;
  for (uint16_t i = 0; i < count; i++) {
    // WS2812 espera a ordem GRB no fio, não RGB.
    const uint8_t channels[3] = {colors[i].g, colors[i].r, colors[i].b};
    for (uint8_t c = 0; c < 3; c++) {
      for (int8_t b = 7; b >= 0; b--) {
        const bool one = (channels[c] >> b) & 0x1;
        g_bits[bit].level0    = 1;
        g_bits[bit].duration0 = one ? T1H_TICKS : T0H_TICKS;
        g_bits[bit].level1    = 0;
        g_bits[bit].duration1 = one ? T1L_TICKS : T0L_TICKS;
        bit++;
      }
    }
  }

  rmtWrite(g_rmt, g_bits, bit); // assincrono — HW RMT transmite em segundo plano
}
