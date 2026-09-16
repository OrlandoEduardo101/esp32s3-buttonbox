#include "simhub.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {

constexpr uint8_t SIMHUB_HEADER_LEN = 6; // 6x 0xFF

constexpr const char *NVS_NAMESPACE  = "simhub";
constexpr const char *NVS_KEY_LEDCNT = "ledcount";

// Buffers de tamanho FIXO (SIMHUB_LED_COUNT_MAX) — sem alocação dinâmica.
// g_ledCount (runtime, persistido em NVS) diz quantos desses bytes estão
// realmente em uso; o resto fica sempre zerado e nunca é exposto pelos
// getters nem enviado em "ledsc"/aceito em "sleds".
uint8_t g_framebuffer[SIMHUB_LED_COUNT_MAX * 3]; // publicado — o que os getters devolvem
uint8_t g_staging[SIMHUB_LED_COUNT_MAX * 3];     // rascunho de recepção — só vira "publicado" se o terminador bater

uint16_t g_ledCount   = SIMHUB_LED_COUNT_DEFAULT;
bool     g_connected  = false;
uint32_t g_lastSledsMs = 0;

// Lê um byte do Serial esperando até 'deadlineMs' (millis()). -1 se o
// prazo estourar sem nenhum byte chegar. Comparação segura contra
// wraparound de millis(), mesmo padrão já usado em mux4067/encoders.
int readByteUntil(uint32_t deadlineMs) {
  while ((int32_t)(millis() - deadlineMs) < 0) {
    if (Serial.available() > 0) {
      return Serial.read();
    }
  }
  return -1;
}

void publishFramebuffer(uint16_t payloadLen) {
  memcpy(g_framebuffer, g_staging, payloadLen);
  g_lastSledsMs = millis();
  g_connected = true;
}

} // namespace

void simhub_init() {
  memset(g_framebuffer, 0, sizeof(g_framebuffer));
  memset(g_staging, 0, sizeof(g_staging));
  g_connected = false;
  g_lastSledsMs = 0;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  g_ledCount = prefs.getUShort(NVS_KEY_LEDCNT, SIMHUB_LED_COUNT_DEFAULT);
  prefs.end();

  if (g_ledCount < 1 || g_ledCount > SIMHUB_LED_COUNT_MAX) {
    g_ledCount = SIMHUB_LED_COUNT_DEFAULT; // valor gravado invalido/corrompido -> volta pro default
  }
}

bool simhub_set_led_count(uint16_t n) {
  if (n < 1 || n > SIMHUB_LED_COUNT_MAX) {
    return false;
  }

  g_ledCount = n;
  memset(g_framebuffer, 0, sizeof(g_framebuffer));
  memset(g_staging, 0, sizeof(g_staging));
  g_connected = false; // forca o SimHub a mandar um "sleds" novo antes de reportar conectado de novo

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putUShort(NVS_KEY_LEDCNT, n);
  prefs.end();

  return true;
}

bool simhub_feed_header_byte(uint8_t b) {
  static uint8_t consecutiveFF = 0;

  if (b == 0xFF) {
    consecutiveFF++;
    if (consecutiveFF >= SIMHUB_HEADER_LEN) {
      consecutiveFF = 0;
      return true;
    }
    return false;
  }

  consecutiveFF = 0; // qualquer byte que nao seja 0xFF interrompe a contagem
  return false;
}

void simhub_process_packet() {
  const uint32_t deadline = millis() + SIMHUB_FRAME_TIMEOUT_MS;

  // Os 4 comandos conhecidos ("proto", "ledsc", "sleds", "unlock")
  // compartilham os 5 primeiros bytes com no maximo um deles cada —
  // basta ler 5 bytes pra desambiguar entre proto/ledsc/sleds, e mais 1
  // (o 6o) so no caso de ser "unlock".
  uint8_t cmd[5];
  for (uint8_t i = 0; i < 5; i++) {
    const int b = readByteUntil(deadline);
    if (b < 0) return; // timeout no cabecalho do comando -> descarta o frame
    cmd[i] = (uint8_t)b;
  }

  if (memcmp(cmd, "proto", 5) == 0) {
    Serial.print("SIMHUB_1.0\r\n");
    return;
  }

  if (memcmp(cmd, "ledsc", 5) == 0) {
    Serial.printf("%u\r\n", (unsigned)simhub_get_led_count());
    return;
  }

  if (memcmp(cmd, "sleds", 5) == 0) {
    const uint16_t payloadLen = simhub_get_led_count() * 3;

    for (uint16_t i = 0; i < payloadLen; i++) {
      const int b = readByteUntil(deadline);
      if (b < 0) return; // timeout no meio do payload -> descarta, nao publica
      g_staging[i] = (uint8_t)b;
    }

    uint8_t term[3];
    for (uint8_t i = 0; i < 3; i++) {
      const int b = readByteUntil(deadline);
      if (b < 0) return;
      term[i] = (uint8_t)b;
    }

    if (term[0] == 0xFF && term[1] == 0xFE && term[2] == 0xFD) {
      publishFramebuffer(payloadLen);
    }
    // Terminador nao bateu: frame corrompido/dessincronizado — descarta
    // silenciosamente. O proximo cabecalho de 6x 0xFF (que o SimHub manda
    // de novo no proximo frame) resincroniza sozinho, sem estado para
    // limpar aqui.
    return;
  }

  if (memcmp(cmd, "unloc", 5) == 0) {
    const int b = readByteUntil(deadline);
    if (b == 'k') {
      Serial.print("Upload unlocked\r\n");
    }
    return;
  }

  // Comando desconhecido: descarta sem travar. Perda de sincronizacao se
  // resolve sozinha no proximo cabecalho de 6x 0xFF.
}

void simhub_update() {
  if (g_connected && (millis() - g_lastSledsMs) > SIMHUB_CONNECTION_TIMEOUT_MS) {
    g_connected = false;
    memset(g_framebuffer, 0, sizeof(g_framebuffer)); // volta pra apagado se a conexao cair
  }
}

bool simhub_is_connected() {
  return g_connected;
}

uint16_t simhub_get_led_count() {
  return g_ledCount;
}

SimhubColor simhub_get_led(uint16_t index) {
  if (index >= g_ledCount) return {0, 0, 0};
  const uint16_t base = index * 3;
  return {g_framebuffer[base], g_framebuffer[base + 1], g_framebuffer[base + 2]};
}
