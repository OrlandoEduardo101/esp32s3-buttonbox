// Firmware de teste ISOLADO do driver do MCP23017 + camada input_expander.
//
// Propositalmente não inclui HID, encoders, 74HC4067, SimHub, LEDs, WiFi
// nem OTA — só existe pra validar, entrada por entrada, que o expansor
// está lido corretamente antes de integrar com o resto do firmware.
//
// USB em modo Serial-JTAG (ARDUINO_USB_MODE=1, ver [env:mcp-test] no
// platformio.ini) — mesmo padrão de diag.cpp: sem HID, só serial, o mais
// simples possível pra isolar o hardware sob teste.
//
// Fiação esperada (docs/INPUTS_PINOUT.md):
//   SDA -> GPIO8, SCL -> GPIO9, endereço I2C 0x20 (A0/A1/A2 no GND)
//   GPA0-7 / GPB0-7 -> switches para GND, pull-up interno do MCP23017
#include <Arduino.h>
#include "input_expander.h"

// Rótulos na mesma ordem dos bits de input_expander_get_state():
// bit 0-7 = GPA0-GPA7, bit 8-15 = GPB0-GPB7 (docs/INPUTS_PINOUT.md secao 2).
static const char *const PIN_LABEL[16] = {
  "GPA0 Push button 1",
  "GPA1 Push button 2",
  "GPA2 Push button 3",
  "GPA3 Push button 4",
  "GPA4 Push button 5",
  "GPA5 Push button 6",
  "GPA6 Push button 7",
  "GPA7 Push button 8",
  "GPB0 Push button 9",
  "GPB1 Push button 10",
  "GPB2 Push button 11",
  "GPB3 Start Engine",
  "GPB4 Ignicao ON",
  "GPB5 Ignicao IGN",
  "GPB6 livre",
  "GPB7 livre",
};

static uint16_t lastPrinted = 0x0000;

static void printState(uint16_t state) {
  Serial.print("[MCP23017] estado = 0b");
  for (int8_t i = 15; i >= 0; i--) {
    Serial.print((state >> i) & 0x1);
  }
  Serial.printf(" (0x%04X)\n", state);
}

static void printChangedBits(uint16_t before, uint16_t after) {
  const uint16_t changed = before ^ after;
  for (uint8_t i = 0; i < 16; i++) {
    if ((changed >> i) & 0x1) {
      const bool pressed = (after >> i) & 0x1;
      Serial.printf("  %-22s -> %s\n", PIN_LABEL[i],
                    pressed ? "PRESSIONADO" : "solto");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado MCP23017 / input_expander ===");

  if (!input_expander_init()) {
    Serial.println("[MCP23017] AVISO: chip nao respondeu no I2C "
                    "(endereco 0x20, SDA=8, SCL=9). Verifique a fiacao.");
  } else {
    Serial.println("[MCP23017] chip respondeu OK no I2C.");
  }

  Serial.println("Mapa de bits:");
  for (uint8_t i = 0; i < 16; i++) {
    Serial.printf("  bit %2u = %s\n", i, PIN_LABEL[i]);
  }

  lastPrinted = input_expander_get_state();
  Serial.println("Estado inicial:");
  printState(lastPrinted);
  Serial.println("Aperte/solte cada entrada para ver mudancas abaixo.\n");
}

void loop() {
  // Chamada frequente e não-bloqueante: só faz uma leitura I2C (dezenas de
  // us a ~1 ms) e avança o debounce por tempo. Sem delay() bloqueante.
  input_expander_update();

  const uint16_t now = input_expander_get_state();
  if (now != lastPrinted) {
    printChangedBits(lastPrinted, now);
    printState(now);
    lastPrinted = now;
  }

  // Status periódico mesmo sem mudança, para confirmar que o loop e a
  // leitura I2C continuam vivos (e não travados).
  static uint32_t lastHeartbeat = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastHeartbeat >= 5000) {
    lastHeartbeat = nowMs;
    Serial.printf("[status] conectado=%s | ",
                  input_expander_is_connected() ? "sim" : "NAO");
    printState(now);
  }
}
