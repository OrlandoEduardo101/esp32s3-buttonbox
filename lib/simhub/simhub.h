// Protocolo SimHub "Arduino" — o que a aba Arduino do SimHub realmente fala.
//
// ATENÇÃO (correção de rota): a primeira versão deste módulo implementava o
// protocolo `0xFF×6 + "proto"/"ledsc"/"sleds"`, documentado na wiki do
// SimHub como "SIMHUB STANDARD ARDUINO PRO MICRO LEDs sketch". Aquele
// protocolo existe, mas é do sketch legado de LEDs — **não é** o que o
// scanner da aba Arduino usa. Prova: o log do SimHub do usuário mostrava
// `Hello (sending)` → `Unrecognized` contra um firmware que respondia
// perfeitamente ao `proto`/`ledsc`/`sleds` (testado com
// scripts/simhub_test_send.py).
//
// Este arquivo implementa o protocolo REAL, extraído de duas
// implementações que comprovadamente funcionam com o SimHub do usuário:
//   ~/Projects/arduino/ESP-SimHub            (upstream, src/SHCommands.h)
//   ~/Projects/arduino/ESP-SimHub-ESP32S3-SCREEN  (fork em uso pelo usuário)
//
// Framing:
//   0x03 (MESSAGE_HEADER) + 1 char de comando + payload específico
//
// Comandos implementados aqui:
//   '1' Hello        -> lê 1 byte (trailer), responde o char de versão ('j')
//   '0' Features     -> responde as letras de capacidade + "\n"
//   '4' RGB count    -> 1 byte com a quantidade de LEDs da FITA
//   '6' RGB data     -> stream RGB (modos 1/2/3) -> framebuffer da fita, ACK 0x15
//   'R' Matrix data  -> stream RGB -> framebuffer da matriz 8x8, ACK 0x15
//   'N' Device name  -> nome + "\n"
//   'I' Unique ID    -> id (MAC) + "\n"
//   'A' Acq          -> 0x03
//   'X' Expandido    -> "list" / "mcutype" / keepalive
//   'J' '2' 'B'      -> contadores (0: botões vão por HID nativo, não por aqui)
//   'G' Gear         -> lê 1 char, ACK
//   '8' Baudrate     -> consome o byte de código e ignora (irrelevante em USB CDC)
//
// NÃO implementa o comando 'P' (SHCustomProtocol) e não o anuncia em
// Features — decisão explícita do usuário: todos os efeitos de LED são
// configurados no SimHub, nenhum comportamento é escrito no firmware.
//
// Divisão dos LEDs (o SimHub trata como dois dispositivos lógicos):
//   RGB Matrix (feature 'R') = matriz 8x8 fixa, 64 pixels -> iFlag etc.
//   RGB Leds   (comando '4') = fita, quantidade configurável -> RPM etc.
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct SimhubColor {
  uint8_t r, g, b;
};

// Matriz é sempre 8x8 no protocolo do SimHub (o driver de referência
// instancia 64 pixels fixos, sem comando de contagem).
static const uint16_t SIMHUB_MATRIX_LED_COUNT = 64;

// Fita: quantidade configurável em runtime (NVS), comando serial
// "SETLEDS <n>". O SimHub lê esse valor pelo comando '4'.
static const uint16_t SIMHUB_STRIP_COUNT_DEFAULT = 10;
static const uint16_t SIMHUB_STRIP_COUNT_MAX     = 192; // teto do buffer interno

// Char de versão devolvido no Hello — mesmo valor das implementações de
// referência. O SimHub usa isso pra saber que o dispositivo é compatível.
static const char SIMHUB_VERSION_CHAR = 'j';

// Nome anunciado no comando 'N' (aparece na lista de dispositivos do
// SimHub). Escolhido pra não colidir com os outros dispositivos do
// usuário ("ESP-SimHubDisplay", "ESP-ButtonBox-WHEEL").
static const char *const SIMHUB_DEVICE_NAME = "ESP32S3-ButtonBox";

// Orçamento de tempo TOTAL para consumir um comando inteiro depois do
// header. Cobre fragmentação do USB CDC sem travar o loop principal se o
// SimHub parar de mandar bytes no meio.
static const uint32_t SIMHUB_FRAME_TIMEOUT_MS = 150;

// Sem atividade serial por este tempo -> considera o SimHub desconectado e
// apaga os dois framebuffers (mesmo comportamento do Command_Shutdown das
// implementações de referência, que usam 5s).
static const uint32_t SIMHUB_CONNECTION_TIMEOUT_MS = 5000;

void simhub_init();

// true se o byte é o header de um comando SimHub (0x03). Quem lê a serial
// chama isto antes de tratar o byte como texto do console.
bool simhub_is_header_byte(uint8_t b);

// Consome e processa UM comando completo (char de comando + payload),
// logo depois de simhub_is_header_byte() ter devolvido true. Bloqueia no
// máximo SIMHUB_FRAME_TIMEOUT_MS no total.
void simhub_process_packet();

// Chamar do loop(): só verifica o timeout de conexão. Não lê serial.
void simhub_update();

bool simhub_is_connected();

// --- Framebuffers -----------------------------------------------------
uint16_t simhub_get_strip_count();
bool     simhub_set_strip_count(uint16_t n); // persiste em NVS

SimhubColor simhub_get_strip_led(uint16_t index);  // 0..strip_count-1
SimhubColor simhub_get_matrix_led(uint16_t index); // 0..63, ordem linear do SimHub
