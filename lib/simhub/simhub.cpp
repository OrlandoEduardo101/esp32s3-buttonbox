#include "simhub.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {

constexpr uint8_t MESSAGE_HEADER = 0x03;
constexpr uint8_t ACK_BYTE       = 0x15;

// Assinatura de MCU devolvida em "X mcutype" — as implementações de
// referência reportam a assinatura de um ATmega32U4 (Leonardo/Pro Micro),
// que é o que o SimHub espera de um dispositivo "Arduino padrão".
constexpr uint8_t SIGNATURE_0 = 0x1E;
constexpr uint8_t SIGNATURE_1 = 0x98;
constexpr uint8_t SIGNATURE_2 = 0x01;

constexpr const char *NVS_NAMESPACE  = "simhub";
constexpr const char *NVS_KEY_STRIP  = "stripcount";

uint8_t g_strip[SIMHUB_STRIP_COUNT_MAX * 3];
uint8_t g_matrix[SIMHUB_MATRIX_LED_COUNT * 3];

uint16_t g_stripCount = SIMHUB_STRIP_COUNT_DEFAULT;
bool     g_connected  = false;
uint32_t g_lastActivityMs = 0;

// Lê um byte da serial esperando até 'deadlineMs'. -1 se estourar o prazo.
// Comparação segura contra wraparound de millis().
int readByteUntil(uint32_t deadlineMs) {
  while ((int32_t)(millis() - deadlineMs) < 0) {
    if (Serial.available() > 0) {
      return Serial.read();
    }
  }
  return -1;
}

// Lê uma string até encontrar um dos dois terminadores (usado só no
// comando 'X', cujo subcomando vem em ASCII).
String readStringUntil(char t1, char t2, uint32_t deadlineMs) {
  String out;
  while (out.length() < 32) {
    const int b = readByteUntil(deadlineMs);
    if (b < 0 || (char)b == t1 || (char)b == t2) break;
    out += (char)b;
  }
  return out;
}

// Stream RGB do SimHub — formato idêntico ao SHRGBLedsBase::read() das
// implementações de referência:
//   mode = byte
//   enquanto mode > 0:
//     mode 1 -> todos os LEDs em sequência (r,g,b cada)
//     mode 2 -> startLed, numLeds, depois numLeds x (r,g,b)
//     mode 3 -> startLed, numLeds, um (r,g,b) repetido no intervalo
//     mode = byte
// Escreve direto no framebuffer destino; índices fora do intervalo são
// descartados (mas os bytes continuam sendo consumidos, pra não
// dessincronizar o stream).
void readRgbStream(uint8_t *fb, uint16_t ledCount, uint32_t deadlineMs) {
  int mode = readByteUntil(deadlineMs);

  while (mode > 0) {
    if (mode == 1) {
      for (uint16_t j = 0; j < ledCount; j++) {
        const int r = readByteUntil(deadlineMs);
        const int g = readByteUntil(deadlineMs);
        const int b = readByteUntil(deadlineMs);
        if (r < 0 || g < 0 || b < 0) return;
        fb[j * 3 + 0] = (uint8_t)r;
        fb[j * 3 + 1] = (uint8_t)g;
        fb[j * 3 + 2] = (uint8_t)b;
      }
    } else if (mode == 2) {
      const int start = readByteUntil(deadlineMs);
      const int num   = readByteUntil(deadlineMs);
      if (start < 0 || num < 0) return;
      for (int j = start; j < start + num; j++) {
        const int r = readByteUntil(deadlineMs);
        const int g = readByteUntil(deadlineMs);
        const int b = readByteUntil(deadlineMs);
        if (r < 0 || g < 0 || b < 0) return;
        if (j >= 0 && j < (int)ledCount) {
          fb[j * 3 + 0] = (uint8_t)r;
          fb[j * 3 + 1] = (uint8_t)g;
          fb[j * 3 + 2] = (uint8_t)b;
        }
      }
    } else if (mode == 3) {
      const int start = readByteUntil(deadlineMs);
      const int num   = readByteUntil(deadlineMs);
      const int r     = readByteUntil(deadlineMs);
      const int g     = readByteUntil(deadlineMs);
      const int b     = readByteUntil(deadlineMs);
      if (start < 0 || num < 0 || r < 0 || g < 0 || b < 0) return;
      for (int j = start; j < start + num; j++) {
        if (j >= 0 && j < (int)ledCount) {
          fb[j * 3 + 0] = (uint8_t)r;
          fb[j * 3 + 1] = (uint8_t)g;
          fb[j * 3 + 2] = (uint8_t)b;
        }
      }
    } else {
      return; // modo desconhecido: aborta em vez de consumir lixo
    }

    mode = readByteUntil(deadlineMs);
  }
}

void markActivity() {
  g_lastActivityMs = millis();
  g_connected = true;
}

} // namespace

