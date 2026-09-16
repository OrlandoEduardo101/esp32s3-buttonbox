// Driver de baixo nível do MCP23017 (expansor de I/O via I2C).
//
// Escopo desta camada: só fala com o chip. Não sabe o que cada pino
// significa (isso é responsabilidade de input_expander.h), não faz
// debounce, não gera eventos.
//
// Convenção elétrica (fixa por hardware, ver docs/INPUTS_PINOUT.md):
//   - todas as entradas usam pull-up interno do MCP23017;
//   - switch aberto  -> pino em HIGH (1);
//   - switch fechado para GND -> pino em LOW (0).
// As funções de leitura devolvem exatamente essa convenção, sem inverter
// nada — a inversão para "pressionado = 1", se for útil, é feita na camada
// de cima (input_expander).
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Endereço default do MCP23017 com A0/A1/A2 amarrados no GND.
static const uint8_t MCP23017_DEFAULT_I2C_ADDR = 0x20;

// Pinos I2C default do ESP32-S3 (core Arduino-ESP32, variants/esp32s3/
// pins_arduino.h: SDA=8, SCL=9) e usados no pinout definido em
// docs/INPUTS_PINOUT.md.
static const uint8_t MCP23017_DEFAULT_SDA_PIN = 8;
static const uint8_t MCP23017_DEFAULT_SCL_PIN = 9;

// Inicializa o barramento I2C e configura o MCP23017:
//   - IODIRA/IODIRB = entrada em todos os 16 pinos;
//   - GPPUA/GPPUB   = pull-up interno ligado em todos os 16 pinos;
//   - IPOLA/IPOLB   = sem inversão de polaridade (mantém a convenção
//                     aberto=HIGH / fechado=LOW);
//   - IOCON         = SEQOP habilitado (auto-incremento de endereço, usado
//                     por mcp23017_read() para ler GPIOA+GPIOB numa única
//                     transação I2C) e MIRROR ligado (INTA/INTB espelhados,
//                     preparado para uso futuro do pino INT — não usado
//                     por este driver hoje, que opera só por polling).
//
// Retorna false se o chip não responder no barramento (endereço errado,
// não alimentado, ou não fiado ainda) — quem chama deve tratar esse caso
// sem travar o resto do firmware.
bool mcp23017_init(uint8_t i2c_addr = MCP23017_DEFAULT_I2C_ADDR,
                    uint8_t sda_pin = MCP23017_DEFAULT_SDA_PIN,
                    uint8_t scl_pin = MCP23017_DEFAULT_SCL_PIN);

// Lê GPIOA e GPIOB numa única transação I2C (aproveita o auto-incremento
// de endereço configurado em mcp23017_init).
// Retorno: bits 0-7 = GPIOA, bits 8-15 = GPIOB. Convenção aberto=1/fechado=0.
// Em falha de comunicação I2C, devolve 0xFFFF (todos abertos) como valor
// seguro — uma falha no barramento nunca deve ser lida como "todo mundo
// pressionado".
uint16_t mcp23017_read();

// Lê só o banco A (GPA0-GPA7). Bit 0 = GPA0 ... bit 7 = GPA7.
// Falha de I2C -> 0xFF (todos abertos).
uint8_t mcp23017_read_gpioa();

// Lê só o banco B (GPB0-GPB7). Bit 0 = GPB0 ... bit 7 = GPB7.
// Falha de I2C -> 0xFF (todos abertos).
uint8_t mcp23017_read_gpiob();
