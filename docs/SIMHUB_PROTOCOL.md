# SIMHUB_PROTOCOL.md — Integração SimHub via CDC

> Atualizado em 2026-09-16, depois de uma correção de rota. Ver a seção
> "Erro anterior" no fim — a primeira implementação usava o protocolo
> errado, e vale registrar por quê, pra ninguém repetir.

## Objetivo

O SimHub deve reconhecer a placa como um **dispositivo Arduino normal**,
para que **todos os efeitos de LED sejam configurados dentro do SimHub** —
nenhum comportamento (RPM, flags, shift light, pit limiter) é escrito no
firmware. O firmware só recebe RGB pronto e acende.

Divisão pedida pelo usuário:
- **Matriz 8x8** → recurso "RGB Matrix" do SimHub (iFlag etc.)
- **Fita de LEDs** → recurso "RGB Leds" do SimHub (RPM etc.)

E **sem `SHCustomProtocol`** (comando `'P'`) — explicitamente descartado.

## Fontes (implementações que comprovadamente funcionam)

O protocolo não está publicado byte a byte na documentação oficial. Foi
extraído de duas implementações reais, ambas presentes na máquina do
usuário e ambas aceitas pelo SimHub dele:

- `~/Projects/arduino/ESP-SimHub` (upstream) — `src/SHCommands.h`,
  `src/main.cpp` (dispatcher), `src/SHRGBLedsBase.h` (formato do stream RGB)
- `~/Projects/arduino/ESP-SimHub-ESP32S3-SCREEN` (fork em uso) — mesmos
  arquivos, versão enxuta

## Framing

```
0x03 (MESSAGE_HEADER)  +  1 char de comando  +  payload específico
```

Sem checksum, sem terminador global: cada comando define seu próprio
payload e sua própria resposta.

### Comandos implementados

| Cmd | Nome | Payload recebido | Resposta |
|---|---|---|---|
| `'1'` | Hello | 1 byte (trailer, descartado) | char de versão `'j'` |
| `'0'` | Features | — | `"NIXR\n"` |
| `'4'` | RGB LED count | — | 1 byte = LEDs da **fita** |
| `'6'` | RGB LED data | stream RGB | `0x15` (ACK) |
| `'R'` | RGB Matrix data | stream RGB (64 px) | `0x15` (ACK) |
| `'N'` | Device name | — | `"ESP32S3-ButtonBox\n"` |
| `'I'` | Unique ID | — | MAC em hex + `"\n"` |
| `'A'` | Acq | — | `0x03` |
| `'X'` | Expandido | string até `' '`/`'\n'` | `list` → lista; `mcutype` → `1E 98 01`; resto → `0x15` |
| `'J'` `'2'` `'B'` | Contadores | — | `0x00` (botões vão por HID nativo) |
| `'G'` | Gear | 1 char | `0x15` |
| `'8'` | Baudrate | 1 byte (código) | — (irrelevante em USB CDC) |

### Features anunciadas

`N` (nome) · `I` (unique id) · `X` (comandos expandidos) · `R` (RGB Matrix).

Deliberadamente **fora**: `P` (SHCustomProtocol, descartado pelo usuário),
`J`/`G` (botões e marcha vão pelo HID nativo, não por este protocolo),
`M`/`L`/`K`/`V` (displays e motores que não existem aqui).

A fita ("RGB Leds") não tem letra de feature — o SimHub descobre pela
resposta do comando `'4'` ser maior que zero.

### Formato do stream RGB (comandos `'6'` e `'R'`)

Idêntico ao `SHRGBLedsBase::read()` das implementações de referência:

```
mode = byte
enquanto mode > 0:
    mode 1 -> todos os LEDs em sequência: (r,g,b) × ledCount
    mode 2 -> startLed, numLeds, depois (r,g,b) × numLeds
    mode 3 -> startLed, numLeds, um (r,g,b) repetido no intervalo
    mode = byte
```

O stream termina quando chega um `0` (ou o timeout estoura).

## Configuração de LEDs

- **Matriz**: fixa em **64** (8x8). O protocolo não tem comando de
  contagem de matriz — o driver de referência instancia 64 pixels fixos.
- **Fita**: configurável em runtime pelo comando serial **`SETLEDS <n>`**
  (persistido em NVS, sobrevive a reboot e a OTA). É o valor devolvido no
  comando `'4'`.

## Layout físico dos LEDs

Uma **única cadeia WS2812** no GPIO1: a **matriz 8x8 primeiro** (pixels
0-63), a **fita logo depois** (DOUT da matriz → DIN da fita). Para o
SimHub continuam sendo dois dispositivos lógicos separados; quem junta as
duas numa cadeia só é o código de integração em `src/main.cpp`.

A matriz usa remapeamento **serpentina** (linhas alternadas invertidas),
que é como a maioria dos painéis 8x8 de WS2812 é ligada. Se o seu painel
for de linhas retas, é só trocar `MATRIX_SERPENTINE` para `false` em
`src/main.cpp`.

## Comportamento de conexão

Sem nenhum comando por **5 s**, o firmware considera o SimHub desconectado
e apaga os dois framebuffers (mesma ideia do `Command_Shutdown` das
implementações de referência). Assim os LEDs não ficam congelados na
última cor se o SimHub fechar.

## Implementação

- `lib/simhub/` — parser do protocolo + dois framebuffers (matriz e fita)
- `lib/ws2812/` — driver WS2812 via RMT (não conhece SimHub nem telemetria)
- `src/main.cpp` — despacho do `0x03` no leitor de serial (convivendo com
  o console de texto), e a ponte framebuffers → cadeia física
- `src/simhub_test.cpp` + `[env:simhub-test]` — teste isolado
- `scripts/simhub_test_send.py` — fala o protocolo real e confere as
  respostas, sem precisar abrir o SimHub

---

## Erro anterior (registrado de propósito)

A primeira implementação seguia o protocolo `0xFF×6 + "proto"/"ledsc"/"sleds"`,
documentado na wiki oficial como *"SIMHUB STANDARD ARDUINO PRO MICRO LEDs
sketch"*. Esse protocolo **existe e foi implementado corretamente** — o
`scripts/simhub_test_send.py` da época confirmou `proto`, `ledsc` e `sleds`
funcionando na placa real. Mas ele é do **sketch legado de LEDs**, e não é
o que o scanner da aba "Arduino" do SimHub fala.

O que provou o erro: o log do SimHub do usuário
(`Arduino scan COM27 ... Hello (sending)` → `Unrecognized (5x)`) contra um
firmware que respondia perfeitamente ao protocolo antigo. O SimHub estava
mandando `0x03 '1'` (Hello) e não recebia nada.

Lição: a documentação pública descrevia um protocolo real, mas não *o*
protocolo do recurso que o usuário queria usar. A verificação que faltou
foi testar contra o consumidor real (o próprio SimHub) em vez de só contra
um script que falava o protocolo que eu mesmo tinha implementado.
