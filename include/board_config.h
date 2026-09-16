// board_config.h — MAPA ÚNICO de hardware deste projeto.
//
// Trocou de placa (ESP32 maior, outra variante) ou quer reaproveitar este
// firmware com pinos diferentes? **Só este arquivo precisa mudar.** Nenhum
// outro .cpp/.h do projeto tem pino/canal/endereço fixo "espalhado" —
// tudo (main.cpp, lib/inputs, e os firmwares de teste isolados
// mcp_test.cpp/mux_test.cpp/encoder_test.cpp/ws2812_test.cpp/
// inputs_test.cpp) lê os valores daqui.
//
// Os drivers de baixo nível (lib/mcp23017, lib/mux4067, lib/encoders,
// lib/ws2812) continuam genéricos e reutilizáveis sozinhos em QUALQUER
// outro projeto — eles não incluem este arquivo, só aceitam pino/endereço
// como parâmetro (com um default próprio, caso alguém use o driver isolado
// sem este arquivo). Este header é a "cola" específica deste projeto que
// decide quais valores passar pra cada um.
//
// Mapa físico completo, com o raciocínio de cada escolha, está em
// docs/INPUTS_PINOUT.md e docs/SYSTEM_INTEGRATION.md — este arquivo é só
// os números; a justificativa fica na documentação, pra não duplicar.
#pragma once
#include <stdint.h>

// ======================================================================
// MCP23017 (expansor de I/O via I2C) — 11 push buttons + ignição + start
// ======================================================================
static const uint8_t  BOARD_I2C_SDA_PIN   = 8;
static const uint8_t  BOARD_I2C_SCL_PIN   = 9;
static const uint8_t  BOARD_MCP23017_ADDR = 0x20; // A0/A1/A2 no GND

// ======================================================================
// 74HC4067 (mux digital de 16 canais) — SW dos encoders, chaves caça,
// freio de estacionamento
// ======================================================================
static const uint8_t  BOARD_MUX_S0_PIN         = 15;
static const uint8_t  BOARD_MUX_S1_PIN         = 16;
static const uint8_t  BOARD_MUX_S2_PIN         = 17;
static const uint8_t  BOARD_MUX_S3_PIN         = 18;
static const uint8_t  BOARD_MUX_SIG_PIN        = 21;
static const uint8_t  BOARD_MUX_CHANNEL_COUNT  = 9; // C0-C8 do mapa aprovado (docs/INPUTS_PINOUT.md secao 3)

// ======================================================================
// Encoders KY-040 (CLK/DT direto na MCU, quadratura por interrupção)
// ======================================================================
static const uint8_t BOARD_ENCODER_COUNT = 4;

static const uint8_t BOARD_ENCODER_CLK_PIN[BOARD_ENCODER_COUNT] = {4, 6, 10, 12};
static const uint8_t BOARD_ENCODER_DT_PIN[BOARD_ENCODER_COUNT]  = {5, 7, 11, 13};

// ======================================================================
// WS2812 (matriz 8x8 + fita, uma única cadeia em série)
// ======================================================================
static const uint8_t BOARD_WS2812_PIN = 1;

// ======================================================================
// Botão BOOT da placa (strapping pin, sempre GPIO0 em qualquer ESP32) —
// usado só para o gesto de abrir o portal de WiFi (segurar 5s), não
// alimenta HID. Não depende do resto do mapa, mas fica aqui por
// completude — é o único pino que NÃO muda entre placas ESP32, mas ainda
// assim é nomeado em vez de mágico no meio do código.
// ======================================================================
static const uint8_t BOARD_BOOT_PIN = 0;
