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
// ======================================================================
// REVISÃO 2 (set/2026) — "só até o GPIO13"
// ======================================================================
// A ESP32-S3 SuperMini só expõe em header de fácil acesso os GPIO 0-13. Do
// GPIO14 em diante (14-18, 21, 33-48) os sinais existem no módulo, mas
// saem em PADS na FACE INFERIOR da placa — soldáveis, porém sob o corpo da
// placa, o que inviabiliza montagem manual decente (isto inclui o próprio
// GPIO14, não só 15 pra cima). A revisão 1 deste mapa colocava as 5 linhas
// do 74HC4067 (S0-S3 + SIG) justamente em 15/16/17/18/21.
//
// Solução adotada (opção B, escolhida pelo usuário): os 8 sinais de
// quadratura dos 4 encoders SAEM do GPIO direto e passam para o banco A
// do MCP23017, liberando 8 pinos de header; o 74HC4067 assume as linhas
// de botão e ocupa 5 desses pinos livres. Resultado: **nenhum pino usado
// vai além do GPIO13**, e ainda sobram 11/12/13 livres.
//
// Consequência técnica que NÃO pode ser esquecida: quadratura por I2C não
// tem interrupção por borda, então a amostragem passou a ser feita por uma
// task dedicada (lib/inputs) a BOARD_MCP_SAMPLE_PERIOD_MS — ver o
// comentário dessa constante mais abaixo antes de mexer nela.
//
// Mapa físico completo, com o raciocínio de cada escolha, está em
// docs/INPUTS_PINOUT.md e docs/SYSTEM_INTEGRATION.md — este arquivo é só
// os números; a justificativa fica na documentação, pra não duplicar.
#pragma once
#include <stdint.h>

// ======================================================================
// MCP23017 (expansor de I/O via I2C)
//   Banco A (GPA0-GPA7) -> CLK/DT dos 4 encoders (quadratura)
//   Banco B (GPB0-GPB3) -> SW dos 4 encoders
//   Banco B (GPB4-GPB7) -> 4 chaves tipo caça
// ======================================================================
static const uint8_t  BOARD_I2C_SDA_PIN   = 8;
static const uint8_t  BOARD_I2C_SCL_PIN   = 9;
static const uint8_t  BOARD_MCP23017_ADDR = 0x20; // A0/A1/A2 no GND

// INT do MCP23017: NAO FIAR. Removido do mapa de proposito.
//
// O INT (INTA/INTB do chip) e a saida que o MCP23017 aciona sozinho quando
// uma entrada habilitada muda — serve para o MCU nao precisar ficar
// perguntando. Este firmware nunca habilita GPINTEN nem registra
// attachInterrupt: a amostragem e periodica (ver a constante abaixo), entao
// o INT nao tem funcao nenhuma aqui.
//
// Uma revisao anterior deste arquivo reservava o GPIO14 para ele. Era um
// erro duplo: alem de inutil, o GPIO14 na ESP32-S3 SuperMini/S3 Zero e um
// pad da face inferior — exatamente o tipo de pino que esta revisao existe
// para evitar. Se um dia a amostragem virar orientada a evento, escolha um
// GPIO de header LIVRE na epoca e declare aqui; nao volte pro 14.

// Período de amostragem do MCP23017 pela task dedicada de lib/inputs.
//
// Por que 1 ms (e por que NÃO pode virar 5 ms): um KY-040 tem 20 detents
// por volta e percorre o ciclo de Gray completo (4 transições) a cada
// detent. Num giro rápido de mão (~1,5 volta/s = 30 detents/s) um detent
// dura ~33 ms e suas 4 transições se espalham por ~5-10 ms entre si. A 1
// ms de amostragem todas são capturadas com folga; a 5 ms, duas transições
// vizinhas caem na MESMA amostra, a máquina de quadratura vê um salto de 2
// bits (fisicamente impossível), reseta pro repouso e o detent é PERDIDO.
// O loop() principal tem delay(5) no fim — é exatamente por isso que a
// amostragem NÃO pode morar nele e ganhou task própria.
//
// Custo: uma leitura de 2 bytes a 400 kHz gasta ~150 us, ou seja ~15% do
// barramento I2C, que não tem mais nenhum outro dispositivo. Aceitável.
static const uint32_t BOARD_MCP_SAMPLE_PERIOD_MS = 1;

// ======================================================================
// Encoders KY-040 — CLK/DT no banco A do MCP23017 (não mais em GPIO)
// Índices na convenção de bit do input_expander: 0-7 = GPA0-GPA7.
// ======================================================================
static const uint8_t BOARD_ENCODER_COUNT = 4;

static const uint8_t BOARD_ENCODER_MCP_CLK_BIT[BOARD_ENCODER_COUNT] = {0, 2, 4, 6};
static const uint8_t BOARD_ENCODER_MCP_DT_BIT[BOARD_ENCODER_COUNT]  = {1, 3, 5, 7};

