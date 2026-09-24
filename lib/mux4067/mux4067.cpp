#include "mux4067.h"
#include <Arduino.h>

namespace {
uint8_t g_s0 = MUX4067_DEFAULT_S0_PIN;
uint8_t g_s1 = MUX4067_DEFAULT_S1_PIN;
uint8_t g_s2 = MUX4067_DEFAULT_S2_PIN;
uint8_t g_s3 = MUX4067_DEFAULT_S3_PIN;
uint8_t g_sig = MUX4067_DEFAULT_SIG_PIN;
uint8_t g_channelCount = MUX4067_MAX_CHANNELS;

// --- Maquina de varredura nao-bloqueante ---------------------------------
// Fases:
//   1. Canal g_currentChannel ja foi selecionado (S0-S3 programados) e
//      estamos esperando g_settleDeadlineUs (micros()) para poder ler.
//   2. Deadline vencida -> le SIG, alimenta o debounce daquele canal,
//      avanca para o proximo canal, reprograma S0-S3 e arma novo deadline.
// Cada chamada de mux4067_scan() faz NO MAXIMO uma dessas duas coisas, e
// nunca dorme — so compara micros()/millis() e volta.
uint8_t  g_currentChannel = 0;
uint32_t g_settleDeadlineUs = 0;

// g_rawPrev/g_stableRaw: mesma tecnica de debounce por tempo do
// input_expander (MCP23017), mas independente — este modulo nao inclui
// nem depende daquele.
// Convencao crua (igual ao pino fisico): bit=1 -> aberto (HIGH).
uint16_t g_rawPrev   = 0xFFFF;
uint16_t g_stableRaw = 0xFFFF;
uint32_t g_lastChangeMs[MUX4067_MAX_CHANNELS] = {0};

void applySelectPins(uint8_t channel) {
  digitalWrite(g_s0, (channel >> 0) & 0x1);
  digitalWrite(g_s1, (channel >> 1) & 0x1);
  digitalWrite(g_s2, (channel >> 2) & 0x1);
  digitalWrite(g_s3, (channel >> 3) & 0x1);
}

void armSettleWindow() {
  g_settleDeadlineUs = micros() + MUX4067_SETTLE_US;
}

void updateDebounceForChannel(uint8_t channel, bool rawBit) {
  const uint32_t now = millis();
  const bool prevBit = (g_rawPrev >> channel) & 0x1;

  if (rawBit != prevBit) {
    g_lastChangeMs[channel] = now; // reinicia a janela deste canal
  }

  const bool stableBit = (g_stableRaw >> channel) & 0x1;
  if (rawBit != stableBit &&
      (now - g_lastChangeMs[channel]) >= MUX4067_DEBOUNCE_MS) {
    if (rawBit) g_stableRaw |= (uint16_t)(1u << channel);
    else        g_stableRaw &= (uint16_t)~(1u << channel);
  }

  if (rawBit) g_rawPrev |= (uint16_t)(1u << channel);
  else        g_rawPrev &= (uint16_t)~(1u << channel);
}
} // namespace

void mux4067_init(uint8_t s0Pin, uint8_t s1Pin, uint8_t s2Pin, uint8_t s3Pin,
                   uint8_t sigPin, uint8_t channelCount) {
  g_s0 = s0Pin;
  g_s1 = s1Pin;
  g_s2 = s2Pin;
  g_s3 = s3Pin;
  g_sig = sigPin;

  if (channelCount < 1) channelCount = 1;
  if (channelCount > MUX4067_MAX_CHANNELS) channelCount = MUX4067_MAX_CHANNELS;
  g_channelCount = channelCount;

  pinMode(g_s0, OUTPUT);
  pinMode(g_s1, OUTPUT);
  pinMode(g_s2, OUTPUT);
  pinMode(g_s3, OUTPUT);
  // Pull-up interno da ESP32 na linha SIG comum: serve todos os canais
  // (o mux liga o selecionado ao SIG) e faz canais livres/nao fiados lerem
  // HIGH em vez de flutuar. Nao e' preciso resistor por entrada.
  pinMode(g_sig, INPUT_PULLUP);

  g_currentChannel = 0;
  applySelectPins(g_currentChannel);
  armSettleWindow();

  // Estado inicial: le uma vez cada canal em uso, sem esperar debounce,
  // pra nao reportar "tudo pressionado"/instavel nos primeiros ciclos.
  // (Bloqueio curto e deliberado, so no init — nunca em scan().)
  for (uint8_t ch = 0; ch < g_channelCount; ch++) {
    applySelectPins(ch);
    delayMicroseconds(MUX4067_SETTLE_US);
    const bool rawBit = digitalRead(g_sig) == HIGH;
    if (rawBit) {
      g_rawPrev   |= (uint16_t)(1u << ch);
      g_stableRaw |= (uint16_t)(1u << ch);
    } else {
      g_rawPrev   &= (uint16_t)~(1u << ch);
      g_stableRaw &= (uint16_t)~(1u << ch);
    }
    g_lastChangeMs[ch] = millis();
  }

  g_currentChannel = 0;
  applySelectPins(g_currentChannel);
  armSettleWindow();
}

void mux4067_select(uint8_t channel) {
  channel &= 0x0F; // mascara para 0-15 (74HC4067 tem 4 bits de endereco)
  applySelectPins(channel);
}

bool mux4067_read() {
  return digitalRead(g_sig) == HIGH;
}

void mux4067_scan() {
  if ((int32_t)(micros() - g_settleDeadlineUs) < 0) {
    return; // ainda assentando o canal atual — nao bloqueia, so volta
  }

  const bool rawBit = mux4067_read();
  updateDebounceForChannel(g_currentChannel, rawBit);

  g_currentChannel++;
  if (g_currentChannel >= g_channelCount) {
    g_currentChannel = 0;
  }
  applySelectPins(g_currentChannel);
  armSettleWindow();
}

uint16_t mux4067_get_state() {
  // Inverte: g_stableRaw usa convencao do pino (1=aberto), o contrato
  // desta funcao e "1=pressionado" — mesma convencao do input_expander.
  const uint16_t mask = (g_channelCount >= 16)
                             ? 0xFFFF
                             : (uint16_t)((1u << g_channelCount) - 1);
  return (uint16_t)(~g_stableRaw) & mask;
}

bool mux4067_get_channel_state(uint8_t channel) {
  if (channel >= g_channelCount || channel >= MUX4067_MAX_CHANNELS) {
    return false;
  }
  return (mux4067_get_state() >> channel) & 0x1;
}
