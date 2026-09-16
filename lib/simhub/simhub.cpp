#include "simhub.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "class/cdc/cdc_device.h" // tud_cdc_n_write() — ver rawWrite()

namespace {

// ---------------------------------------------------------------------
// Camada ARQ (transporte do SimHub)
//
// O SimHub NAO manda os comandos crus na serial: tudo vai dentro de um
// pacote ARQ com id e CRC8, e o dispositivo tem que confirmar cada pacote.
//
//   Host -> device:  0x01 0x01 <packetId> <len> <dados...> <crc8>
//   Device -> host:  ACK  = 0x03 <packetId>
//                    NACK = 0x04 <ultimoPacoteValido> <motivo>
//
// Exemplo real (do proprio codigo de referencia): Hello =
//   01 01 FF 03 03 31 10 6A
//   header duplo | id 255 | len 3 | dados 03 '1' 0x10 | crc 0x6A
//
// Os DADOS de dentro e' que carregam o protocolo de comandos:
//   0x03 (MESSAGE_HEADER) + char de comando + payload
//
// Respostas do dispositivo tambem sao enquadradas (fora do ARQ):
//   byte    -> 0x08 <byte>
//   string  -> 0x06 <len> <bytes> 0x20
//   string+ -> 0x06 <len+1> <bytes> '\n' 0x20   (PrintLn)
//
// Referencia: ~/Projects/arduino/ESP-SimHub/src/ArqSerial.h
// ---------------------------------------------------------------------

const uint8_t CRC8_TABLE[256] = {
  0,213,127,170,254,43,129,84,41,252,86,131,215,2,168,125,82,135,45,248,172,121,211,6,123,174,4,209,133,80,250,47,
  164,113,219,14,90,143,37,240,141,88,242,39,115,166,12,217,246,35,137,92,8,221,119,162,223,10,160,117,33,244,94,139,
  157,72,226,55,99,182,28,201,180,97,203,30,74,159,53,224,207,26,176,101,49,228,78,155,230,51,153,76,24,205,103,178,
  57,236,70,147,199,18,184,109,16,197,111,186,238,59,145,68,107,190,20,193,149,64,234,63,66,151,61,232,188,105,195,22,
  239,58,144,69,17,196,110,187,198,19,185,108,56,237,71,146,189,104,194,23,67,150,60,233,148,65,235,62,106,191,21,192,
  75,158,52,225,181,96,202,31,98,183,29,200,156,73,227,54,25,204,102,179,231,50,152,77,48,229,79,154,206,27,177,100,
  114,167,13,216,140,89,243,38,91,142,36,241,165,112,218,15,32,245,95,138,222,11,161,116,9,220,118,163,247,34,136,93,
  214,3,169,124,40,253,87,130,255,42,128,85,1,212,126,171,132,81,251,46,122,175,5,208,173,120,210,7,83,134,44,249
};

constexpr uint8_t ARQ_HEADER        = 0x01;
constexpr uint8_t ARQ_ACK           = 0x03;
constexpr uint8_t ARQ_NACK          = 0x04;
constexpr uint8_t FRAME_STRING      = 0x06;
constexpr uint8_t FRAME_BYTE        = 0x08;
constexpr uint8_t FRAME_STRING_END  = 0x20;
constexpr uint8_t ARQ_MAX_DATA      = 32;

constexpr uint8_t MESSAGE_HEADER = 0x03;
constexpr uint8_t ACK_BYTE       = 0x15;

// Assinatura de MCU devolvida em "X mcutype" — as implementacoes de
// referencia reportam a assinatura de um ATmega32U4 (Leonardo/Pro Micro),
// que e' o que o SimHub espera de um "Arduino padrao".
constexpr uint8_t SIGNATURE_0 = 0x1E;
constexpr uint8_t SIGNATURE_1 = 0x98;
constexpr uint8_t SIGNATURE_2 = 0x01;

constexpr const char *NVS_NAMESPACE = "simhub";
constexpr const char *NVS_KEY_STRIP = "stripcount";

uint8_t g_strip[SIMHUB_STRIP_COUNT_MAX * 3];
uint8_t g_matrix[SIMHUB_MATRIX_LED_COUNT * 3];

uint16_t g_stripCount = SIMHUB_STRIP_COUNT_DEFAULT;
bool     g_connected  = false;
uint32_t g_lastActivityMs = 0;

int g_lastValidPacket = 255;

// Buffer dos dados JA decodificados do ARQ (o que os comandos consomem).
constexpr uint16_t PAYLOAD_SIZE = 128;
uint8_t  g_payload[PAYLOAD_SIZE];
uint16_t g_payloadHead = 0;
uint16_t g_payloadCount = 0;

void payloadPush(uint8_t b) {
  if (g_payloadCount >= PAYLOAD_SIZE) return; // cheio: descarta (nao deveria acontecer)
  const uint16_t tail = (g_payloadHead + g_payloadCount) % PAYLOAD_SIZE;
  g_payload[tail] = b;
  g_payloadCount++;
}

int payloadPop() {
  if (g_payloadCount == 0) return -1;
  const uint8_t b = g_payload[g_payloadHead];
  g_payloadHead = (g_payloadHead + 1) % PAYLOAD_SIZE;
  g_payloadCount--;
  return b;
}

// --- Escrita crua -----------------------------------------------------
// NAO usar Serial.write(): o USBCDC::write() do core descarta tudo quando
// tud_cdc_n_connected() e' falso, e isso depende do host assertar DTR. O
// SimHub abre a porta SEM assertar DTR (assertar reseta placas Arduino de
// verdade), entao as respostas morreriam dentro do USBCDC antes de sair.
// Escrevendo direto na TinyUSB o gate nao se aplica.
constexpr uint8_t  CDC_ITF = 0; // `Serial` e' USBCDC(0) no core
constexpr uint32_t WRITE_TIMEOUT_MS = 50;

void rawWrite(const uint8_t *data, size_t len) {
  size_t sent = 0;
  const uint32_t deadline = millis() + WRITE_TIMEOUT_MS;
  while (sent < len && (int32_t)(millis() - deadline) < 0) {
    sent += tud_cdc_n_write(CDC_ITF, data + sent, len - sent);
    tud_cdc_n_write_flush(CDC_ITF);
  }
}

void rawWriteByte(uint8_t b) { rawWrite(&b, 1); }

// --- Respostas enquadradas -------------------------------------------
void frameByte(uint8_t b) {
  const uint8_t out[2] = {FRAME_BYTE, b};
  rawWrite(out, 2);
}

void frameString(const char *s) {
  const uint8_t len = (uint8_t)strlen(s);
  rawWriteByte(FRAME_STRING);
  rawWriteByte(len);
  rawWrite((const uint8_t *)s, len);
  rawWriteByte(FRAME_STRING_END);
}

void frameStringLn(const char *s) {
  const uint8_t len = (uint8_t)strlen(s);
  rawWriteByte(FRAME_STRING);
  rawWriteByte((uint8_t)(len + 1));
  rawWrite((const uint8_t *)s, len);
  rawWriteByte('\n');
  rawWriteByte(FRAME_STRING_END);
}

void sendAck(uint8_t packetId) {
  const uint8_t out[2] = {ARQ_ACK, packetId};
  rawWrite(out, 2);
}

void sendNack(uint8_t lastValid, uint8_t reason) {
  const uint8_t out[3] = {ARQ_NACK, lastValid, reason};
  rawWrite(out, 3);
}

// --- Leitura ----------------------------------------------------------
int serialReadUntil(uint32_t deadlineMs) {
  while ((int32_t)(millis() - deadlineMs) < 0) {
    if (Serial.available() > 0) return Serial.read();
  }
  return -1;
}

void markActivity() {
  g_lastActivityMs = millis();
  g_connected = true;
}

// Le o corpo de um pacote ARQ (os dois bytes 0x01 ja consumidos),
// valida o CRC, responde ACK/NACK e empilha os dados decodificados.
// Devolve true se o pacote foi aceito.
bool parseArqBody(uint32_t deadlineMs) {
  const int packetId = serialReadUntil(deadlineMs);
  if (packetId < 0) {
    sendNack((uint8_t)g_lastValidPacket, 0x01); // id ruim
    return false;
  }

  const int length = serialReadUntil(deadlineMs);
  if (length <= 0 || length > ARQ_MAX_DATA) {
    sendNack((uint8_t)g_lastValidPacket, 0x02); // tamanho ruim
    return false;
  }

  uint8_t data[ARQ_MAX_DATA];
  for (int i = 0; i < length; i++) {
    const int b = serialReadUntil(deadlineMs);
    if (b < 0) {
      sendNack((uint8_t)g_lastValidPacket, 0x05); // dados incompletos
      return false;
    }
    data[i] = (uint8_t)b;
  }

  const int crc = serialReadUntil(deadlineMs);
  if (crc < 0) {
    sendNack((uint8_t)g_lastValidPacket, 0x03); // sem checksum
    return false;
  }

  uint8_t current = 0;
  current = CRC8_TABLE[current ^ (uint8_t)packetId];
  current = CRC8_TABLE[current ^ (uint8_t)length];
  for (int i = 0; i < length; i++) {
    current = CRC8_TABLE[current ^ data[i]];
  }

  if ((uint8_t)crc != current) {
    sendNack((uint8_t)g_lastValidPacket, 0x04); // checksum nao bate
    return false;
  }

  // Aceita o pacote se for o proximo da sequencia ou broadcast (255).
  const int nextId = (g_lastValidPacket > 127) ? 0 : g_lastValidPacket + 1;
  if (packetId == nextId || packetId == 255) {
    for (int i = 0; i < length; i++) payloadPush(data[i]);
    g_lastValidPacket = packetId;
  }

  sendAck((uint8_t)packetId);
  markActivity();
  return true;
}

// Puxa mais um pacote ARQ da serial (usado quando um comando precisa de
// mais bytes do que couberam no pacote atual).
void pullNextArqPacket(uint32_t deadlineMs) {
  const int b1 = serialReadUntil(deadlineMs);
  if (b1 != ARQ_HEADER) return;
  const int b2 = serialReadUntil(deadlineMs);
  if (b2 != ARQ_HEADER) return;
  parseArqBody(deadlineMs);
}

// Le um byte do fluxo JA decodificado, puxando mais pacotes ARQ se
// precisar. -1 se estourar o prazo.
int arqRead(uint32_t deadlineMs) {
  for (;;) {
    const int b = payloadPop();
    if (b >= 0) return b;
    if ((int32_t)(millis() - deadlineMs) >= 0) return -1;
    if (Serial.available() > 0) pullNextArqPacket(deadlineMs);
  }
}

// Stream RGB do SimHub — formato do SHRGBLedsBase::read() da referencia:
//   mode = byte
//   enquanto mode > 0:
//     1 -> todos os LEDs: (r,g,b) x ledCount
//     2 -> startLed, numLeds, depois (r,g,b) x numLeds
//     3 -> startLed, numLeds, um (r,g,b) repetido no intervalo
//     mode = byte
void readRgbStream(uint8_t *fb, uint16_t ledCount, uint32_t deadlineMs) {
  int mode = arqRead(deadlineMs);

  while (mode > 0) {
    if (mode == 1) {
      for (uint16_t j = 0; j < ledCount; j++) {
        const int r = arqRead(deadlineMs);
        const int g = arqRead(deadlineMs);
        const int b = arqRead(deadlineMs);
        if (r < 0 || g < 0 || b < 0) return;
        fb[j * 3 + 0] = (uint8_t)r;
        fb[j * 3 + 1] = (uint8_t)g;
        fb[j * 3 + 2] = (uint8_t)b;
      }
    } else if (mode == 2 || mode == 3) {
      const int start = arqRead(deadlineMs);
      const int num   = arqRead(deadlineMs);
      if (start < 0 || num < 0) return;

      if (mode == 3) {
        const int r = arqRead(deadlineMs);
        const int g = arqRead(deadlineMs);
        const int b = arqRead(deadlineMs);
        if (r < 0 || g < 0 || b < 0) return;
        for (int j = start; j < start + num; j++) {
          if (j >= 0 && j < (int)ledCount) {
            fb[j * 3 + 0] = (uint8_t)r;
            fb[j * 3 + 1] = (uint8_t)g;
            fb[j * 3 + 2] = (uint8_t)b;
          }
        }
      } else {
        for (int j = start; j < start + num; j++) {
          const int r = arqRead(deadlineMs);
          const int g = arqRead(deadlineMs);
          const int b = arqRead(deadlineMs);
          if (r < 0 || g < 0 || b < 0) return;
          if (j >= 0 && j < (int)ledCount) {
            fb[j * 3 + 0] = (uint8_t)r;
            fb[j * 3 + 1] = (uint8_t)g;
            fb[j * 3 + 2] = (uint8_t)b;
          }
        }
      }
    } else {
      return; // modo desconhecido
    }

    mode = arqRead(deadlineMs);
  }
}

String readStringUntil(char t1, char t2, uint32_t deadlineMs) {
  String out;
  while (out.length() < 32) {
    const int b = arqRead(deadlineMs);
    if (b < 0 || (char)b == t1 || (char)b == t2) break;
    out += (char)b;
  }
  return out;
}

void dispatchCommand(char cmd, uint32_t deadlineMs) {
  switch (cmd) {
    case '1': { // Hello
      arqRead(deadlineMs); // byte de trailer (0x10 no exemplo), descartado
      frameByte((uint8_t)SIMHUB_VERSION_CHAR);
      break;
    }

    case '0': { // Features — cada letra vai num frame proprio
      frameString("N"); // nome
      frameString("I"); // unique id
      frameString("X"); // comandos expandidos
      frameString("R"); // matriz RGB
      frameString("\n");
      break;
    }

    case '4': frameByte((uint8_t)g_stripCount); break; // LEDs da fita

    case '6': // Dados RGB da fita
      readRgbStream(g_strip, g_stripCount, deadlineMs);
      frameByte(ACK_BYTE);
      break;

    case 'R': // Dados RGB da matriz 8x8
      readRgbStream(g_matrix, SIMHUB_MATRIX_LED_COUNT, deadlineMs);
      frameByte(ACK_BYTE);
      break;

    case 'N': // Nome do dispositivo
      frameString(SIMHUB_DEVICE_NAME);
      frameString("\n");
      break;

    case 'I': { // Unique ID — MAC da placa
      uint8_t mac[6] = {0};
      esp_efuse_mac_get_default(mac);
      char id[13];
      snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      frameString(id);
      frameString("\n");
      break;
    }

    case 'A': frameByte(0x03); break; // Acq

    case 'X': { // Comandos expandidos
      const String action = readStringUntil(' ', '\n', deadlineMs);
      if (action == "list") {
        frameStringLn("mcutype");
        frameStringLn("keepalive");
        frameByte('\n');
      } else if (action == "mcutype") {
        frameByte(SIGNATURE_0);
        frameByte(SIGNATURE_1);
        frameByte(SIGNATURE_2);
      } else {
        frameByte(ACK_BYTE); // keepalive e afins
      }
      break;
    }

    case 'J': // Botoes por serial: nenhum (vao pelo HID nativo)
    case '2': // Modulos TM1638: nenhum
    case 'B': // Modulos simples: nenhum
      frameByte(0);
      break;

    case 'G': // Marcha — nao anunciamos 'G', mas respondemos se vier
      arqRead(deadlineMs);
      frameByte(ACK_BYTE);
      break;

    case '8': // Baudrate: irrelevante em USB CDC, so consome o codigo
      arqRead(deadlineMs);
      break;

    default:
      break; // desconhecido: ignora
  }
}

} // namespace