// ======================================================================
// 74HC4067 (mux digital de 16 canais) — 11 push buttons, start engine,
// ignição (ON/IGN) e freio de estacionamento. Todas as 5 linhas de
// controle em pinos de header (GPIO4-7 e 10).
// ======================================================================
static const uint8_t  BOARD_MUX_S0_PIN         = 4;
static const uint8_t  BOARD_MUX_S1_PIN         = 5;
static const uint8_t  BOARD_MUX_S2_PIN         = 6;
static const uint8_t  BOARD_MUX_S3_PIN         = 7;
static const uint8_t  BOARD_MUX_SIG_PIN        = 10;
static const uint8_t  BOARD_MUX_CHANNEL_COUNT  = 15; // C0-C14 (docs/INPUTS_PINOUT.md secao 3)

// ======================================================================
// WS2812 (matriz 8x8 + fita, uma única cadeia em série)
// ======================================================================
static const uint8_t BOARD_WS2812_PIN = 1;

// Remapeamento da matriz 8x8: o SimHub manda os 64 pixels em ordem linear
// (linha a linha, esquerda->direita, topo->baixo). O remapeamento em
// src/main.cpp converte isso pro indice fisico real da cadeia, em duas
// etapas independentes — cada uma resolve um problema de fiação diferente:
//
//   MATRIX_SERPENTINE: quase todo painel 8x8 WS2812 barato e' fiado em
//   zigue-zague por dentro (uma trilha so, sem pular linha) — a linha 0 vai
//   esquerda->direita, a linha 1 direita->esquerda, e assim por diante.
//   true corrige isso; false assume fiacao reta (cada linha sempre
//   esquerda->direita), rara em paineis prontos.
//
//   MATRIX_MIRROR_X: espelhamento adicional e' o PAINEL inteiro montado/
//   ligado com o DIN do lado oposto do esperado (ex.: entra pela direita em
//   vez da esquerda) — isso espelha a imagem toda no eixo horizontal, POR
//   CIMA do zigue-zague acima (as duas causas sao independentes: uma e' a
//   fiacao interna do painel, a outra e' como ele foi montado/orientado).
//   Sintoma classico: um numero ou letra aparece com a lateralidade
//   trocada (ex.: um "2" enviado pelo SimHub aparece parecido com um "S").
//
// Se a imagem ainda sair errada depois de ajustar estas duas constantes,
// o proximo suspeito e' inversao VERTICAL (linha 0 fisica = topo do
// SimHub ou o contrario) — ainda nao existe flag pronta pra isso; avise
// se precisar.
static const bool BOARD_MATRIX_SERPENTINE = true;
static const bool BOARD_MATRIX_MIRROR_X   = true;

// 0x0800 = canal C11 (Start Engine) tratado como active-high.
//
// Botão montado nesta bancada é comum-ÂNODO (COM → 3,3V). Fechar a chave
// arrasta C11 para 3,3V, e o pull-down externo de ~4,7 kΩ entre C11 e GND
// (já soldado — confirmado com o usuário) resolve o problema histórico de
// pino flutuante: sem esse resistor, o pull-up interno da ESP32 no SIG
// mantinha a linha em HIGH mesmo com o canal desconectado, e o botão 18
// ficava SEMPRE acionado. Com o pull-down presente, active-high é seguro.
//
// Se um dia o botão for trocado por um de comum-CÁTODO (COM → GND), volte
// isto para 0x0000 e remova o pull-down (ele deixaria de fazer sentido) —
// ver a variante A em docs/INPUTS_PINOUT.md seção 11.
static const uint16_t BOARD_MUX_ACTIVE_HIGH_MASK = 0x0800;

// true  -> o LED acende com o GPIO em LOW (a ESP32 DRENA a corrente; use
//          quando o comum/ânodo do botão estiver amarrado em 3,3 V).
// false -> acende com o GPIO em HIGH (a ESP32 FORNECE; comum/cátodo no GND).
// Anda junto com BOARD_MUX_ACTIVE_HIGH_MASK: se você mudou um, o outro
// quase certamente também muda.
static const bool BOARD_START_ENGINE_LED_ACTIVE_LOW = true;

// LED do botão Start Engine (saída). Acompanha a ignição:
// aceso com INPUT_IGNITION_ON fechado, apagado com a chave em OFF — ver
// updateStartEngineLed() em src/main.cpp e docs/INPUTS_PINOUT.md secao 6.
// ======================================================================
static const uint8_t BOARD_START_ENGINE_LED_PIN = 2;

// ======================================================================
// Botão BOOT da placa (strapping pin, sempre GPIO0 em qualquer ESP32) —
// usado só para o gesto de abrir o portal de WiFi (segurar 5s), não
// alimenta HID. Não depende do resto do mapa, mas fica aqui por
// completude — é o único pino que NÃO muda entre placas ESP32, mas ainda
// assim é nomeado em vez de mágico no meio do código.
// ======================================================================
static const uint8_t BOARD_BOOT_PIN = 0;

// GPIO 11, 12 e 13 ficam LIVRES nesta revisão (além do 3, que é strapping e
// é melhor não usar). Qualquer expansão futura entra por aí, pelo canal C15
// livre do mux, ou por um segundo MCP23017 no mesmo I2C — este último não
// custa pino nenhum, e é o caminho preferido enquanto a placa for a
// SuperMini.
