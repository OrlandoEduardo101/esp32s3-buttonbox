#include "led_idle.h"

namespace {

// HSV -> RGB em aritmética inteira (saturação sempre máxima).
// h: 0-255 (círculo de cores inteiro), v: 0-255.
LedIdleColor hsv(uint8_t h, uint8_t v) {
  const uint8_t region = h / 43;                       // 6 setores de 43
  const uint8_t rem    = (uint8_t)((h - region * 43) * 6); // 0-252 dentro do setor
  const uint8_t p = 0;
  const uint8_t q = (uint8_t)((v * (255 - rem)) >> 8);
  const uint8_t t = (uint8_t)((v * rem) >> 8);

  switch (region) {
    case 0:  return {v, t, p};
    case 1:  return {q, v, p};
    case 2:  return {p, v, t};
    case 3:  return {p, q, v};
    case 4:  return {t, p, v};
    default: return {v, p, q};
  }
}

// "Respirar": onda triangular de ~4 s que leva o brilho de ~40% a ~99% do
// teto LED_IDLE_BRIGHTNESS.
uint8_t breathingValue(uint32_t nowMs) {
  const uint16_t phase = (uint16_t)((nowMs >> 3) & 0x1FF); // 0-511 (~4,1 s)
  const uint16_t tri   = (phase < 256) ? phase : (uint16_t)(511 - phase); // 0-255
  const uint16_t scale = (uint16_t)(100 + ((tri * 155) >> 8));            // 100-254
  return (uint8_t)((LED_IDLE_BRIGHTNESS * scale) >> 8);
}

} // namespace

void led_idle_render(uint32_t nowMs, LedIdleColor *matrix64,
                     LedIdleColor *strip, uint16_t stripCount) {
  const uint8_t v    = breathingValue(nowMs);
  const uint8_t base = (uint8_t)((nowMs >> 4) & 0xFF); // ciclo de cor em ~4,1 s

  if (matrix64 != nullptr) {
    for (uint16_t i = 0; i < 64; i++) {
      const uint8_t x = (uint8_t)(i % 8);
      const uint8_t y = (uint8_t)(i / 8);
      matrix64[i] = hsv((uint8_t)(base + (x + y) * 16), v); // onda diagonal
    }
  }

  if (strip != nullptr) {
    for (uint16_t j = 0; j < stripCount; j++) {
      strip[j] = hsv((uint8_t)(base + j * 24), v);
    }
  }
}
