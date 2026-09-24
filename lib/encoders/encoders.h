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

// Pinos default — usados só se este driver for usado isolado, fora deste
// projeto (sem passar clkPins/dtPins explícitos).
//
// ATENÇÃO: neste projeto estes números NÃO valem mais. Desde a revisão 2 do
// pinout os encoders não estão em GPIO — vêm do banco A do MCP23017, por
// encoder_init_external()/encoder_feed() (ver include/board_config.h). Por
// coincidência, 4/6/10/12 são hoje as linhas S0-S3 do 74HC4067: chamar
// encoder_init() sem argumentos aqui dentro roubaria o mux. Não faça isso.
static const uint8_t ENCODER_DEFAULT_CLK_PIN[ENCODER_COUNT] = {4, 6, 10, 12};
static const uint8_t ENCODER_DEFAULT_DT_PIN[ENCODER_COUNT]  = {5, 7, 11, 13};

// Configura os 8 pinos (4 x CLK/DT) como entrada (com pull-up interno da
// ESP32 como rede de segurança — os módulos KY-040 já trazem pull-up
// próprio, mas isso evita leitura flutuante se algum não tiver) e liga as
// interrupções CHANGE. clkPins[i]/dtPins[i] = pinos do encoder i (0-3).
void encoder_init(const uint8_t clkPins[ENCODER_COUNT] = ENCODER_DEFAULT_CLK_PIN,
                   const uint8_t dtPins[ENCODER_COUNT]  = ENCODER_DEFAULT_DT_PIN);

// --- Modo EXTERNO: quadratura vinda de um expansor de I/O --------------
//
// Usado quando CLK/DT NÃO estão em GPIO da MCU e sim atrás de um barramento
// (neste projeto: banco A do MCP23017, via I2C — ver include/board_config.h
// e docs/INPUTS_PINOUT.md). Nesse caso não existe interrupção por borda:
// quem descobre as transições é uma amostragem periódica, e ela é que
// entrega as amostras aqui com encoder_feed().
//
// A MÁQUINA DE ESTADOS É EXATAMENTE A MESMA do modo GPIO — só muda quem
// produz as amostras. Todo o tratamento de bounce/ruído/reversão descrito
// no topo deste arquivo continua valendo sem uma linha de diferença.
//
// Trade-off honesto de usar este modo: com ISR, NENHUMA borda se perde,
// porque o hardware acorda a MCU em cada uma. Amostrando, uma transição só
// é vista se a amostra cair entre ela e a próxima — por isso o período de
// amostragem tem que ser bem menor que o intervalo entre duas transições
// do giro mais rápido que se espera (a conta está em
// include/board_config.h, BOARD_MCP_SAMPLE_PERIOD_MS). Amostrar devagar
// demais não gera evento errado — gera detent PERDIDO, porque um salto de
// 2 bits é tratado como ruído e reseta a máquina.
//
// Inicializa os 4 encoders sem tocar em nenhum pino e sem registrar
// interrupção — só zera a máquina de estados e as filas. Use no lugar de
// encoder_init(), nunca os dois.
void encoder_init_external();

// Entrega UMA amostra crua do encoder 'index' (0-3). clkLevel/dtLevel são
// os níveis lidos (0 ou 1, qualquer valor != 0 conta como 1), na mesma
// convenção elétrica do modo GPIO (repouso = ambos em 1).
//
// Amostras IGUAIS à anterior são descartadas aqui mesmo — pode chamar em
// toda varredura, mesmo parado, que o ring buffer não enche: só entra
// amostra quando algum dos dois pinos realmente mudou.
//
// Contexto de chamada: esta função é o PRODUTOR do ring buffer SPSC.
// Chame sempre do MESMO contexto (a task de amostragem, ou uma ISR — mas
// não dos dois). O consumidor continua sendo encoder_update(), chamada do
// loop principal.
void encoder_feed(uint8_t index, uint8_t clkLevel, uint8_t dtLevel);

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
