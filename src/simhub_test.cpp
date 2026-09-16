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
// Como testar: ver scripts/simhub_test_send.py (envia proto/ledsc/sleds e
// confere as respostas), ou mandar os bytes manualmente por qualquer
// terminal serial binário.
//
// Também aceita "SETLEDS <n>\n" (texto, terminado em '\n') para testar a
// troca de contagem de LEDs em runtime + persistência em NVS, isolado do
// resto do firmware — mesma lógica de despacho cabeçalho-SimHub-vs-texto
// usada em src/main.cpp.
#include <Arduino.h>
#include "simhub.h"

static void handleSetledsLine(char *line) {
  for (char *p = line; *p; p++) *p = (char)toupper((int)*p);
  if (strncmp(line, "SETLEDS ", 8) != 0) return;

  const int n = atoi(line + 8);
  if (n > 0 && simhub_set_led_count((uint16_t)n)) {
    Serial.printf("LEDS_SET %d\n", n);
  } else {
    Serial.printf("LEDS_INVALID (1-%u)\n", (unsigned)SIMHUB_LED_COUNT_MAX);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado do protocolo SimHub Standard Serial (CDC) ===");

  simhub_init();

  Serial.printf("LED count anunciado via 'ledsc': %u\n", simhub_get_led_count());
  Serial.println("Envie proto/ledsc/sleds com o cabecalho de 6x 0xFF para testar.\n");
}

void loop() {
  static char line[32];
  static uint8_t len = 0;

  while (Serial.available() > 0) {
    const uint8_t raw = (uint8_t)Serial.read();

    if (simhub_feed_header_byte(raw)) {
      simhub_process_packet();
      len = 0;
      continue;
    }
    if (raw == 0xFF) continue; // prefixo parcial de cabecalho SimHub

    const char c = (char)raw;
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      handleSetledsLine(line);
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
    Serial.printf("[status] simhub %s\n", connected ? "CONECTADO (sleds valido recebido)"
                                                      : "desconectado (timeout sem sleds novo)");
  }

  static uint32_t lastDump = 0;
  const uint32_t now = millis();
  if (connected && now - lastDump >= 2000) {
    lastDump = now;
    const uint16_t n = simhub_get_led_count();
    const uint16_t toShow = (n < 8) ? n : 8; // só os primeiros 8, pra não floodar
    Serial.print("[framebuffer] ");
    for (uint16_t i = 0; i < toShow; i++) {
      const SimhubColor c = simhub_get_led(i);
      Serial.printf("LED%u=(%3u,%3u,%3u) ", i, c.r, c.g, c.b);
    }
    if (n > toShow) Serial.printf("... (+%u LEDs)", (unsigned)(n - toShow));
    Serial.println();
  }
}
