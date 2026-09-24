// Driver do 74HC4067 (mux digital de 16 canais), alimentado a 3.3V.
//
// Diferente do MCP23017 (I2C, register-based), este chip é só um
// multiplexador analógico/digital passivo: a ESP32 escolhe o canal via
// S0-S3 e lê o resultado em SIG. Não existe barramento de endereço nem
// registrador — o "protocolo" é inteiramente feito de GPIO.
//
// Convenção elétrica (igual ao MCP23017, ver docs/INPUTS_PINOUT.md secao 3):
// cada canal tem pull-up (interno na ESP32 para o SIG, e/ou externo no
// próprio canal, conforme a fiação) -> switch aberto = HIGH, switch
// fechado para GND = LOW.
//
// Este módulo é autocontido: além do driver de baixo nível
// (mux4067_select/mux4067_read), ele mesmo faz o debounce por tempo — não
// existe uma camada de abstração separada aqui como input_expander, porque
// não foi pedida; se for preciso mais pra frente, dá pra empacotar
// mux4067_get_state() atrás de uma interface maior sem reescrever nada
// disto.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Pinos default do DRIVER ISOLADO (para reuso fora deste projeto).
//
// ATENÇÃO: neste projeto estes números NÃO valem. Desde a revisão 2 do
// pinout, 15/16/17/18/21 são pads da face inferior da ESP32-S3 SuperMini e
// foram abandonados — os valores reais estão em include/board_config.h
// (GPIO4-7 e 10) e quem chama sempre os passa explicitamente.
static const uint8_t MUX4067_DEFAULT_S0_PIN  = 15;
static const uint8_t MUX4067_DEFAULT_S1_PIN  = 16;
static const uint8_t MUX4067_DEFAULT_S2_PIN  = 17;
static const uint8_t MUX4067_DEFAULT_S3_PIN  = 18;
static const uint8_t MUX4067_DEFAULT_SIG_PIN = 21;

// Numero maximo de canais fisicos do chip (C0-C15).
static const uint8_t MUX4067_MAX_CHANNELS = 16;

// Tempo de assentamento apos trocar de canal (S0-S3), antes que o SIG
// possa ser lido com confianca. Cobre o atraso de propagacao do 74HC4067
// (poucos ns, por datasheet) mais a carga/descarga RC da capacitancia
// parasita do barramento SIG através do pull-up — na pratica, alguns
// microssegundos. 30us e uma margem confortavel sem ser um bloqueio longo.
static const uint32_t MUX4067_SETTLE_US = 30;

// Janela de debounce por canal, por tempo (nao por contagem de amostras).
// Mesma ordem de grandeza usada no input_expander (MCP23017) para switches
// mecanicos equivalentes.
static const uint32_t MUX4067_DEBOUNCE_MS = 15;

// Configura S0-S3 como saida e SIG como entrada com pull-up interno da
// ESP32. Como o mux liga o canal selecionado ao SIG, esse unico pull-up
// serve todos os canais: chave no GND = LOW, aberta = HIGH, sem resistor
// por entrada. Um resistor externo unico (SIG -> 3V3) so e' opcional, se
// a leitura ficar instavel — ver docs/INPUTS_PINOUT.md secao 11.
//
// channelCount: quantos dos 16 canais (C0..C{channelCount-1}) devem ser
// varridos por mux4067_scan(). NÃO é preciso usar os 16 — o mapa aprovado
// usa só C0-C8; o restante (channelCount < 16) evita gastar tempo de scan
// em canais que nem existem fisicamente ainda. Valores fora de 1-16 são
// grampeados (clamped) para esse intervalo.
void mux4067_init(uint8_t s0Pin  = MUX4067_DEFAULT_S0_PIN,
                   uint8_t s1Pin  = MUX4067_DEFAULT_S1_PIN,
                   uint8_t s2Pin  = MUX4067_DEFAULT_S2_PIN,
                   uint8_t s3Pin  = MUX4067_DEFAULT_S3_PIN,
                   uint8_t sigPin = MUX4067_DEFAULT_SIG_PIN,
                   uint8_t channelCount = MUX4067_MAX_CHANNELS);

// Programa S0-S3 para endereçar 'channel' (0-15, valores fora da faixa são
// mascarados com & 0x0F). Não lê nada, não espera assentar — é o primitivo
// de baixo nível; mux4067_scan() já cuida da espera de assentamento antes
// de ler. Exposta separadamente porque foi pedida explicitamente.
void mux4067_select(uint8_t channel);

// Lê o pino SIG NESTE INSTANTE, sem trocar canal nem esperar assentamento.
// Convenção: aberto=1 (HIGH), fechado para GND=0 (LOW). Primitivo de baixo
// nível — quem chama é responsável por garantir que o canal já estava
// selecionado há tempo suficiente (ver MUX4067_SETTLE_US). Use
// mux4067_scan() para leitura completa e segura, canal por canal.
bool mux4067_read();

// Avança a máquina de varredura não-bloqueante: um canal por vez, com
// espera de assentamento medida por micros() (não por delay()) e debounce
// por tempo medido por millis(). Chamar com a maior frequência possível a
// partir do loop() — cada chamada custa, na pior das hipóteses, uma
// leitura de GPIO; nunca dorme/bloqueia. Um ciclo completo de N canais
// demora aproximadamente N * MUX4067_SETTLE_US (ex.: 9 canais ~270us),
// dividido ao longo de várias chamadas de scan().
void mux4067_scan();

// Estado atual JÁ DEBOUNCED de todos os canais varridos (ver
// channelCount em mux4067_init()). bit=1 -> pressionado/fechado
// (convenção invertida em relação a mux4067_read(), igual ao
// input_expander do MCP23017, para manter consistência entre os módulos).
// Não faz leitura nova, não bloqueia — só devolve o cache.
uint16_t mux4067_get_state();

// Conveniência: estado debounced de um único canal (0-15). Fora da faixa
// varrida (>= channelCount) ou fora de 0-15 devolve false.
bool mux4067_get_channel_state(uint8_t channel);
