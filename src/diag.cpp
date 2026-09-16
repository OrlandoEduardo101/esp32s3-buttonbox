// Diagnóstico: roda em ARDUINO_USB_MODE=1 (USB-Serial-JTAG de hardware).
// Só serve para responder "o app está rodando nesta placa?" — se estas linhas
// aparecerem na serial, a placa, o toolchain e a gravação estão OK, e o
// problema do HID fica isolado na troca para USB-OTG/TinyUSB.
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== APP RODANDO (diag, USB_MODE=1) ===");
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("alive %lu\n", (unsigned long)n++);
  delay(500);
}