void simhub_init() {
  memset(g_strip, 0, sizeof(g_strip));
  memset(g_matrix, 0, sizeof(g_matrix));
  g_connected = false;
  g_lastActivityMs = 0;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  g_stripCount = prefs.getUShort(NVS_KEY_STRIP, SIMHUB_STRIP_COUNT_DEFAULT);
  prefs.end();

  if (g_stripCount < 1 || g_stripCount > SIMHUB_STRIP_COUNT_MAX) {
    g_stripCount = SIMHUB_STRIP_COUNT_DEFAULT;
  }
}

bool simhub_is_header_byte(uint8_t b) {
  return b == MESSAGE_HEADER;
}

void simhub_process_packet() {
  const uint32_t deadline = millis() + SIMHUB_FRAME_TIMEOUT_MS;

  const int cmd = readByteUntil(deadline);
  if (cmd < 0) return;

  markActivity();

  switch ((char)cmd) {
    case '1': { // Hello
      readByteUntil(deadline); // byte de trailer, descartado
      delay(10);               // mesma pausa das implementacoes de referencia
      Serial.write((uint8_t)SIMHUB_VERSION_CHAR);
      Serial.flush();
      break;
    }

    case '0': { // Features
      delay(10);
      // N = nome, I = unique id, X = comandos expandidos, R = matriz RGB.
      // Sem "P" (SHCustomProtocol, excluido de proposito), sem "J"/"G"
      // (botoes e marcha vao pelo HID nativo, nao por este protocolo).
      Serial.print("NIXR\n");
      Serial.flush();
      break;
    }

    case '4': { // Quantidade de LEDs da fita
      Serial.write((uint8_t)g_stripCount);
      Serial.flush();
      break;
    }

    case '6': { // Dados RGB da fita
      readRgbStream(g_strip, g_stripCount, deadline);
      Serial.write(ACK_BYTE);
      Serial.flush();
      break;
    }

    case 'R': { // Dados RGB da matriz 8x8
      readRgbStream(g_matrix, SIMHUB_MATRIX_LED_COUNT, deadline);
      Serial.write(ACK_BYTE);
      Serial.flush();
      break;
    }

    case 'N': { // Nome do dispositivo
      Serial.print(SIMHUB_DEVICE_NAME);
      Serial.print("\n");
      Serial.flush();
      break;
    }

    case 'I': { // Unique ID — MAC da placa, mesmo valor do serial USB
      uint8_t mac[6] = {0};
      esp_efuse_mac_get_default(mac);
      char id[13];
      snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      Serial.print(id);
      Serial.print("\n");
      Serial.flush();
      break;
    }

    case 'A': { // Acq
      Serial.write((uint8_t)0x03);
      Serial.flush();
      break;
    }

    case 'X': { // Comandos expandidos
      const String action = readStringUntil(' ', '\n', deadline);
      if (action == "list") {
        Serial.print("mcutype\n");
        Serial.print("keepalive\n");
        Serial.print("\n");
      } else if (action == "mcutype") {
        Serial.write(SIGNATURE_0);
        Serial.write(SIGNATURE_1);
        Serial.write(SIGNATURE_2);
      } else {
        // keepalive e qualquer outro subcomando: só confirma.
        Serial.write(ACK_BYTE);
      }
      Serial.flush();
      break;
    }

    case 'J':   // Botoes lidos por serial: nenhum (vao por HID nativo)
    case '2':   // Modulos TM1638: nenhum
    case 'B': { // Modulos simples: nenhum
      Serial.write((uint8_t)0);
      Serial.flush();
      break;
    }

    case 'G': { // Marcha — nao anunciamos 'G', mas respondemos se vier
      readByteUntil(deadline);
      Serial.write(ACK_BYTE);
      Serial.flush();
      break;
    }

    case '8': { // Baudrate: irrelevante em USB CDC, so consome o codigo
      readByteUntil(deadline);
      break;
    }

    default:
      // Comando desconhecido: ignora. O proximo 0x03 ressincroniza.
      break;
  }
}

void simhub_update() {
  if (g_connected && (millis() - g_lastActivityMs) > SIMHUB_CONNECTION_TIMEOUT_MS) {
    g_connected = false;
    memset(g_strip, 0, sizeof(g_strip));
    memset(g_matrix, 0, sizeof(g_matrix));
  }
}

bool simhub_is_connected() {
  return g_connected;
}

uint16_t simhub_get_strip_count() {
  return g_stripCount;
}

bool simhub_set_strip_count(uint16_t n) {
  if (n < 1 || n > SIMHUB_STRIP_COUNT_MAX) {
    return false;
  }

  g_stripCount = n;
  memset(g_strip, 0, sizeof(g_strip));
  g_connected = false;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putUShort(NVS_KEY_STRIP, n);
  prefs.end();

  return true;
}

SimhubColor simhub_get_strip_led(uint16_t index) {
  if (index >= g_stripCount) return {0, 0, 0};
  const uint16_t base = index * 3;
  return {g_strip[base], g_strip[base + 1], g_strip[base + 2]};
}

SimhubColor simhub_get_matrix_led(uint16_t index) {
  if (index >= SIMHUB_MATRIX_LED_COUNT) return {0, 0, 0};
  const uint16_t base = index * 3;
  return {g_matrix[base], g_matrix[base + 1], g_matrix[base + 2]};
}
