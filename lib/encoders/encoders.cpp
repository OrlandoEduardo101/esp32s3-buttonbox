#include "encoders.h"
#include <Arduino.h>
#include "esp_attr.h" // IRAM_ATTR

namespace {

// Ring buffer por encoder: produtor único (a ISR, escreve 'head') /
// consumidor único (encoder_update(), escreve 'tail'). Esse padrão SPSC
// (single-producer/single-consumer) dispensa seção crítica — ISR e loop()
// rodam no mesmo core por padrão no Arduino-ESP32, e cada índice só é
// escrito por um dos dois lados.
constexpr uint8_t RING_SIZE = 32; // potencia de 2 -> mascara em vez de %
constexpr uint8_t RING_MASK = RING_SIZE - 1;

// Máquina de estados sobre o ciclo de Gray de 4 passos do KY-040 (ver
// encoders.h para a justificativa). Pins: bit1=CLK, bit0=DT.
enum class QuadState : uint8_t { Rest, Cw1, Cw2, Cw3, Ccw1, Ccw2, Ccw3 };

struct EncoderRuntime {
  uint8_t clkPin;
  uint8_t dtPin;

  // Escrito só pela ISR:
  volatile uint8_t ring[RING_SIZE];
  volatile uint8_t head;
  // Escrito só por encoder_update():
  uint8_t tail;

  // Tocado só pelo PRODUTOR (encoder_feed, no modo externo) — guarda a
  // última amostra já enfileirada, para descartar repetições. 0xFF = ainda
  // não há amostra anterior. Não usado no modo GPIO/ISR, onde cada borda
  // já é, por definição, uma mudança.
  uint8_t lastFed;

  // Tocados só por encoder_update() (nunca pela ISR) — sem risco de
  // corrida com a interrupção.
  QuadState state;
  int32_t position;
  uint16_t pendingCW;
  uint16_t pendingCCW;
};

EncoderRuntime g_enc[ENCODER_COUNT];

// ISR mínima de propósito: só lê os 2 pinos e empilha a amostra. Nenhuma
// lógica de decodificação aqui — isso fica em stepQuadrature(), chamada só
// a partir de encoder_update() (contexto de loop, não de interrupção).
void IRAM_ATTR encoderIsr(void *argIndex) {
  const uint8_t idx = (uint8_t)(uintptr_t)argIndex;
  EncoderRuntime &enc = g_enc[idx];

  const uint8_t pins = (uint8_t)((digitalRead(enc.clkPin) << 1) | digitalRead(enc.dtPin));

  const uint8_t head = enc.head;
  const uint8_t next = (uint8_t)((head + 1) & RING_MASK);
  if (next != enc.tail) {
    enc.ring[head] = pins;
    enc.head = next;
  }
  // Buffer cheio (next == tail): amostra descartada de propósito. Só
  // aconteceria se encoder_update() ficasse muito tempo sem ser chamada —
  // preferimos perder uma amostra intermediária a travar/bloquear a ISR.
}

// Avança um passo da maquina de estados. 'pins' e o par (CLK,DT) cru desta
// amostra. Devolve o novo estado e, por saida, o evento (se completou um
// detent). Ver a tabela completa (28 casos, 7 estados x 4 valores de pino)
// derivada no raciocinio de projeto — cada estado só avança nos dois
// vizinhos validos do ciclo de Gray (progride ou faz bounce de volta); tudo
// mais (salto de 2 bits simultaneo) reseta pro repouso sem emitir evento.
QuadState stepQuadrature(QuadState state, uint8_t pins, EncoderEvent *event) {
  *event = ENCODER_NONE;

  switch (state) {
    case QuadState::Rest:
      if (pins == 0b01) return QuadState::Cw1;
      if (pins == 0b10) return QuadState::Ccw1;
      return QuadState::Rest; // 0b11 (sem mudanca) ou 0b00 (ruido) -> fica

    case QuadState::Cw1:
      if (pins == 0b01) return QuadState::Cw1;  // sem mudanca
      if (pins == 0b00) return QuadState::Cw2;  // progride
      return QuadState::Rest;                   // 0b11 bounce ou 0b10 ruido

    case QuadState::Cw2:
      if (pins == 0b00) return QuadState::Cw2;  // sem mudanca
      if (pins == 0b10) return QuadState::Cw3;  // progride
      if (pins == 0b01) return QuadState::Cw1;  // bounce de volta
      return QuadState::Rest;                   // 0b11 ruido (salto duplo)

    case QuadState::Cw3:
      if (pins == 0b10) return QuadState::Cw3;  // sem mudanca
      if (pins == 0b11) { *event = ENCODER_CW; return QuadState::Rest; } // fechou o ciclo
      if (pins == 0b00) return QuadState::Cw2;  // bounce de volta
      return QuadState::Rest;                   // 0b01 ruido

    case QuadState::Ccw1:
      if (pins == 0b10) return QuadState::Ccw1; // sem mudanca
      if (pins == 0b00) return QuadState::Ccw2; // progride
      return QuadState::Rest;                   // 0b11 bounce ou 0b01 ruido

    case QuadState::Ccw2:
      if (pins == 0b00) return QuadState::Ccw2; // sem mudanca
      if (pins == 0b01) return QuadState::Ccw3; // progride
      if (pins == 0b10) return QuadState::Ccw1; // bounce de volta
      return QuadState::Rest;                   // 0b11 ruido (salto duplo)

    case QuadState::Ccw3:
      if (pins == 0b01) return QuadState::Ccw3; // sem mudanca
      if (pins == 0b11) { *event = ENCODER_CCW; return QuadState::Rest; } // fechou o ciclo
      if (pins == 0b00) return QuadState::Ccw2; // bounce de volta
      return QuadState::Rest;                   // 0b10 ruido
  }
  return QuadState::Rest;
}

// Zera todo o estado de um encoder. Comum aos dois modos de init.
void resetRuntime(EncoderRuntime &enc) {
  enc.head = 0;
  enc.tail = 0;
  enc.lastFed = 0xFF;
  enc.state = QuadState::Rest;
  enc.position = 0;
  enc.pendingCW = 0;
  enc.pendingCCW = 0;
}

} // namespace

