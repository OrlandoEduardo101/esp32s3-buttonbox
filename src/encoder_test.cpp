// Firmware de teste ISOLADO dos 4 encoders KY-040 (quadratura).
//
// Propositalmente não toca em botões, MCP23017, 74HC4067, SimHub, LEDs,
// WiFi nem OTA — só valida a decodificação de quadratura antes de integrar
// com o resto do firmware.
//
// USB em modo Serial-JTAG (ARDUINO_USB_MODE=1, ver [env:encoder-test] no
// platformio.ini), mesmo padrão dos outros testes isolados deste projeto.
//
// Fiação esperada (docs/INPUTS_PINOUT.md secao 2 — revisão 2 do pinout):
//   Encoder 1: CLK=GPA0 DT=GPA1   (do MCP23017, I2C em SDA=GPIO8/SCL=GPIO9)
//   Encoder 2: CLK=GPA2 DT=GPA3
//   Encoder 3: CLK=GPA4 DT=GPA5
//   Encoder 4: CLK=GPA6 DT=GPA7
// (SW de cada encoder vai em GPB0-GPB3 — nao faz parte deste teste.)
//
// Este teste amostra o MCP23017 no proprio loop(), sem a task dedicada do
// firmware principal: aqui o loop nao tem delay(5) nem render de LED, entao
// ele ja gira bem mais rapido que 1 ms. E' de proposito — mantem o teste
// isolado, sem depender de lib/inputs.
//
// Critério de sucesso a validar manualmente: girar cada encoder 1 clique
// físico de cada vez deve imprimir EXATAMENTE 1 linha CW ou CCW — nunca
// zero, nunca duas.
#include <Arduino.h>
#include "encoders.h"
#include "input_expander.h"
#include "board_config.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado dos 4 encoders KY-040 (quadratura) ===");

  if (!input_expander_init(BOARD_MCP23017_ADDR, BOARD_I2C_SDA_PIN,
                            BOARD_I2C_SCL_PIN)) {
    Serial.println("[ERRO] MCP23017 nao respondeu no I2C — confira "
                    "alimentacao, RESET em 3V3 e A0/A1/A2 no GND.");
  }
  encoder_init_external();

  Serial.println("Gire cada encoder 1 detent de cada vez e confira: deve "
                  "aparecer exatamente 1 linha por clique, no sentido "
                  "certo.\n");
}

void loop() {
  // Amostra o MCP23017 e entrega CLK/DT crus (sem debounce) para a maquina
  // de quadratura. Amostra repetida e' descartada dentro de encoder_feed().
  input_expander_update();
  const uint16_t raw = input_expander_get_raw();
  for (uint8_t i = 0; i < BOARD_ENCODER_COUNT; i++) {
    encoder_feed(i, (uint8_t)((raw >> BOARD_ENCODER_MCP_CLK_BIT[i]) & 0x1),
                     (uint8_t)((raw >> BOARD_ENCODER_MCP_DT_BIT[i]) & 0x1));
  }

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