void simhub_init() {
  memset(g_strip, 0, sizeof(g_strip));
  memset(g_matrix, 0, sizeof(g_matrix));
  g_connected = false;
  g_lastActivityMs = 0;
  g_lastValidPacket = 255;
  g_payloadHead = 0;
  g_payloadCount = 0;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  g_stripCount = prefs.getUShort(NVS_KEY_STRIP, SIMHUB_STRIP_COUNT_DEFAULT);
  prefs.end();

  if (g_stripCount < 1 || g_stripCount > SIMHUB_STRIP_COUNT_MAX) {
    g_stripCount = SIMHUB_STRIP_COUNT_DEFAULT;
  }
}

bool simhub_is_header_byte(uint8_t b) {
  return b == ARQ_HEADER;
}

void simhub_process_packet() {
  const uint32_t deadline = millis() + SIMHUB_FRAME_TIMEOUT_MS;

  // O primeiro 0x01 ja foi consumido por quem chamou; o segundo tem que
  // vir agora, senao nao e' um pacote ARQ.
  if (serialReadUntil(deadline) != ARQ_HEADER) return;

  if (!parseArqBody(deadline)) return;

  // Processa os comandos que vieram nos dados decodificados.
  while (g_payloadCount > 0) {
    const int header = payloadPop();
    if (header != MESSAGE_HEADER) continue; // ressincroniza

    const int cmd = arqRead(deadline);
    if (cmd < 0) return;
    dispatchCommand((char)cmd, deadline);
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
  if (n < 1 || n > SIMHUB_STRIP_COUNT_MAX) return false;

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
