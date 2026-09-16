🇧🇷 Português | [🇺🇸 English](README.en.md)

# ESP32-S3 SimHub Button Box

![Render do button box](assets/buttonbox-render.png)

Button box open source (firmware + case impresso em 3D) pra sim racing:
USB HID gamepad nativo, WiFi + OTA, e integração real com o SimHub — a
placa aparece pro SimHub como um "Arduino" padrão, e **todos os efeitos de
luz são configurados dentro do próprio SimHub**, sem precisar escrever
nenhum comportamento de jogo no firmware.

## O que é este projeto

Um button box completo, do hardware ao firmware, com duas partes:

1. **Eletrônica + firmware** (este repositório): uma ESP32-S3 lê até 31
   controles físicos (botões, encoders, ignição, chaves, freio de
   estacionamento) e os expõe como um gamepad USB padrão, ao mesmo tempo
   em que fala o protocolo real do SimHub pela mesma porta serial pra
   controlar uma matriz de LEDs 8x8 (ex.: bandeiras/iFlag) e uma fita de
   LEDs (ex.: RPM).
2. **Case impresso em 3D** (pasta [`assets/`](assets)): o invólucro físico
   completo, mais pontos de fixação pra encaixar acessórios impressos por
   terceiros (freio de estacionamento, botão de rádio PTT) — ver seção
   abaixo.

## Recursos principais

- **HID nativo** (USB-OTG/TinyUSB) — aparece como gamepad no Windows sem
  driver nenhum, 32 botões.
- **WiFi + OTA** — depois da primeira gravação por cabo, todo o resto é
  sem fio.
- **Protocolo Arduino real do SimHub** — incluindo a camada de transporte
  ARQ (checksum + confirmação por pacote) que o scanner da aba "Arduino"
  do SimHub realmente usa, não um sketch de LED simplificado.
- **RGB Matrix (8x8) + RGB Leds (fita)** — dois dispositivos lógicos
  separados pro SimHub, numa única cadeia física de LEDs endereçáveis.
  Nenhuma lógica de jogo no firmware: o SimHub decide as cores, o
  firmware só recebe e acende.
- **Pinout 100% centralizado e configurável** — trocar de placa ESP32 ou
  reaproveitar este projeto com outro pinout é editar um arquivo só (ver
  abaixo).
- **Sem matriz de botões** — cada entrada tem canal/pino próprio,
  priorizando confiabilidade e simplicidade de fiação.
- **Case open source** — impresso em 3D, com pontos pra parafusar
  acessórios de terceiros.

## Hardware eletrônico

| Componente | Função |
|---|---|
| ESP32-S3 (qualquer variante com USB-OTG nativo) | MCU — HID, WiFi, OTA, protocolo SimHub |
| MCP23017 (I2C) | 11 push buttons, ignição (3 posições), botão Start Engine |
| 74HC4067 (mux 16 canais) | SW dos 4 encoders, 4 chaves "caça", freio de estacionamento |
| 4× encoder KY-040 | CLK/DT direto na MCU (quadratura por interrupção); SW pelo mux |
| Matriz WS2812 8x8 (64 LEDs) | RGB Matrix do SimHub — ex.: iFlag |
| Fita WS2812 (~10 LEDs, configurável) | RGB Leds do SimHub — ex.: RPM |

## Case impresso em 3D

Tudo em [`assets/`](assets):

| Arquivo | Conteúdo |
|---|---|
| `assets/STL/front.stl` | Painel frontal |
| `assets/STL/rear.stl` | Painel traseiro |
| `assets/STL/stand.stl` | Base/suporte |
| `assets/STL/LED holder.stl` | Suporte da matriz/fita de LED |
| `assets/buttonbox.f3z` | Projeto Fusion 360 completo, editável (arquivo grande — ver nota abaixo) |

> **Nota sobre o `buttonbox.f3z`**: esse arquivo tem ~124 MB, acima do
> limite de 100 MB que o GitHub aceita sem Git LFS configurado no
> repositório. Ele ainda não está versionado aqui — se você precisar do
> projeto Fusion 360 editável, pergunte ou acompanhe o repositório pra
> essa parte ser resolvida (Git LFS, ou hospedagem externa).

### Acessórios de terceiros compatíveis

Estes não são meus — são projetos separados de outros criadores, feitos
pra combinar com este button box. Baixe, imprima e parafuse direto no
case:

