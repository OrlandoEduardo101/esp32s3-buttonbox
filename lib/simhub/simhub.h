// Integração com o protocolo "Standard Serial" do SimHub (LEDs), sobre o
// CDC já existente do firmware. NÃO é o SHCustomProtocol (explicitamente
// fora de escopo) — é o protocolo binário simples que o próprio SimHub usa
// pra falar com qualquer dispositivo CDC ("SimHub Standard Serial
// Protocol", descrito como "o protocolo mais universal", implementável em
// qualquer coisa com CDC).
//
// Fontes consultadas (ver docs/SIMHUB_PROTOCOL.md para o levantamento
// completo, incluindo o que NÃO está documentado publicamente e por isso
// foi decisão de engenharia deste firmware, não fato verificado):
//   - github.com/SHWotever/SimHub/wiki (sketch V2 + "Getting started")
//   - manual.simhubdash.com (device-communication-protocols)
//
// Framing verificado (documentação oficial):
//   cabeçalho: 6 bytes 0xFF
//   comando ASCII de 5-6 letras: "proto" | "ledsc" | "sleds" | "unlock"
//   "proto" -> responde "SIMHUB_1.0\r\n"
//   "ledsc" -> responde "<contagem>\r\n" (quantos LEDs este dispositivo tem)
//   "sleds" -> <N*3 bytes RGB, N = contagem de "ledsc"> + terminador
//              (0xFF)(0xFE)(0xFD)
//   "unlock" -> responde "Upload unlocked\r\n" (específico de bootloader
//              AVR; sem efeito real no ESP32-S3, mas respondido por
//              compatibilidade — "não é obrigatório" segundo a doc)
//
// Existe também um "SimHub Standard HID Protocol" (relatos HID com Report
// ID próprio) — NÃO USADO aqui por instrução explícita de usar o CDC já
// existente, não HID.
//
// O firmware SÓ recebe e guarda RGB num framebuffer. Nenhuma lógica de
// RPM/flags/seta/pit-limiter/shift-light: quem decide o que cada LED
// mostra é o SimHub, do lado de fora.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// Quantidade de LEDs que este firmware anuncia via "ledsc". PRECISA bater
// EXATAMENTE com o "LED count" configurado no perfil do dispositivo dentro
// do SimHub — se um lado achar que são N e o outro M, cada frame "sleds"
// desalinha para sempre (o terminador nunca cai no lugar certo). A ORDEM
// de quais índices são a matriz e quais são a fita é decidida inteiramente
// no editor de dispositivo do SimHub (zonas), não neste firmware — aqui é
// só um array plano de N RGBs, na ordem que o SimHub mandar.
//
// CONFIGURÁVEL EM RUNTIME (persistido em NVS) — não é mais uma constante
// de compilação. Comando serial: "SETLEDS <n>" (ex.: "SETLEDS 74" para a
// matriz 8x8 + fita ~10 do hardware do usuário). Sobrevive a reboot; não
// precisa recompilar nem regravar o firmware pra mudar. Ver
// simhub_set_led_count() e SIMHUB_LED_COUNT_DEFAULT/_MAX abaixo.
static const uint16_t SIMHUB_LED_COUNT_DEFAULT = 74; // usado só no 1º boot, antes de qualquer SETLEDS
static const uint16_t SIMHUB_LED_COUNT_MAX     = 256; // tamanho fixo dos buffers internos (sem alocação dinâmica)

// Orçamento de tempo TOTAL (não por byte) para consumir um frame inteiro
// depois que o cabeçalho de 6x 0xFF já foi visto. Cobre fragmentação do
// USB CDC (um frame pode chegar em vários pacotes USB) sem travar o loop
// principal indefinidamente se o SimHub parar de mandar bytes no meio —
// valor de engenharia deste firmware (não documentado publicamente pelo
// SimHub), dimensionado com folga generosa sobre o tempo real de uma
// transferência CDC em USB Full Speed.
static const uint32_t SIMHUB_FRAME_TIMEOUT_MS = 100;

// Se nenhum "sleds" válido chegar por este tempo, consideramos a conexão
// com o SimHub caída e voltamos o framebuffer para apagado (0,0,0) — não
// fica preso mostrando a última cor para sempre se o SimHub fechar/travar.
// Também decisão de engenharia (não documentada publicamente).
static const uint32_t SIMHUB_CONNECTION_TIMEOUT_MS = 3000;

struct SimhubColor {
  uint8_t r, g, b;
};

// Zera o framebuffer e o estado interno, e carrega a contagem de LEDs
// persistida em NVS (ou SIMHUB_LED_COUNT_DEFAULT, no primeiro boot). Não
// mexe no Serial/CDC (já inicializado em outro lugar) — só prepara este
// módulo.
void simhub_init();

// Troca a contagem de LEDs anunciada via "ledsc" e grava em NVS (namespace
// "simhub", chave "ledcount") — sobrevive a reboot. 'n' fora de 1..
// SIMHUB_LED_COUNT_MAX é rejeitado (devolve false, nada muda). Zera o
// framebuffer e marca a conexão como caída (força o SimHub a mandar um
// "sleds" novo antes de voltarmos a reportar conectado) — evita expor
// LEDs "fantasmas" de uma contagem antiga.
bool simhub_set_led_count(uint16_t n);

// Alimenta UM byte cru (já lido do Serial por quem chama) no detector do
// cabeçalho de 6x 0xFF. Devolve true exatamente no byte que fecha a 6ª
// ocorrência consecutiva — quem chama deve então invocar
// simhub_process_packet() para consumir o resto do frame. Devolve false
// em qualquer outro caso. Chamar para TODO byte lido do Serial antes de
// decidir o que fazer com ele (inclusive bytes que não são 0xFF — isso
// reresenta a contagem parcial se uma sequência de 0xFF for
// interrompida por outro byte).
bool simhub_feed_header_byte(uint8_t b);

// Consome do Serial o restante de UM frame SimHub (comando + payload +
// terminador, quando aplicável) — chamar logo depois que
// simhub_feed_header_byte() devolver true. Pode bloquear por até
// SIMHUB_FRAME_TIMEOUT_MS no total (nunca mais que isso) esperando bytes
// que o CDC ainda não entregou. Um "sleds" só é publicado no framebuffer
// se os N*3 bytes de payload E o terminador (0xFF)(0xFE)(0xFD) chegarem
// dentro do orçamento de tempo — qualquer timeout, comando desconhecido
// ou terminador incorreto descarta o frame silenciosamente (perda de
// sincronização se resolve sozinha no próximo cabeçalho de 6x 0xFF, sem
// precisar reiniciar nada).
void simhub_process_packet();

// Chamar a partir do loop() principal, com qualquer frequência — só
// verifica timeout de conexão (ver SIMHUB_CONNECTION_TIMEOUT_MS). Não lê
// Serial, não bloqueia.
void simhub_update();

// true se um "sleds" válido foi recebido há menos de
// SIMHUB_CONNECTION_TIMEOUT_MS.
bool simhub_is_connected();

uint16_t simhub_get_led_count();

// index fora de 0..SIMHUB_LED_COUNT-1 devolve {0,0,0}.
SimhubColor simhub_get_led(uint16_t index);
