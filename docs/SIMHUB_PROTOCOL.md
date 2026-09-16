# SIMHUB_PROTOCOL.md — Integração SimHub via CDC (Standard Serial)

> Levantamento feito antes de implementar, como pedido. Cada afirmação
> abaixo é marcada como **verificado** (fonte pública consultada) ou
> **decisão de engenharia** (a documentação oficial não especifica, e a
> escolha foi feita e justificada por este firmware). Gerado em 2026-09-15.

## 1-2. Documentação/repositórios consultados e o protocolo Standard Serial atual

Fontes:
- [SIMHUB STANDARD ARDUINO PRO MICRO sketch V2 — wiki oficial](https://github.com/SHWotever/SimHub/wiki/SIMHUB-STANDARD-ARDUINO-PRO-MICRO-sketch-V2/a928282469966f6825e4f237abd1b3bd245311ad)
- [Getting started with the SIMHUB STANDARD ARDUINO PRO MICRO LEDs sketch](https://github.com/SHWotever/SimHub/wiki/Getting-started-with-the-SIMHUB-STANDARD-ARDUINO-PRO-MICRO-LEDs-sketch)
- [Device communication protocols — manual oficial atual](https://manual.simhubdash.com/device-definition-authoring/device-communication-protocols)
- [Arduino Setup — wiki oficial](https://github.com/SHWotever/SimHub/wiki/Arduino--Setup)

O manual oficial atual (`manual.simhubdash.com`) confirma a existência do
protocolo e o descreve como **"o mais universal", implementável em
qualquer dispositivo com CDC** — exatamente o que este firmware já tem.
Mas o manual novo é construído em torno do "Standard Serial Firmware
Builder" (uma ferramenta que gera o sketch pronto), e não publica o
framing byte a byte — isso só está documentado na wiki mais antiga (que
continua sendo a referência oficial pública) e o próprio código-fonte que
o SimHub gera diz "veja `protocol.h`" para o detalhe completo (arquivo
que só existe dentro do firmware exportado, não publicado à parte).

**Framing verificado** (bate entre as duas fontes da wiki, buscas
independentes e resultados idênticos):

| Elemento | Valor |
|---|---|
| Cabeçalho de todo comando | 6 bytes `0xFF` |
| Query de versão | header + `"proto"` → resposta `"SIMHUB_1.0\r\n"` |
| Query de contagem de LEDs | header + `"ledsc"` → resposta `"<N>\r\n"` (ex.: `"32\r\n"`) |
| Envio de cor dos LEDs | header + `"sleds"` + N×3 bytes RGB (R,G,B por LED, N = valor respondido em `ledsc`) + terminador |
| Terminador do `sleds` | 3 bytes fixos `0xFF 0xFE 0xFD` ("basic message integrity check") |
| Destrava upload (bootloader AVR) | header + `"unlock"` → resposta `"Upload unlocked\r\n"` — **opcional**, documentado como "not mandatory to get SimHub working" |
| Baud rate (Arduino Pro Micro/Leonardo) | 115200 |
| Terminador das respostas de texto | `\r\n` (CRLF) |

Não há checksum separado no `sleds` — o terminador de 3 bytes fixos é o
único mecanismo de integridade documentado.

## 3. Standard HID — existe, não é usado aqui

O manual oficial confirma um **"SimHub Standard HID Protocol"** paralelo,
com reports HID próprios (LED = Report ID `0x68` por padrão, primeiros 4
bytes = header com índice/contagem/flag de LED, resto = RGB). É uma via
alternativa real, mas **não usada neste firmware** por instrução explícita
de usar a CDC já existente — HID já está ocupado pelo gamepad (`Gamepad`,
`USBHIDGamepad`) e não deve ser alterado (restrição desta tarefa).

## 4. Comparação com o firmware "Arduino Standard"

O protocolo Standard Serial **é literalmente o mesmo** que o sketch
"Arduino Pro Micro/Leonardo Standard" da wiki usa — não há dois protocolos
diferentes aqui, o firmware Arduino de referência é a implementação
canônica deste mesmo framing (`proto`/`ledsc`/`sleds`/`unlock`). Não existe
uma variante "mais nova" ou diferente para outras placas — o que muda de
placa pra placa é só a camada de transporte (USB CDC nativo aqui, na
Pro Micro é o CDC do próprio ATmega32U4), o framing acima é idêntico.

## 5. Framing exato implementado

Igual à tabela da seção 1-2, sem nenhuma alteração — implementado byte a
byte em `lib/simhub/simhub.cpp`.

---

## O que a documentação pública NÃO especifica (decisões de engenharia deste firmware)

A wiki e o manual descrevem o **formato dos bytes**, mas não o
**comportamento operacional** abaixo — não achei nenhuma fonte pública
que descreva isso byte a byte (nem no manual novo, nem na wiki antiga, nem
em reimplementações de terceiros pesquisadas). Cada item foi decidido e
documentado explicitamente em `lib/simhub/simhub.h`:

| Aspecto | Decisão deste firmware | Por quê |
|---|---|---|
| **Timeout de frame** | Orçamento total de 100 ms (não por byte) para consumir um frame inteiro depois do cabeçalho | Cobre fragmentação normal do USB CDC (um frame pode chegar em vários pacotes) sem arriscar travar o `loop()` por segundos se o SimHub parar de mandar bytes no meio — um timeout por byte multiplicado pelo tamanho do payload poderia somar segundos de bloqueio no pior caso |
| **Pacotes fragmentados** | `readByteUntil()` espera (dentro do orçamento de 100 ms) cada byte individualmente, então um frame que chega em vários pedaços USB é remontado transparentemente | Padrão para qualquer protocolo binário sobre um stream de bytes; simples e suficiente pro volume de dados aqui (poucas dezenas de bytes por frame) |
| **Perda de sincronização / corrupção** | Se o terminador `0xFF 0xFE 0xFD` não bater, ou se o comando não for reconhecido, o frame inteiro é descartado sem publicar nada — nenhum estado fica "meio atualizado". A resincronização acontece sozinha no próximo cabeçalho de 6× `0xFF` que o SimHub manda no frame seguinte | Mais simples e mais seguro que tentar re-alinhar no meio de um payload binário; como o SimHub reenvia continuamente (é um protocolo de estado, não de comando único), um frame perdido só atrasa a próxima atualização em vez de quebrar a conexão |
| **Múltiplos pacotes na fila** | Cada frame é consumido inteiramente antes de voltar pro loop principal, então frames enfileirados no buffer do CDC são processados um de cada vez, em ordem, na próxima passagem do `serialCommands()` | Simplicidade; o volume de tráfego do SimHub (algumas dezenas de Hz) nunca chega perto de saturar o buffer da CDC a ponto disso importar |
| **Timeout de conexão / reconexão** | Se nenhum `sleds` válido chegar por 3000 ms, `simhub_is_connected()` passa a `false` e o framebuffer volta pra apagado (0,0,0) | Evita ficar mostrando a última cor recebida pra sempre se o SimHub fechar ou o cabo cair — sem esse timeout, um LED "vermelho piscando alerta" travado aceso seria pior que simplesmente apagar |
| **Convivência com o console de texto existente (PING/VERSION/IP/BOOTLOADER)** | Todo byte lido passa primeiro por `simhub_feed_header_byte()`; só quando 6× `0xFF` fecham é que o resto do frame vai pro parser binário — qualquer outro byte (inclusive um `0xFF` isolado que não completou o cabeçalho) segue pro parser de texto normal | `0xFF` nunca é um caractere válido de início de comando de texto (`PING`, `VERSION`, etc. são maiúsculas ASCII), então não há ambiguidade real entre os dois protocolos no mesmo stream |

---

## Implementação

- `lib/simhub/simhub.h` / `.cpp` — parser binário + framebuffer RGB
  (`simhub_init()`, `simhub_feed_header_byte()`, `simhub_process_packet()`,
  `simhub_update()`, `simhub_get_led()`/`simhub_get_led_count()`,
  `simhub_set_led_count()`, `simhub_is_connected()`).
- `src/main.cpp` — `serialCommands()` agora despacha pra
  `simhub_process_packet()` quando vê o cabeçalho SimHub; resto do console
  de texto intocado. HID, WiFi, OTA, inputs, MCP23017, 74HC4067 e encoders
  não foram tocados nesta integração.
- `src/simhub_test.cpp` + `[env:simhub-test]` — firmware isolado (USB
  nativo, igual à produção) só com o parser SimHub, sem HID/WiFi/OTA/inputs.
- `scripts/simhub_test_send.py` — envia `proto`/`ledsc`/`sleds` de verdade
  pela porta serial e confere as respostas + o framebuffer recebido, sem
  precisar instalar o SimHub.

### Contagem de LEDs configurável em runtime (NVS), não mais hardcoded

A contagem de LEDs anunciada via `ledsc` **não é mais uma constante de
compilação** — é lida de NVS (`Preferences`, namespace `"simhub"`, chave
`"ledcount"`) no boot, com `SIMHUB_LED_COUNT_DEFAULT = 74` (matriz 8x8 = 64
+ fita ~10) usada só no primeiro boot, antes de qualquer configuração.

Comando serial novo (no console de texto de sempre, junto com
`PING`/`VERSION`/`IP`/`BOOTLOADER`): **`SETLEDS <n>`** — troca a contagem,
grava em NVS (sobrevive a reboot) e zera o framebuffer (evita expor LEDs
"fantasmas" de uma contagem antiga). `n` precisa estar entre 1 e
`SIMHUB_LED_COUNT_MAX` (256, tamanho fixo dos buffers internos — sem
alocação dinâmica). Exemplo: `SETLEDS 74`.

Isso não muda o protocolo em si — o SimHub continua sem como "empurrar"
uma contagem pro dispositivo (`ledsc` só funciona na direção
dispositivo→SimHub); o `SETLEDS` é uma extensão nossa, local, pro humano
configurar o dispositivo sem recompilar, não algo que o SimHub manda.

## O que este firmware deliberadamente NÃO faz

Por instrução explícita: nenhuma lógica de RPM, iFlag/flags, seta, pit
limiter ou shift light. O SimHub decide o que cada LED mostra e manda os
valores RGB já prontos via `sleds` — este firmware só recebe, guarda num
framebuffer e expõe via `simhub_get_led()`. Também não há, ainda,
nenhum código que pegue esse framebuffer e acenda LEDs físicos (WS2812 ou
outros) — isso fica para uma etapa seguinte; o critério de sucesso desta
tarefa é a **recepção** correta dos valores RGB, verificável com
`scripts/simhub_test_send.py` ou com o próprio SimHub configurado para
"Standard Serial" com o mesmo `LED count` já gravado na placa via
`SETLEDS` (74 por padrão — matriz 8x8 = 64 + fita ~10 LEDs).

---

## Como testar (critério de sucesso desta etapa)

1. Compilar e gravar `esp32s3-supermini` (integração completa) ou
   `simhub-test` (isolado, mais simples para este teste específico).
2. (opcional, só se a contagem final for diferente de 74) mandar
   `SETLEDS <n>\n` pelo console serial e conferir a resposta `LEDS_SET <n>`
   — persiste em NVS, não precisa repetir a cada boot.
3. `~/.platformio/penv/bin/python scripts/simhub_test_send.py <porta>` —
   confere automaticamente `proto`, `ledsc` e um `sleds` de teste (LED 0 =
   vermelho puro, LED 1 = verde, LED 2 = azul, resto = rampa).
4. Alternativa manual: qualquer terminal serial binário, ou o próprio
   SimHub com um perfil de dispositivo "Standard Serial" apontando pra
   porta COM da placa, LED count = 74 (64 da matriz + ~10 da fita — as
   zonas/ordem entre matriz e fita são configuradas no editor de
   dispositivo do SimHub, não neste firmware).
