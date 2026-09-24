// Animação de espera (idle) para a matriz 8x8 + fita de LEDs, usada só
// quando o SimHub NÃO está conectado (ex.: logo depois de ligar, ou com o
// SimHub fechado). Assim que o SimHub volta a falar com a placa, quem manda
// nas cores é ele — esta animação não faz parte de nenhum efeito de jogo.
//
// Regra de arquitetura: este módulo não conhece SimHub, telemetria, GPIO
// nem o driver WS2812. Só recebe arrays de cor e os preenche. Quem decide
// QUANDO usá-lo (SimHub desconectado) e como levar as cores até a fita é
// o código de integração em src/main.cpp.
//
// Código puro (só <stdint.h>, sem Arduino.h): dá pra compilar e testar no PC.
#pragma once
#include <stdint.h>

struct LedIdleColor {
  uint8_t r, g, b;
};

// Teto de brilho (0-255) proprio da animacao, ja BAIXO de proposito: ela
// pode ficar ligada por horas com o cockpit parado. Por cima disso ainda
// vale o limitador global de brilho do driver (25-75%, padrao 60%) — entao
// no padrao a animacao sai a ~19% de brilho real. 0 desliga a animacao.
static const uint8_t LED_IDLE_BRIGHTNESS = 80;

// Preenche os buffers lógicos para o instante 'nowMs' (millis()).
//   matrix64: 64 cores em ordem linear, linha a linha (mesma ordem que o
//             SimHub usa — o remapeamento serpentina é feito por quem chama)
//   strip:    'stripCount' cores da fita
// Efeito: onda de arco-íris lenta correndo na diagonal da matriz e ao longo
// da fita, com um "respirar" suave de brilho.
void led_idle_render(uint32_t nowMs, LedIdleColor *matrix64,
                     LedIdleColor *strip, uint16_t stripCount);
