// Camada unificada de INPUTS — a única API que qualquer consumidor (HID
// incluído, quando for integrado) deve usar para ler os controles físicos
// da Button Box.
//
// Esconde completamente de quem a usa se uma entrada vem de GPIO direto da
// ESP32-S3, do MCP23017 (I2C), do 74HC4067 (mux) ou de um KY-040
// (quadratura). Quem chama só conhece INPUT_* — nunca um número de GPIO,
// endereço I2C ou canal de mux. O único lugar do firmware que sabe o
// mapeamento físico é a tabela interna de inputs.cpp (BINDINGS), espelho
// do mapa aprovado em docs/INPUTS_PINOUT.md.
//
// STATE vs EVENT:
//   STATE  = a entrada está pressionada/fechada NESTE INSTANTE
//            (inputs_get_state) — só existe pra entradas de nível (botões,
//            switches, ignição). Encoders não têm STATE: uma rotação não
//            fica "segurada".
//   EVENT  = a entrada AGORA MESMO mudou (inputs_get_event) — PRESSED/
//            RELEASED pra entradas de nível, CW/CCW pra direção de
//            encoder. Fila por ID (profundidade 4): chame em loop
//            (`while (...) != INPUT_EVENT_NONE`) se quiser garantir que
//            nenhuma mudança rápida seja perdida.
//
// Debounce: só onde ainda não foi feito. O MCP23017 (via input_expander) e
// o 74HC4067 (via mux4067) já debouncam por tempo na camada de baixo — este
// módulo NÃO debounça de novo, só faz detecção de borda (compara o nível
// atual, já limpo, com o nível anterior) pra gerar PRESSED/RELEASED. Os
// encoders já resolvem ruído/bounce na própria máquina de estados de
// quadratura (lib/encoders) — aqui só encaminhamos o evento CW/CCW que eles
// já produziram, prontos.
#pragma once
#include <stdint.h>

enum InputId : uint8_t {
  // --- Botões genéricos (têm STATE e EVENT) ---------------------------
  // 11 push buttons físicos, no MCP23017 (docs/INPUTS_PINOUT.md secao 2).
  INPUT_BUTTON_01 = 0,
  INPUT_BUTTON_02,
  INPUT_BUTTON_03,
  INPUT_BUTTON_04,
  INPUT_BUTTON_05,
  INPUT_BUTTON_06,
  INPUT_BUTTON_07,
  INPUT_BUTTON_08,
  INPUT_BUTTON_09,
  INPUT_BUTTON_10,
  INPUT_BUTTON_11,
  // SW dos 4 encoders (clique do próprio encoder), no 74HC4067 — tratados
  // como botão normal mapeável, mesma convenção dos 11 acima.
  INPUT_BUTTON_12, // SW do encoder 1
  INPUT_BUTTON_13, // SW do encoder 2
  INPUT_BUTTON_14, // SW do encoder 3
  INPUT_BUTTON_15, // SW do encoder 4

  // Ignição (chave de scooter 3 posições, ver docs/INPUTS_PINOUT.md secao
  // 2) e botão de partida — todos no MCP23017.
  INPUT_IGNITION_ON,
  INPUT_IGNITION_IGN,
  INPUT_START_ENGINE,

  // Freio de estacionamento (microswitch), no 74HC4067. STATE aqui reflete
  // a posição física da alavanca (pressed=fechado); a tradução para pulso
  // de toggle exigida pelo Euro Truck Simulator (docs/INPUTS_PINOUT.md
  // secao 7) é responsabilidade de quem consumir este evento, não desta
  // camada — aqui só existe o fato físico, sem lógica de jogo nenhuma.
  INPUT_HANDBRAKE,

  // 4 chaves tipo caça ON/OFF ("kill switches"), no 74HC4067.
  INPUT_KILL_SWITCH_01,
  INPUT_KILL_SWITCH_02,
  INPUT_KILL_SWITCH_03,
  INPUT_KILL_SWITCH_04,

  // Marcador: tudo ANTES daqui tem STATE (nível) + EVENT (PRESSED/
  // RELEASED). Tudo DEPOIS só tem EVENT (CW/CCW), sem STATE.
  INPUT_LEVEL_ID_COUNT,

  // --- Encoders (só têm EVENT — CW/CCW, sem STATE) ---------------------
  // Cada KY-040 vira 2 IDs, um por sentido (docs/INPUTS_PINOUT.md secao
  // 8: modo "virtual +/- fixo", decidido com o usuário). CLK/DT direto na
  // ESP32-S3; o SW de cada um já está em INPUT_BUTTON_12-15 acima.
  INPUT_ENCODER_01_CW = INPUT_LEVEL_ID_COUNT,
  INPUT_ENCODER_01_CCW,
  INPUT_ENCODER_02_CW,
  INPUT_ENCODER_02_CCW,
  INPUT_ENCODER_03_CW,
  INPUT_ENCODER_03_CCW,
  INPUT_ENCODER_04_CW,
  INPUT_ENCODER_04_CCW,

  INPUT_ID_COUNT,
};

enum InputEventType : uint8_t {
  INPUT_EVENT_NONE = 0,
  INPUT_EVENT_PRESSED,   // entradas de nível (id < INPUT_LEVEL_ID_COUNT)
  INPUT_EVENT_RELEASED,  // idem
  INPUT_EVENT_CW,        // só em INPUT_ENCODER_xx_CW
  INPUT_EVENT_CCW,       // só em INPUT_ENCODER_xx_CCW
};

// Inicializa as 3 camadas de hardware por baixo (MCP23017, 74HC4067,
// encoders) com os parâmetros do mapa aprovado — quem chama esta função
// não precisa saber quantos canais o mux usa, nem endereço I2C, nem pinos.
void inputs_init();

// Atualiza tudo: faz o polling do MCP23017 e do 74HC4067, drena os
// encoders, e gera os eventos PRESSED/RELEASED/CW/CCW por transição.
// Não bloqueia (delega para input_expander_update()/mux4067_scan()/
// encoder_update(), todas não-bloqueantes). Chamar com a maior frequência
// possível a partir do loop() de quem consumir esta API.
void inputs_update();

// Estado atual (nível) de uma entrada. Só é significativo para
// id < INPUT_LEVEL_ID_COUNT — para IDs de encoder (CW/CCW) sempre devolve
// false (não existe "segurar" uma rotação). id fora de 0..INPUT_ID_COUNT-1
// devolve false.
bool inputs_get_state(InputId id);

// Consome um evento pendente da fila do id (profundidade 4). Devolve
// INPUT_EVENT_NONE quando não há mais nada pendente. Chame em loop para
// esvaziar tudo que se acumulou desde a última leitura.
InputEventType inputs_get_event(InputId id);

// Nome legível do ID, só para logs/depuração (ex.: firmware de teste).
// Nunca usado para decidir hardware — é só texto.
const char *inputs_get_name(InputId id);
