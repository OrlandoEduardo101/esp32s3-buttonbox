// Camada de abstração de alto nível sobre o MCP23017.
//
// Ninguém fora deste arquivo deve tocar em registrador de MCP23017
// diretamente (nem o código de HID, nem nenhum outro). Todo consumidor
// (HID, futura lógica de jogo, testes) fala só com esta interface.
//
// Convenção de bits devolvida por input_expander_get_state():
//   bit = 1  -> entrada PRESSIONADA / switch FECHADO
//   bit = 0  -> entrada SOLTA / switch ABERTO
// (Invertida em relação ao registrador cru do MCP23017, que é
// aberto=HIGH/fechado=LOW por causa do pull-up — a inversão para uma
// convenção "1 = pressionado" fica escondida aqui, então quem consome
// esta camada não precisa saber de polaridade elétrica.)
//
// Mapeamento bit -> pino físico (docs/INPUTS_PINOUT.md, seção 2):
//   bit 0-7  = GPA0-GPA7
//   bit 8-15 = GPB0-GPB7
//
// Debounce: feito aqui, por tempo (millis()), sem nenhum delay()
// bloqueante. input_expander_update() deve ser chamada com frequência a
// partir do loop() principal; input_expander_get_state() só lê o cache já
// debounced, não faz I2C nem bloqueia.
//
// Eventos BUTTON_PRESSED / BUTTON_RELEASED: propositalmente NÃO
// implementados ainda (pedido explícito de escopo). A estrutura de estado
// debounced já guardada aqui é o que vai alimentar esses eventos quando
// forem adicionados, sem precisar refazer a camada de debounce.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "mcp23017.h" // MCP23017_DEFAULT_* — usados só se init() for chamado sem argumentos

// Tempo mínimo (ms) que uma transição precisa se manter estável antes de
// ser aceita como novo estado. 15 ms cobre bounce típico de botão/switch
// mecânico sem atrasar perceptivelmente a resposta.
static const uint32_t INPUT_EXPANDER_DEBOUNCE_MS = 15;

// Inicializa o MCP23017 (I2C + direção + pull-up, ver mcp23017_init()).
// Os 3 parâmetros default são os do MCP23017 isolado (para uso fora deste
// projeto); dentro deste projeto, lib/inputs sempre passa os valores de
// include/board_config.h. Retorna false se o chip não responder no
// barramento.
bool input_expander_init(uint8_t i2c_addr = MCP23017_DEFAULT_I2C_ADDR,
                          uint8_t sda_pin  = MCP23017_DEFAULT_SDA_PIN,
                          uint8_t scl_pin  = MCP23017_DEFAULT_SCL_PIN);

// Faz uma leitura do MCP23017 e avança a máquina de debounce. Não bloqueia
// (não usa delay(); uma leitura I2C tem latência da ordem de dezenas de
// microssegundos a ~1 ms a 400 kHz, desprezível para o loop principal).
// Chamar a cada iteração do loop() de quem usar esta camada.
void input_expander_update();

// Estado atual JÁ DEBOUNCED de todas as 16 entradas. bit=1 -> pressionado.
// Não faz I2C, não bloqueia — é só leitura de uma variável em RAM.
uint16_t input_expander_get_state();

// Conveniência: estado de uma única entrada (0-15), já debounced.
// index fora de 0-15 devolve false.
bool input_expander_get_bit(uint8_t index);

// true se o MCP23017 respondeu no barramento durante o init(). Útil para
// os testes individuais reportarem "chip não encontrado" em vez de um
// estado de leitura enganoso.
bool input_expander_is_connected();
