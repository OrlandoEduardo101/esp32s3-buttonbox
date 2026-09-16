#include "inputs.h"
#include <Arduino.h>
#include "input_expander.h" // MCP23017 (lib/input_expander) — nao alterado
#include "mux4067.h"         // 74HC4067 (lib/mux4067) — nao alterado
#include "encoders.h"        // KY-040 (lib/encoders) — nao alterado
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
  // INPUT_BUTTON_01-08 -> MCP23017 GPA0-GPA7 (bits 0-7)
  {SourceKind::Mcp23017, 0}, {SourceKind::Mcp23017, 1}, {SourceKind::Mcp23017, 2},
  {SourceKind::Mcp23017, 3}, {SourceKind::Mcp23017, 4}, {SourceKind::Mcp23017, 5},
  {SourceKind::Mcp23017, 6}, {SourceKind::Mcp23017, 7},
  // INPUT_BUTTON_09-11 -> MCP23017 GPB0-GPB2 (bits 8-10)
  {SourceKind::Mcp23017, 8}, {SourceKind::Mcp23017, 9}, {SourceKind::Mcp23017, 10},
  // INPUT_BUTTON_12-15 -> 74HC4067 C0-C3 (SW dos 4 encoders)
  {SourceKind::Mux4067, 0}, {SourceKind::Mux4067, 1},
  {SourceKind::Mux4067, 2}, {SourceKind::Mux4067, 3},
  // INPUT_IGNITION_ON/IGN -> MCP23017 GPB4/GPB5 (bits 12/13)
  {SourceKind::Mcp23017, 12}, {SourceKind::Mcp23017, 13},
  // INPUT_START_ENGINE -> MCP23017 GPB3 (bit 11)
  {SourceKind::Mcp23017, 11},
  // INPUT_HANDBRAKE -> 74HC4067 C8
  {SourceKind::Mux4067, 8},
  // INPUT_KILL_SWITCH_01-04 -> 74HC4067 C4-C7
  {SourceKind::Mux4067, 4}, {SourceKind::Mux4067, 5},
  {SourceKind::Mux4067, 6}, {SourceKind::Mux4067, 7},
  // INPUT_ENCODER_01..04 _CW/_CCW -> encoders 0-3 (lib/encoders)
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
  encoder_init(BOARD_ENCODER_CLK_PIN, BOARD_ENCODER_DT_PIN);

  for (uint8_t id = 0; id < INPUT_ID_COUNT; id++) {
    InputRuntime &rt = g_runtime[id];
    rt.queueHead = 0;
    rt.queueCount = 0;
    rt.lastLevel = (id < INPUT_LEVEL_ID_COUNT) ? readLevel(BINDINGS[id]) : false;
  }
}

void inputs_update() {
  // Avanca as 3 camadas de hardware por baixo — nenhuma bloqueia.
  input_expander_update();
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
