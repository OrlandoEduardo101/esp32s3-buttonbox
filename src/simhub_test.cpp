// Firmware de teste ISOLADO do protocolo SimHub Standard Serial (CDC).
//
// Propositalmente não toca em HID, inputs, MCP23017, 74HC4067, encoders,
// WiFi nem OTA — só valida o parser do protocolo SimHub antes de integrar
// com o resto do firmware.
//
// USB em modo NATIVO (ARDUINO_USB_MODE=0, ver [env:simhub-test] no
// platformio.ini) — diferente dos outros testes isolados deste projeto
// (que usam USB-Serial-JTAG por simplicidade): aqui queremos exercitar
// exatamente a MESMA pilha de CDC (TinyUSB) que o firmware de produção
// usa de verdade, já que o objetivo é validar o protocolo sobre essa CDC
// específica, não só a lógica de parsing em abstrato.
//
// Como testar: ver scripts/simhub_test_send.py (fala o protocolo real do
// SimHub: 0x03 + comando), ou o proprio SimHub na aba Arduino.
//
// Também aceita "SETLEDS <n>\n" (texto, terminado em '\n') para testar a
// troca de contagem de LEDs em runtime + persistência em NVS, isolado do
// resto do firmware — mesma lógica de despacho cabeçalho-SimHub-vs-texto
// usada em src/main.cpp.
#include <Arduino.h>
#include "simhub.h"

static void printDumpleds() {
  const uint16_t strip = simhub_get_strip_count();
  Serial.printf("[dumpleds] conectado=%s matriz=%u fita=%u\n",
                simhub_is_connected() ? "sim" : "nao",
                (unsigned)SIMHUB_MATRIX_LED_COUNT, (unsigned)strip);
  Serial.print("[dumpleds] matriz ");
  for (uint16_t i = 0; i < 4; i++) {
    const SimhubColor c = simhub_get_matrix_led(i);
    Serial.printf("M%u=(%3u,%3u,%3u) ", i, c.r, c.g, c.b);
  }
  Serial.println();
  Serial.print("[dumpleds] fita ");
  const uint16_t toShow = (strip < 4) ? strip : 4;
  for (uint16_t i = 0; i < toShow; i++) {
    const SimhubColor c = simhub_get_strip_led(i);
    Serial.printf("LED%u=(%3u,%3u,%3u) ", i, c.r, c.g, c.b);
  }
  Serial.println();
}

static void handleTextLine(char *line) {
  for (char *p = line; *p; p++) *p = (char)toupper((int)*p);

  if (strcmp(line, "DUMPLEDS") == 0) {
    printDumpleds();
    return;
  }
  if (strncmp(line, "SETLEDS ", 8) != 0) return;

  const int n = atoi(line + 8);
  if (n > 0 && simhub_set_strip_count((uint16_t)n)) {
    Serial.printf("LEDS_SET %d\n", n);
  } else {
    Serial.printf("LEDS_INVALID (1-%u)\n", (unsigned)SIMHUB_STRIP_COUNT_MAX);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado do protocolo SimHub Arduino (CDC) ===");

  simhub_init();

  Serial.printf("matriz 8x8 (%u) + fita (%u)\n", (unsigned)SIMHUB_MATRIX_LED_COUNT, (unsigned)simhub_get_strip_count());
  Serial.println("Aguardando comandos do SimHub (0x03 + comando).\n");
}

void loop() {
  static char line[32];
  static uint8_t len = 0;

  while (Serial.available() > 0) {
    const uint8_t raw = (uint8_t)Serial.read();

    if (simhub_is_header_byte(raw)) {
      simhub_process_packet();
      len = 0;
      continue;
    }

    const char c = (char)raw;
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      handleTextLine(line);
      len = 0;
      continue;
    }
    if (len < sizeof(line) - 1) line[len++] = c;
    else                        len = 0;
  }

  simhub_update();

  static bool lastConnected = false;
  const bool connected = simhub_is_connected();
  if (connected != lastConnected) {
    lastConnected = connected;
    Serial.printf("[status] simhub %s\n", connected ? "CONECTADO"
                                                      : "desconectado (timeout sem comandos)");
  }

  static uint32_t lastDump = 0;
  const uint32_t now = millis();
  if (connected && now - lastDump >= 2000) {
    lastDump = now;
    printDumpleds();
  }
}