- **[Freio de estacionamento DIY para truck simulators](https://www.printables.com/model/995554-diy-parking-brake-for-truck-simulators/files)**
  (Printables) — a alavanca física que aciona o `INPUT_HANDBRAKE`
  (ver [`docs/INPUTS_PINOUT.md`](docs/INPUTS_PINOUT.md) seção 7 pra como o
  firmware trata esse sinal).
- **Botão de rádio (PTT)** — duas opções no Thingiverse:
  [thing:4740146](https://www.thingiverse.com/thing:4740146) e
  [thing:2928122](https://www.thingiverse.com/thing:2928122).

Confira a licença e os créditos de cada modelo na própria página do
autor antes de usar/redistribuir.

## Por que dá pra reaproveitar isso pra outra placa/pinout

Todo pino, canal e endereço deste projeto está centralizado em
**[`include/board_config.h`](include/board_config.h)** — é o único arquivo
que muda se você trocar de placa ESP32 (maior, outra variante) ou só
quiser usar pinos diferentes. Nenhum outro `.cpp`/`.h` tem número de pino
"solto" no meio do código.

Os drivers de cada componente (`lib/mcp23017`, `lib/mux4067`,
`lib/encoders`, `lib/ws2812`) são genéricos e continuam funcionando
sozinhos, fora deste projeto — cada um aceita pino/endereço como parâmetro
e só usa um valor default próprio se você não passar nada.

## Arquitetura (visão geral)

```
                    ESP32-S3
                       │
        ┌──────────────┼───────────────┐
        │              │               │
       HID            CDC             WiFi
        │              │               │
        │           SimHub            OTA
        │              │
        │         RGB framebuffer
        │              │
        │              ▼
        │           WS2812 (matriz + fita, em série)
        │
        ▼
      INPUTS
        │
   ┌────┴─────────────┐
   │                  │
ESP32 GPIO        Expansores
(encoders)     ┌────┴────┐
               │         │
           MCP23017   74HC4067
               │         │
               └─────────┴── controles físicos
```

Cada seta é uma camada independente — nenhuma conhece a lógica da outra
(ex.: o driver WS2812 não sabe o que é RPM; o parser do SimHub não sabe o
que é GPIO). Detalhe completo, com a auditoria de cada regra de
isolamento, em [`docs/SYSTEM_INTEGRATION.md`](docs/SYSTEM_INTEGRATION.md).

## Como compilar e gravar

1. **Credenciais de WiFi** (obrigatório, não versionado):
   ```
   cp include/secrets.h.example include/secrets.h
   ```
   e edite `include/secrets.h` com sua rede WiFi real.

2. **Pinout** (só se sua fiação for diferente do padrão): edite
   [`include/board_config.h`](include/board_config.h).

3. **Compilar + gravar por USB** (primeira vez, ou sem WiFi disponível):
   ```
   pio run -e esp32s3-supermini -t upload
   ```
   Não precisa segurar nenhum botão — o firmware entra em modo download
   sozinho (ver `scripts/enter_bootloader.py`).

4. **Gravar por OTA** (depois da primeira vez, com WiFi já configurado):
   ```
   pio run -e ota -t upload
   ```

## Como testar cada peça isoladamente

Cada componente de hardware tem um firmware de teste próprio, sem HID/
WiFi/OTA/SimHub misturado — grava, conecta só aquele componente, e
confirma que funciona antes de integrar com o resto:

| Env | Testa | Comando |
|---|---|---|
| `mcp-test` | MCP23017 (16 entradas) | `pio run -e mcp-test -t upload -t monitor` |
| `mux-test` | 74HC4067 (canais configurados) | `pio run -e mux-test -t upload -t monitor` |
| `encoder-test` | Os 4 KY-040 (CW/CCW) | `pio run -e encoder-test -t upload -t monitor` |
| `inputs-test` | Camada unificada (os três juntos) | `pio run -e inputs-test -t upload -t monitor` |
| `ws2812-test` | Matriz + fita (varredura de cores) | `pio run -e ws2812-test -t upload -t monitor` |
| `simhub-test` | Protocolo SimHub isolado | `pio run -e simhub-test -t upload` + `python scripts/simhub_test_send.py <porta>` |
| `diag` | Toolchain/board mínimo (sem HID) | `pio run -e diag -t upload -t monitor` |

Se o firmware completo (`esp32s3-supermini`) apresentar algum problema,
comece isolando pelo teste do componente suspeito — é mais rápido achar a
causa assim do que depurar tudo junto.

## Console serial (CDC)

Além do protocolo do SimHub, a mesma porta COM aceita comandos de texto
(qualquer terminal serial, 115200 baud):

| Comando | Efeito |
|---|---|
| `PING` | responde `PONG` |
| `VERSION` | versão do firmware |
| `IP` | IP atual na rede |
| `SETLEDS <n>` | define quantos LEDs tem a fita (persiste em NVS) |
| `DUMPLEDS` | mostra o que os framebuffers da matriz/fita têm agora |
| `BOOTLOADER` | entra em modo download por software (sem precisar do botão BOOT) |

## Documentação completa

| Arquivo | Conteúdo |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | USB (VID/PID/descriptors), HID, CDC, WiFi, OTA — como cada peça do baseline funciona |
| [`docs/BASELINE.md`](docs/BASELINE.md) | Testes que comprovadamente passaram, com evidência |
| [`docs/INPUTS_PINOUT.md`](docs/INPUTS_PINOUT.md) | Por que cada entrada foi pro MCP23017/74HC4067/GPIO direto (o raciocínio; os números atuais estão em `board_config.h`) |
| [`docs/SIMHUB_PROTOCOL.md`](docs/SIMHUB_PROTOCOL.md) | Protocolo real do SimHub (transporte ARQ + comandos), com os erros de investigação registrados de propósito |
| [`docs/SYSTEM_INTEGRATION.md`](docs/SYSTEM_INTEGRATION.md) | Integração final, auditoria de isolamento entre camadas, checklist de teste completo |
| [`HANDOFF.md`](HANDOFF.md) | Histórico do projeto (incluindo a tentativa anterior com STM32, abandonada por hardware clonado) |

## Não implementado por decisão explícita

- Nenhuma lógica de jogo no firmware (RPM, flags, shift light, pit
  limiter) — o SimHub decide tudo, o firmware só recebe RGB e acende.
- `SHCustomProtocol` do SimHub — não usado; a placa aparece como um
  dispositivo "Arduino" padrão, configurável inteiramente pela UI do
  SimHub.
- Matriz de botões — cada entrada tem canal/pino próprio.
