#include "input_expander.h"
#include "mcp23017.h"
#include <Arduino.h> // millis()

namespace {
bool g_connected = false;

// g_rawPrev: última amostra crua lida do MCP23017, convenção do chip
// (bit=1 => pino em HIGH => switch aberto). Usada só para detectar quando
// um bit MUDOU de valor (e então reiniciar o timer de debounce daquele bit).
uint16_t g_rawPrev = 0xFFFF;

// g_stableRaw: estado já debounced, MESMA convenção do chip (1=aberto).
// Convertido para "1=pressionado" só na hora de sair por
// input_expander_get_state()/get_bit(), pra manter a inversão de
// polaridade num único lugar.
uint16_t g_stableRaw = 0xFFFF;

// Timestamp (millis) da última vez que cada um dos 16 bits mudou de valor
// na leitura crua. Usado para saber se uma mudança já "descansou" o
// suficiente (INPUT_EXPANDER_DEBOUNCE_MS) para virar estado estável.
uint32_t g_lastChangeMs[16] = {0};
} // namespace

bool input_expander_init(uint8_t i2c_addr, uint8_t sda_pin, uint8_t scl_pin) {
  g_connected = mcp23017_init(i2c_addr, sda_pin, scl_pin);

  // Primeira leitura define o estado inicial sem esperar debounce — evita
  // reportar "tudo pressionado" ou instável nos primeiros ciclos após o
  // boot.
  g_rawPrev   = mcp23017_read();
  g_stableRaw = g_rawPrev;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 16; i++) {
    g_lastChangeMs[i] = now;
  }

  return g_connected;
}

void input_expander_update() {
  const uint16_t raw = mcp23017_read();
  const uint32_t now = millis();

  for (uint8_t i = 0; i < 16; i++) {
    const bool rawBit  = (raw        >> i) & 0x1;
    const bool prevBit = (g_rawPrev  >> i) & 0x1;

    if (rawBit != prevBit) {
      g_lastChangeMs[i] = now; // reinicia a janela de debounce deste bit
    }

    const bool stableBit = (g_stableRaw >> i) & 0x1;
    if (rawBit != stableBit &&
        (now - g_lastChangeMs[i]) >= INPUT_EXPANDER_DEBOUNCE_MS) {
      if (rawBit) g_stableRaw |= (uint16_t)(1u << i);
      else        g_stableRaw &= (uint16_t)~(1u << i);
    }
  }

  g_rawPrev = raw;
}

uint16_t input_expander_get_raw() {
  // g_rawPrev guarda exatamente a amostra da ultima update(), na convencao
  // do chip (1 = aberto). Sem inversao aqui — ver o contrato no header.
  return g_rawPrev;
}

uint16_t input_expander_get_state() {
  // Inverte aqui: g_stableRaw usa a convenção do chip (1=aberto), o
  // contrato desta função é "1=pressionado".
  return (uint16_t)~g_stableRaw;
}

bool input_expander_get_bit(uint8_t index) {
  if (index >= 16) return false;
  return (input_expander_get_state() >> index) & 0x1;
}

bool input_expander_is_connected() {
  return g_connected;
}
