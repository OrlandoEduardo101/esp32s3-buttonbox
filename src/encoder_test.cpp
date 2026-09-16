// Firmware de teste ISOLADO dos 4 encoders KY-040 (quadratura).
//
// Propositalmente não toca em botões, MCP23017, 74HC4067, SimHub, LEDs,
// WiFi nem OTA — só valida a decodificação de quadratura antes de integrar
// com o resto do firmware.
//
// USB em modo Serial-JTAG (ARDUINO_USB_MODE=1, ver [env:encoder-test] no
// platformio.ini), mesmo padrão dos outros testes isolados deste projeto.
//
// Fiação esperada (docs/INPUTS_PINOUT.md secao 4):
//   Encoder 1: CLK=GPIO4  DT=GPIO5
//   Encoder 2: CLK=GPIO6  DT=GPIO7
//   Encoder 3: CLK=GPIO10 DT=GPIO11
//   Encoder 4: CLK=GPIO12 DT=GPIO13
// (SW de cada encoder vai pelo 74HC4067 — nao faz parte deste teste.)
//
// Critério de sucesso a validar manualmente: girar cada encoder 1 clique
// físico de cada vez deve imprimir EXATAMENTE 1 linha CW ou CCW — nunca
// zero, nunca duas.
#include <Arduino.h>
#include "encoders.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado dos 4 encoders KY-040 (quadratura) ===");

  encoder_init();

  Serial.println("Gire cada encoder 1 detent de cada vez e confira: deve "
                  "aparecer exatamente 1 linha por clique, no sentido "
                  "certo.\n");
}

void loop() {
  // Nao-bloqueante: drena o ring buffer de amostras cruas de cada encoder
  // e roda a maquina de estados. Custo por chamada proporcional so ao
  // numero de transicoes acumuladas desde a ultima chamada.
  encoder_update();

  for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
    EncoderEvent ev;
    while ((ev = encoder_get_event(i)) != ENCODER_NONE) {
      Serial.printf("Encoder %u: %-3s  (posicao acumulada = %ld)\n", i + 1,
                    ev == ENCODER_CW ? "CW" : "CCW",
                    (long)encoder_get_position(i));
    }
  }

  static uint32_t lastHeartbeat = 0;
  const uint32_t now = millis();
  if (now - lastHeartbeat >= 5000) {
    lastHeartbeat = now;
    Serial.print("[status] posicoes:");
    for (uint8_t i = 0; i < ENCODER_COUNT; i++) {
      Serial.printf(" enc%u=%ld", i + 1, (long)encoder_get_position(i));
    }
    Serial.println();
  }
}
