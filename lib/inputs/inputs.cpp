#include "inputs.h"
#include <Arduino.h>
#include "input_expander.h" // MCP23017 (lib/input_expander) — nao alterado
#include "mux4067.h"         // 74HC4067 (lib/mux4067) — nao alterado
#include "encoders.h"        // KY-040 (lib/encoders), usado em MODO EXTERNO
#include "board_config.h"    // mapa unico de pinos/canais deste projeto (include/)

namespace {

// --- Mapeamento fisico -----------------------------------------------
// Este é o ÚNICO lugar do firmware que sabe de onde cada InputId vem de
// verdade. Qualquer coisa fora deste arquivo (inclusive o futuro código
// HID) só enxerga InputId.
enum class SourceKind : uint8_t { Gpio, Mcp23017, Mux4067, EncoderCw, EncoderCcw };

struct InputBinding {
  SourceKind kind;
  uint8_t index; // significado depende de 'kind':
                 //   Gpio      -> numero do pino
                 //   Mcp23017  -> bit 0-15 (ver input_expander_get_bit)
                 //   Mux4067   -> canal 0-8 (ver mux4067_get_channel_state)
                 //   EncoderCw/EncoderCcw -> indice do encoder 0-3
};

// Ordem TEM que bater exatamente com o enum InputId em inputs.h — o
// static_assert logo abaixo garante isso em tempo de compilacao.
constexpr InputBinding BINDINGS[INPUT_ID_COUNT] = {
  // INPUT_BUTTON_01-11 -> 74HC4067 C0-C10
  {SourceKind::Mux4067, 0}, {SourceKind::Mux4067, 1}, {SourceKind::Mux4067, 2},
  {SourceKind::Mux4067, 3}, {SourceKind::Mux4067, 4}, {SourceKind::Mux4067, 5},
  {SourceKind::Mux4067, 6}, {SourceKind::Mux4067, 7}, {SourceKind::Mux4067, 8},
  {SourceKind::Mux4067, 9}, {SourceKind::Mux4067, 10},
  // INPUT_BUTTON_12-15 (SW dos 4 encoders) -> MCP23017 GPB0-GPB3 (bits 8-11).
  // Ficam ao lado dos proprios encoders no mesmo conector do KY-040 — é o
  // unico motivo de estarem no MCP e nao no mux: encurta a fiacao.
  {SourceKind::Mcp23017, 8},  {SourceKind::Mcp23017, 9},
  {SourceKind::Mcp23017, 10}, {SourceKind::Mcp23017, 11},
  // INPUT_IGNITION_ON/IGN -> 74HC4067 C12/C13
  {SourceKind::Mux4067, 12}, {SourceKind::Mux4067, 13},
  // INPUT_START_ENGINE -> 74HC4067 C11
  {SourceKind::Mux4067, 11},
  // INPUT_HANDBRAKE -> 74HC4067 C14
  {SourceKind::Mux4067, 14},
  // INPUT_KILL_SWITCH_01-04 -> MCP23017 GPB4-GPB7 (bits 12-15)
  {SourceKind::Mcp23017, 12}, {SourceKind::Mcp23017, 13},
  {SourceKind::Mcp23017, 14}, {SourceKind::Mcp23017, 15},
  // INPUT_ENCODER_01..04 _CW/_CCW -> encoders 0-3 (lib/encoders, modo
  // externo: CLK/DT vem do banco A do MCP23017, amostrado pela task abaixo)
  {SourceKind::EncoderCw, 0}, {SourceKind::EncoderCcw, 0},
  {SourceKind::EncoderCw, 1}, {SourceKind::EncoderCcw, 1},
  {SourceKind::EncoderCw, 2}, {SourceKind::EncoderCcw, 2},
  {SourceKind::EncoderCw, 3}, {SourceKind::EncoderCcw, 3},
};
static_assert(sizeof(BINDINGS) / sizeof(BINDINGS[0]) == INPUT_ID_COUNT,
              "BINDINGS tem que ter exatamente um item por InputId");

constexpr const char *NAMES[INPUT_ID_COUNT] = {
  "INPUT_BUTTON_01", "INPUT_BUTTON_02", "INPUT_BUTTON_03", "INPUT_BUTTON_04",
  "INPUT_BUTTON_05", "INPUT_BUTTON_06", "INPUT_BUTTON_07", "INPUT_BUTTON_08",
  "INPUT_BUTTON_09", "INPUT_BUTTON_10", "INPUT_BUTTON_11",
  "INPUT_BUTTON_12_ENC01_SW", "INPUT_BUTTON_13_ENC02_SW",
  "INPUT_BUTTON_14_ENC03_SW", "INPUT_BUTTON_15_ENC04_SW",
  "INPUT_IGNITION_ON", "INPUT_IGNITION_IGN", "INPUT_START_ENGINE",
  "INPUT_HANDBRAKE",
  "INPUT_KILL_SWITCH_01", "INPUT_KILL_SWITCH_02",
  "INPUT_KILL_SWITCH_03", "INPUT_KILL_SWITCH_04",
  "INPUT_ENCODER_01_CW", "INPUT_ENCODER_01_CCW",
  "INPUT_ENCODER_02_CW", "INPUT_ENCODER_02_CCW",
  "INPUT_ENCODER_03_CW", "INPUT_ENCODER_03_CCW",
  "INPUT_ENCODER_04_CW", "INPUT_ENCODER_04_CCW",
};
static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == INPUT_ID_COUNT,
              "NAMES tem que ter exatamente um item por InputId");

