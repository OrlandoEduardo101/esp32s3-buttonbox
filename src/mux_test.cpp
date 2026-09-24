// Firmware de teste ISOLADO do driver do 74HC4067.
//
// Propositalmente não toca em HID, CDC, WiFi, OTA, MCP23017, encoders nem
// LEDs — só valida, canal por canal, que o mux está sendo lido
// corretamente antes de integrar com o resto do firmware.
//
// USB em modo Serial-JTAG (ARDUINO_USB_MODE=1, ver [env:mux-test] no
// platformio.ini), mesmo padrão de diag.cpp/mcp_test.cpp.
//
// Fiação esperada (docs/INPUTS_PINOUT.md secao 4):
//   S0=GPIO15 S1=GPIO16 S2=GPIO17 S3=GPIO18 SIG=GPIO21
//   C0-C8 em uso (mapa aprovado, secao 3); C9-C15 livres — por isso este
//   teste varre só 9 canais (channelCount=9), demonstrando que o driver
//   não exige os 16.
#include <Arduino.h>
#include "mux4067.h"
#include "board_config.h" // pinos e numero de canais — mapa unico do projeto

static const uint8_t CHANNEL_COUNT = BOARD_MUX_CHANNEL_COUNT; // C0-C14

// Rótulos na mesma ordem dos bits de mux4067_get_state() (docs/INPUTS_
// PINOUT.md secao 3): C0-C10 = push buttons, C11 = start engine, C12/C13 =
// ignição, C14 = microswitch do freio de estacionamento.
static const char *const CHANNEL_LABEL[CHANNEL_COUNT] = {
  "C0  Push button 1",
  "C1  Push button 2",
  "C2  Push button 3",
  "C3  Push button 4",
  "C4  Push button 5",
  "C5  Push button 6",
  "C6  Push button 7",
  "C7  Push button 8",
  "C8  Push button 9",
  "C9  Push button 10",
  "C10 Push button 11",
  "C11 Start Engine",
  "C12 Ignicao ON",
  "C13 Ignicao IGN",
  "C14 Microswitch freio de estacionamento",
};

static uint16_t lastPrinted = 0x0000;

static void printState(uint16_t state) {
  Serial.print("[74HC4067] estado = 0b");
  for (int8_t i = CHANNEL_COUNT - 1; i >= 0; i--) {
    Serial.print((state >> i) & 0x1);
  }
  Serial.printf(" (0x%04X)\n", state);
}

static void printChangedBits(uint16_t before, uint16_t after) {
  const uint16_t changed = before ^ after;
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    if ((changed >> i) & 0x1) {
      const bool pressed = (after >> i) & 0x1;
      Serial.printf("  %-40s -> %s\n", CHANNEL_LABEL[i],
                    pressed ? "PRESSIONADO/FECHADO" : "solto/aberto");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado 74HC4067 (mux4067) ===");

  // Pinos do board_config, NAO os defaults do driver: os defaults sao
  // 15/16/17/18/21, que nesta placa sao pads da face inferior — usa-los aqui
  // faria o teste varrer pinos que nem estao ligados no mux.
  mux4067_init(BOARD_MUX_S0_PIN, BOARD_MUX_S1_PIN, BOARD_MUX_S2_PIN,
               BOARD_MUX_S3_PIN, BOARD_MUX_SIG_PIN, CHANNEL_COUNT);

  Serial.println("Mapa de canais varridos (C9-C15 ficam de fora de "
                  "propósito, sao livres/nao fiados ainda):");
  for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
    Serial.printf("  canal %2u = %s\n", i, CHANNEL_LABEL[i]);
  }

  lastPrinted = mux4067_get_state();
  Serial.println("Estado inicial:");
  printState(lastPrinted);
  Serial.println("Aperte/mova cada entrada para ver mudancas abaixo.\n");
}

void loop() {
  // Nao-bloqueante: cada chamada avanca no maximo um passo da varredura
  // (espera de assentamento medida por micros(), nunca delay()).
  mux4067_scan();

  const uint16_t now = mux4067_get_state();
  if (now != lastPrinted) {
    printChangedBits(lastPrinted, now);
    printState(now);
    lastPrinted = now;
  }

  static uint32_t lastHeartbeat = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastHeartbeat >= 5000) {
    lastHeartbeat = nowMs;
    Serial.print("[status] ");
    printState(now);
  }
}
