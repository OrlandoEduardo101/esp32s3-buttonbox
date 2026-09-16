// Firmware de teste ISOLADO do driver WS2812 (lib/ws2812).
//
// Propositalmente não toca em HID, inputs, MCP23017, 74HC4067, encoders,
// SimHub, WiFi nem OTA — só varre cores na fita/matriz pra validar o
// canal RMT e o timing de bit, sem depender de nenhum dado do SimHub.
//
// USB em modo USB-Serial-JTAG (ARDUINO_USB_MODE=1) — este teste não usa
// HID nem precisa da CDC nativa, só serial de debug simples.
//
// NÃO TESTADO em hardware real por este agente (fita/matriz física ainda
// não montada no momento desta integração) — o timing de bit reaproveita
// exatamente os valores já usados por neopixelWrite() no core Arduino-
// ESP32 (validados pela Espressif para 1 pixel), generalizados para N
// pixels; ainda assim, confirme visualmente antes de considerar
// definitivo.
//
// Fiação esperada: dado da fita/matriz em GPIO1 (WS2812_DEFAULT_PIN),
// alimentação e nível lógico conforme o datasheet do WS2812B usado
// (normalmente 5V de alimentação; muitos aceitam ~3,3V de dado dentro da
// tolerância, mas um level-shifter é mais seguro em fitas longas).
#include <Arduino.h>
#include "ws2812.h"

static const uint16_t LED_COUNT = 74; // matriz 8x8 (64) + fita ~10 — ajuste conforme o SETLEDS real

static Ws2812Color buf[LED_COUNT];

static void fillAll(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < LED_COUNT; i++) buf[i] = {r, g, b};
  ws2812_show(buf, LED_COUNT);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste isolado WS2812 (lib/ws2812) ===");

  if (!ws2812_init()) {
    Serial.println("[ws2812] ERRO: falha ao inicializar canal RMT");
  } else {
    Serial.printf("[ws2812] RMT ok, GPIO%u, %u LEDs\n", WS2812_DEFAULT_PIN, LED_COUNT);
  }
  Serial.println("Varredura de cores a cada 1s: vermelho, verde, azul, "
                  "branco, apagado.\n");
}

void loop() {
  static uint8_t phase = 0;
  static uint32_t lastChange = 0;
  const uint32_t now = millis();

  if (now - lastChange >= 1000) {
    lastChange = now;
    switch (phase) {
      case 0: Serial.println("-> VERMELHO"); fillAll(255, 0, 0); break;
      case 1: Serial.println("-> VERDE");    fillAll(0, 255, 0); break;
      case 2: Serial.println("-> AZUL");     fillAll(0, 0, 255); break;
      case 3: Serial.println("-> BRANCO");   fillAll(255, 255, 255); break;
      case 4: Serial.println("-> APAGADO");  fillAll(0, 0, 0); break;
    }
    phase = (phase + 1) % 5;
  }
}