// --- Estado de runtime por ID ------------------------------------------
constexpr uint8_t EVENT_QUEUE_DEPTH = 4;

struct InputRuntime {
  bool lastLevel; // só significativo para id < INPUT_LEVEL_ID_COUNT
  InputEventType queue[EVENT_QUEUE_DEPTH];
  uint8_t queueHead;
  uint8_t queueCount;
};

InputRuntime g_runtime[INPUT_ID_COUNT];

void pushEvent(uint8_t id, InputEventType ev) {
  InputRuntime &rt = g_runtime[id];
  if (rt.queueCount >= EVENT_QUEUE_DEPTH) {
    return; // fila cheia — praticamente inatingível com update() chamada
             // a cada iteracao do loop; descarta em vez de travar/crescer.
  }
  const uint8_t tail = (uint8_t)((rt.queueHead + rt.queueCount) % EVENT_QUEUE_DEPTH);
  rt.queue[tail] = ev;
  rt.queueCount++;
}

// --- Task de amostragem do MCP23017 -----------------------------------
//
// POR QUE EXISTE: depois da revisao 2 do pinout (ver include/board_config.h)
// os 8 sinais de quadratura dos encoders vem do banco A do MCP23017, por
// I2C. I2C nao tem interrupcao por borda — quem descobre as transicoes e'
// a amostragem. E o loop() principal termina com delay(5), ou seja,
// amostraria a 200 Hz: lento demais, perderia detent (a conta completa esta
// no comentario de BOARD_MCP_SAMPLE_PERIOD_MS).
//
// Por isso a leitura do MCP23017 saiu de inputs_update() e virou esta task
// dedicada, com periodo proprio, independente do que o loop() esteja
// fazendo (render de LED, WiFi, OTA, serial).
//
// PROPRIEDADE DO BARRAMENTO: depois que esta task sobe, ela e' a UNICA a
// chamar input_expander_update() — nada mais neste projeto toca I2C, entao
// nao ha concorrencia no Wire. inputs_update(), no loop, so LE o cache ja
// pronto (input_expander_get_bit), que nao faz I2C.
//
// CORRIDA DE LEITURA: a task escreve o estado debounced (uint16_t) enquanto
// o loop pode le-lo. Em Xtensa uma palavra alinhada e' lida/escrita
// atomicamente, entao o pior caso e' o loop ver o valor de 1 ms atras —
// nunca um valor "meio escrito". Nao precisa de mutex.
TaskHandle_t g_sampleTask = nullptr;

void mcpSampleTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    // Uma unica transacao I2C traz GPIOA+GPIOB; serve tanto pros botoes
    // (debounce interno da camada) quanto pra quadratura (cru, logo abaixo).
    input_expander_update();

    const uint16_t raw = input_expander_get_raw(); // 1 = pino HIGH
    for (uint8_t i = 0; i < BOARD_ENCODER_COUNT; i++) {
      encoder_feed(i,
                   (uint8_t)((raw >> BOARD_ENCODER_MCP_CLK_BIT[i]) & 0x1),
                   (uint8_t)((raw >> BOARD_ENCODER_MCP_DT_BIT[i]) & 0x1));
    }

    // vTaskDelayUntil (nao vTaskDelay): mantem o PERIODO fixo mesmo quando
    // a leitura I2C demora mais num ciclo — sem acumular atraso.
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BOARD_MCP_SAMPLE_PERIOD_MS));
  }
}

// Lê o nível atual (já debounced pela camada de baixo) de uma entrada de
// nível. Não debounça de novo — só encaminha o que MCP23017/74HC4067 já
// resolveram.
bool readLevel(const InputBinding &b) {
  switch (b.kind) {
    case SourceKind::Mcp23017:
      return input_expander_get_bit(b.index);
    case SourceKind::Mux4067:
      return mux4067_get_channel_state(b.index);
    case SourceKind::Gpio:
      // Nao usado no mapa atual (nenhum InputId de nivel vem de GPIO
      // direto hoje) — suporte deixado pronto para expansao futura.
      // Convencao igual as demais: fechado para GND = pressionado.
      return digitalRead(b.index) == LOW;
    default:
      return false; // EncoderCw/EncoderCcw nunca chegam aqui (sem STATE)
  }
}

} // namespace

