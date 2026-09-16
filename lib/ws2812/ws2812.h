// Driver de saída para uma fita/matriz WS2812/WS2812B endereçável (a
// matriz 8x8 + a fita ~10 LEDs do usuário, encadeadas numa única cadeia
// serial), via periférico RMT da ESP32-S3 — sem bit-banging por software,
// sem delay() bloqueante.
//
// Regra de arquitetura (item 9 da integração final): este driver NÃO
// conhece nada de telemetria/SimHub/RPM/flags — só desenha o RGB que
// mandarem desenhar. Por isso define seu próprio tipo de cor
// (Ws2812Color) em vez de incluir simhub.h — quem faz a ponte entre "o
// framebuffer do SimHub" e "o que aparece na fita" é o código de
// integração (src/main.cpp), nunca este módulo nem o lib/simhub.
//
// Timing de bit idêntico ao já usado (e já validado pela Espressif) em
// neopixelWrite() (cores/esp32/esp32-hal-rgb-led.c, parte do core
// Arduino-ESP32 instalado) — reaproveitado aqui generalizado para N
// pixels em vez de 1, em vez de reinventar os valores de tick.
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct Ws2812Color {
  uint8_t r, g, b;
};

// GPIO da linha de dados — decisão desta integração (não fazia parte do
// pinout de INPUTS, que só cobria entradas). GPIO1 está na faixa 0-21,
// universal em qualquer variante de ESP32-S3, e estava marcado como livre
// em docs/INPUTS_PINOUT.md seção 5.
static const uint8_t WS2812_DEFAULT_PIN = 1;

// Teto de LEDs suportado pelo driver — buffers de tamanho FIXO, sem
// alocação dinâmica por frame. Mesmo valor de SIMHUB_LED_COUNT_MAX
// (lib/simhub) por consistência, não por acoplamento entre os dois.
static const uint16_t WS2812_MAX_LEDS = 256;

// Inicializa o canal RMT no pino indicado. Retorna false se não houver
// canal RMT livre (ex.: todos os 8 canais da ESP32-S3 já em uso por outra
// coisa — não é o caso aqui, mas o retorno existe pra não mascarar isso).
bool ws2812_init(uint8_t pin = WS2812_DEFAULT_PIN);

// Manda um frame inteiro pra fita. count > WS2812_MAX_LEDS é truncado pro
// máximo (os LEDs além do teto simplesmente não são atualizados).
// NÃO BLOQUEIA: usa rmtWrite() (assíncrono) — a transmissão real acontece
// via hardware RMT em segundo plano depois que esta função retorna. O
// tempo de FIO para N LEDs é ~N*30us (74 LEDs ~2,2ms), mas isso é tempo de
// hardware rodando em paralelo, não CPU bloqueada. Chamar de novo antes
// da transmissão anterior terminar corta a anterior no meio (glitch
// visual momentâneo, sem risco de travar ou corromper estado) — na
// prática nunca acontece aqui, já que o chamador nunca chama isto mais
// rápido que a cada alguns ms.
void ws2812_show(const Ws2812Color *colors, uint16_t count);
