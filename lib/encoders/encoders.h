// Decoder de quadratura para os 4 encoders KY-040 (CLK+DT), direto na
// ESP32-S3 (ver docs/INPUTS_PINOUT.md secao 4/8). O SW de cada encoder NÃO
// é tratado aqui — vai pelo 74HC4067, camada separada.
//
// Base do decoder — quantas transições por detent:
// O módulo KY-040 "de placa azul" (o modelo ubíquo com os dois resistores
// de pull-up e o capacitor de 104 já embutidos na placazinha) descansa com
// CLK e DT ambos em HIGH entre cliques, e percorre o ciclo de Gray completo
// de 4 estados (11->01->00->10->11, ou o espelho, dependendo do sentido) a
// cada detent mecânico — ou seja, **4 transições de pino = 1 detent**. Essa
// é a característica documentada e amplamente observada desse modelo
// específico; não foi medida no componente físico do usuário (hardware
// ainda não montado). Se, ao testar de verdade, um detent físico não bater
// 1:1 com um evento (faltando ou sobrando), o ponto a revisar é exatamente
// essa suposição — o motivo mais provável seria um lote de KY-040 que
// descansa em LOW-LOW em vez de HIGH-HIGH, o que inverteria a leitura do
// estado de repouso no decoder.
//
// Estratégia de robustez (bounce, estado inválido, giro rápido/lento,
// reversão, ruído): máquina de estados de 7 estados sobre o ciclo de Gray
// (não uma tabela de pinos crus) — só emite ENCODER_CW/ENCODER_CCW quando
// o ciclo de 4 passos se fecha de volta no repouso (11). Qualquer bounce
// (passo que volta pra trás) é absorvido sem emitir evento nem perder
// posição; qualquer salto de 2 bits ao mesmo tempo (fisicamente
// impossível num contato real, sinal de ruído) reseta pro repouso sem
// emitir nada — nunca gera um evento no sentido errado. Reversão no meio
// do ciclo (girar e voltar antes de completar o detent) também não emite
// nada, exatamente como o clique mecânico não teria acontecido de verdade.
//
// Separação ISR / loop: a interrupção (CHANGE em CLK e DT) só lê os 2
// pinos e empilha a amostra crua num ring buffer por encoder — não roda a
// máquina de estados dentro da ISR, pra manter a ISR mínima. Quem
// realmente decodifica é encoder_update(), chamada do loop principal, que
// esvazia o ring buffer e processa cada amostra em ordem. Isso garante que
// nenhuma transição se perde mesmo em giro rápido (toda borda gera uma
// amostra própria, processada depois, sem depender de a leitura coincidir
// com o momento exato da mudança).
#pragma once
#include <stdint.h>

enum EncoderEvent : uint8_t {
  ENCODER_NONE = 0,
  ENCODER_CW,
  ENCODER_CCW,
};

static const uint8_t ENCODER_COUNT = 4;

// Pinos default (docs/INPUTS_PINOUT.md secao 4) — todos em GPIO0-21,
// universal em qualquer variante de ESP32-S3.
static const uint8_t ENCODER_DEFAULT_CLK_PIN[ENCODER_COUNT] = {4, 6, 10, 12};
static const uint8_t ENCODER_DEFAULT_DT_PIN[ENCODER_COUNT]  = {5, 7, 11, 13};

// Configura os 8 pinos (4 x CLK/DT) como entrada (com pull-up interno da
// ESP32 como rede de segurança — os módulos KY-040 já trazem pull-up
// próprio, mas isso evita leitura flutuante se algum não tiver) e liga as
// interrupções CHANGE. Usa os pinos default de docs/INPUTS_PINOUT.md.
void encoder_init();

// Esvazia o ring buffer de amostras cruas de cada um dos 4 encoders e
// avança a máquina de estados de quadratura sobre elas, em ordem. Não usa
// delay(), não bloqueia: o trabalho por chamada é proporcional só ao
// número de transições acumuladas desde a última chamada (tipicamente
// zero ou poucas). Chamar com a maior frequência possível a partir do
// loop() principal.
void encoder_update();

// Consome UM evento pendente do encoder 'index' (0-3), na ordem em que a
// fila interna guarda (todos os CW pendentes antes dos CCW pendentes, ou
// vice-versa — a ordem exata entre direções diferentes não é garantida em
// caso de inversão rápida, mas a CONTAGEM de cada direção é exata: nenhum
// detent é perdido nem duplicado). Chamar em loop (`while (...) !=
// ENCODER_NONE`) para drenar todos os eventos acumulados desde a última
// leitura, especialmente após giro rápido. index fora de 0-3 devolve
// ENCODER_NONE.
EncoderEvent encoder_get_event(uint8_t index);

// Posição absoluta acumulada do encoder 'index' (0-3): incrementada em
// cada ENCODER_CW, decrementada em cada ENCODER_CCW, independente de
// alguém ter chamado encoder_get_event() ou não. index fora de 0-3 devolve
// 0.
int32_t encoder_get_position(uint8_t index);