void inputs_init() {
  // Todo pino/canal/endereço vem de include/board_config.h — é o único
  // lugar que muda se a placa ou o pinout mudar.
  input_expander_init(BOARD_MCP23017_ADDR, BOARD_I2C_SDA_PIN, BOARD_I2C_SCL_PIN);
  mux4067_init(BOARD_MUX_S0_PIN, BOARD_MUX_S1_PIN, BOARD_MUX_S2_PIN,
               BOARD_MUX_S3_PIN, BOARD_MUX_SIG_PIN, BOARD_MUX_CHANNEL_COUNT);
  // Modo externo: o driver nao toca em pino nem registra interrupcao; as
  // amostras de CLK/DT chegam por encoder_feed(), da task abaixo.
  encoder_init_external();

  for (uint8_t id = 0; id < INPUT_ID_COUNT; id++) {
    InputRuntime &rt = g_runtime[id];
    rt.queueHead = 0;
    rt.queueCount = 0;
    rt.lastLevel = (id < INPUT_LEVEL_ID_COUNT) ? readLevel(BINDINGS[id]) : false;
  }

  // Task de amostragem so depois do estado inicial pronto, pra ela nao
  // competir com a leitura de baseline acima. Fixada no core 0 (o loop()
  // do Arduino roda no core 1), prioridade acima do loop pra que o periodo
  // de 1 ms seja respeitado mesmo com o loop ocupado.
  if (g_sampleTask == nullptr) {
    xTaskCreatePinnedToCore(mcpSampleTask, "mcp_sample", 3072, nullptr,
                             /*priority=*/3, &g_sampleTask, /*core=*/0);
  }
}

void inputs_update() {
  // Avanca as camadas de hardware por baixo — nenhuma bloqueia.
  //
  // input_expander_update() NAO e' chamada aqui de proposito: a leitura do
  // MCP23017 pertence a mcpSampleTask (ver comentario dela). Chamar tambem
  // daqui colocaria duas tasks no mesmo Wire.
  mux4067_scan();
  encoder_update();

  // Entradas de nivel: so detecta borda (o debounce ja foi feito por
  // input_expander/mux4067). Isso é o "aplicar debounce somente onde
  // necessário" — aqui não é debounce, é so deteccao de mudanca.
  for (uint8_t id = 0; id < INPUT_LEVEL_ID_COUNT; id++) {
    InputRuntime &rt = g_runtime[id];
    const bool level = readLevel(BINDINGS[id]);
    if (level != rt.lastLevel) {
      rt.lastLevel = level;
      pushEvent(id, level ? INPUT_EVENT_PRESSED : INPUT_EVENT_RELEASED);
    }
  }

  // Encoders: so encaminha o que a maquina de quadratura ja decidiu (ela
  // mesma ja tratou bounce/ruido/reversao — ver lib/encoders).
  for (uint8_t enc = 0; enc < ENCODER_COUNT; enc++) {
    const uint8_t cwId  = (uint8_t)(INPUT_ENCODER_01_CW  + enc * 2);
    const uint8_t ccwId = (uint8_t)(INPUT_ENCODER_01_CCW + enc * 2);

    EncoderEvent ev;
    while ((ev = encoder_get_event(enc)) != ENCODER_NONE) {
      if (ev == ENCODER_CW)  pushEvent(cwId,  INPUT_EVENT_CW);
      else                    pushEvent(ccwId, INPUT_EVENT_CCW);
    }
  }
}

bool inputs_get_state(InputId id) {
  if (id >= INPUT_LEVEL_ID_COUNT) return false; // inclui id invalido e IDs de encoder
  return g_runtime[id].lastLevel;
}

InputEventType inputs_get_event(InputId id) {
  if (id >= INPUT_ID_COUNT) return INPUT_EVENT_NONE;
  InputRuntime &rt = g_runtime[id];
  if (rt.queueCount == 0) return INPUT_EVENT_NONE;

  const InputEventType ev = rt.queue[rt.queueHead];
  rt.queueHead = (uint8_t)((rt.queueHead + 1) % EVENT_QUEUE_DEPTH);
  rt.queueCount--;
  return ev;
}

const char *inputs_get_name(InputId id) {
  if (id >= INPUT_ID_COUNT) return "INPUT_INVALID";
  return NAMES[id];
}