void encoder_init(const uint8_t clkPins[ENCODER_COUNT], const uint8_t dtPins[ENCODER_COUNT]) {
  for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
    EncoderRuntime &enc = g_enc[i];
    enc.clkPin = clkPins[i];
    enc.dtPin  = dtPins[i];
    resetRuntime(enc);

    // Pull-up interno como rede de seguranca; o modulo KY-040 ja traz o
    // seu proprio pull-up de fabrica.
    pinMode(enc.clkPin, INPUT_PULLUP);
    pinMode(enc.dtPin, INPUT_PULLUP);

    attachInterruptArg(enc.clkPin, encoderIsr, (void *)(uintptr_t)i, CHANGE);
    attachInterruptArg(enc.dtPin,  encoderIsr, (void *)(uintptr_t)i, CHANGE);
  }
}

void encoder_init_external() {
  for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
    EncoderRuntime &enc = g_enc[i];
    // 0xFF = "sem pino" — deixa explícito que neste modo o driver nunca
    // faz digitalRead(); quem lê o hardware é quem chama encoder_feed().
    enc.clkPin = 0xFF;
    enc.dtPin  = 0xFF;
    resetRuntime(enc);
  }
}

void encoder_feed(uint8_t index, uint8_t clkLevel, uint8_t dtLevel) {
  if (index >= ENCODER_COUNT) return;
  EncoderRuntime &enc = g_enc[index];

  const uint8_t pins = (uint8_t)(((clkLevel ? 1 : 0) << 1) | (dtLevel ? 1 : 0));

  // Filtro de repetição: sem isso, uma varredura de 1 kHz encheria o ring
  // de 32 posições com amostras idênticas em 32 ms de placa parada, e a
  // primeira transição de verdade seria descartada por buffer cheio.
  if (pins == enc.lastFed) return;
  enc.lastFed = pins;

  const uint8_t head = enc.head;
  const uint8_t next = (uint8_t)((head + 1) & RING_MASK);
  if (next != enc.tail) {
    enc.ring[head] = pins;
    enc.head = next;
  }
  // Buffer cheio: mesma decisão do modo ISR — descarta a amostra em vez de
  // bloquear o produtor. Só aconteceria com encoder_update() sem ser
  // chamada por muito tempo (>32 transições de atraso).
}

void encoder_update() {
  for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
    EncoderRuntime &enc = g_enc[i];

    // Consumidor unico do ring buffer. 'head' pode mudar a qualquer
    // momento (escrito pela ISR); lemos uma vez por iteracao do while,
    // o que basta para o padrao SPSC.
    while (enc.tail != enc.head) {
      const uint8_t pins = enc.ring[enc.tail];
      enc.tail = (uint8_t)((enc.tail + 1) & RING_MASK);

      EncoderEvent ev;
      enc.state = stepQuadrature(enc.state, pins, &ev);

      if (ev == ENCODER_CW) {
        enc.position++;
        enc.pendingCW++;
      } else if (ev == ENCODER_CCW) {
        enc.position--;
        enc.pendingCCW++;
      }
    }
  }
}

EncoderEvent encoder_get_event(uint8_t index) {
  if (index >= ENCODER_COUNT) return ENCODER_NONE;
  EncoderRuntime &enc = g_enc[index];

  // pendingCW/pendingCCW só são tocados por encoder_update() e por esta
  // função — ambas rodam em contexto de loop, nunca em ISR. Sem corrida.
  if (enc.pendingCW > 0) {
    enc.pendingCW--;
    return ENCODER_CW;
  }
  if (enc.pendingCCW > 0) {
    enc.pendingCCW--;
    return ENCODER_CCW;
  }
  return ENCODER_NONE;
}

int32_t encoder_get_position(uint8_t index) {
  if (index >= ENCODER_COUNT) return 0;
  return g_enc[index].position;
}
