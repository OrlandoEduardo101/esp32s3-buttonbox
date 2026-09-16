# ESP32-S3 SimHub Button Box

Button box USB para sim racing: HID gamepad nativo (32 botões), WiFi + OTA,
e integração com o SimHub (aba "Arduino") pra controlar uma matriz 8x8 de
LEDs endereçáveis (iFlag) e uma fita de LEDs (RPM), com todos os efeitos
configurados dentro do próprio SimHub — nenhum comportamento de jogo é
escrito neste firmware.

## Hardware

| Componente | Função |
|---|---|
| ESP32-S3 (qualquer variante com USB-OTG nativo) | MCU — HID, WiFi, OTA, protocolo SimHub |
| MCP23017 (I2C) | 11 push buttons, ignição (3 posições), botão Start Engine |
| 74HC4067 (mux 16 canais) | SW dos 4 encoders, 4 chaves "caça", freio de estacionamento |
| 4× encoder KY-040 | CLK/DT direto na MCU (quadratura por interrupção); SW pelo mux |
| Matriz WS2812 8x8 (64 LEDs) | RGB Matrix do SimHub — ex.: iFlag |
| Fita WS2812 (~10 LEDs, configurável) | RGB Leds do SimHub — ex.: RPM |

**Não usa matriz de botões** (nem física nem de varredura) — cada entrada
tem seu próprio canal/pino, priorizando simplicidade de fiação e
confiabilidade sobre economia extrema de GPIO.

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
