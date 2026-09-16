// Firmware de teste ISOLADO da camada unificada de INPUTS (lib/inputs).
//
// Propositalmente usa SÓ a API pública de inputs.h (inputs_init/update/
// get_state/get_event) — nunca chama input_expander_*, mcp23017_*,
// mux4067_* nem encoder_* diretamente. O objetivo deste teste é provar
// exatamente o critério de sucesso pedido: quem consome esta camada não
// precisa saber (e aqui, propositalmente, NÃO sabe) se cada entrada vem do
// MCP23017, do 74HC4067 ou de um encoder.
//
// Não implementa HID, CDC especial, WiFi, OTA, SimHub nem LEDs — só
// imprime pela serial (USB-Serial-JTAG, ARDUINO_USB_MODE=1, ver
// [env:inputs-test] no platformio.ini).
#include <Arduino.h>
#include "inputs.h"

static const char *eventName(InputEventType ev) {
  switch (ev) {
    case INPUT_EVENT_PRESSED:  return "PRESSED";
    case INPUT_EVENT_RELEASED: return "RELEASED";
    case INPUT_EVENT_CW:       return "CW";
    case INPUT_EVENT_CCW:      return "CCW";
    default:                   return "NONE";
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Teste da camada unificada de INPUTS ===");
  Serial.println("(usa só INPUT_*, nunca MCP23017/74HC4067/encoder direto)");

  inputs_init();

  Serial.printf("Total de IDs logicos: %u (%u com STATE+EVENT, %u so com EVENT)\n",
                (unsigned)INPUT_ID_COUNT, (unsigned)INPUT_LEVEL_ID_COUNT,
                (unsigned)(INPUT_ID_COUNT - INPUT_LEVEL_ID_COUNT));
  Serial.println("Acione cada entrada e confira o ID logico + evento abaixo.\n");
}

void loop() {
  inputs_update();

  for (uint8_t id = 0; id < INPUT_ID_COUNT; id++) {
    InputEventType ev;
    while ((ev = inputs_get_event((InputId)id)) != INPUT_EVENT_NONE) {
      Serial.printf("%-28s -> %s\n", inputs_get_name((InputId)id), eventName(ev));
    }
  }

  static uint32_t lastHeartbeat = 0;
  const uint32_t now = millis();
  if (now - lastHeartbeat >= 5000) {
    lastHeartbeat = now;
    Serial.print("[status] pressionados agora:");
    bool any = false;
    for (uint8_t id = 0; id < INPUT_LEVEL_ID_COUNT; id++) {
      if (inputs_get_state((InputId)id)) {
        Serial.print(" ");
        Serial.print(inputs_get_name((InputId)id));
        any = true;
      }
    }
    if (!any) Serial.print(" (nenhum)");
    Serial.println();
  }
}
