🇧🇷 Português | [🇺🇸 English](SYSTEM_INTEGRATION.en.md)

# SYSTEM_INTEGRATION.md — Integração final do sistema

> Gerado em 2026-09-15. Cobre a arquitetura final pedida, a auditoria das
> 12 regras contra o código real (não por memória — cada uma foi
> verificada por leitura/grep do código nesta sessão) e o plano de teste
> completo. **Hardware físico (MCP23017, 74HC4067, encoders, matriz+fita
> WS2812) ainda não está montado** — por isso a parte de hardware do teste
> completo pedido não pôde ser *executada*, só preparada e documentada
> como checklist. Isso é dito explicitamente em vez de assumido como
> feito.

## Arquitetura final (como pedida, com o que foi adicionado nesta etapa)

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
        │           WS2812   <- NOVO nesta etapa (lib/ws2812, via RMT)
        │
        ▼
      INPUTS
        │
   ┌────┴─────────────┐
   │                  │
ESP32 GPIO        Expansores
   │              ┌────┴────┐
   │              │         │
Encoders       MCP23017   74HC4067
   │              │         │
   └──────────────┴─────────┘
                  │
            controles físicos
```

O único componente que faltava pra fechar este diagrama era o driver
WS2812 (RGB framebuffer → fita física) — tudo o resto (HID, CDC, WiFi,
OTA, SimHub, inputs, MCP23017, 74HC4067, encoders) já existia de etapas
anteriores e não foi reescrito, só verificado.

## Novo nesta etapa

- **`lib/ws2812/`** — driver de saída para a matriz 8x8 + fita (~10 LEDs),
  via periférico RMT da ESP32-S3 (`rmtInit`/`rmtWrite`, mesmo timing de
  bit já usado e validado pela Espressif em `neopixelWrite()` do core,
  generalizado de 1 pixel para N). Não bloqueia — `rmtWrite()` é
  assíncrono, a transmissão real roda em hardware depois que a função
  retorna. Não conhece SimHub nem telemetria (regra 9) — tem seu próprio
  tipo `Ws2812Color`.
- **GPIO1** — nova decisão de pinout: saída de dados WS2812 (estava livre,
  documentado em `docs/INPUTS_PINOUT.md`).
- **`src/main.cpp`** — a única ponte entre `lib/simhub` e `lib/ws2812`
  (glue de ~10 linhas no `loop()`: lê o framebuffer do SimHub, converte
  pro tipo do driver, manda pra fita). Nem simhub nem ws2812 se conhecem.
- **`src/ws2812_test.cpp`** + `[env:ws2812-test]` — isolado, varre cores
  fixas (vermelho/verde/azul/branco/apagado) pra validar o canal RMT sem
  depender do SimHub.

## Pinout centralizado — `include/board_config.h`

Adicionado em 2026-09-16, a pedido explícito: o usuário escolheu uma
ESP32-S3 bem pequena por ter os expansores disponíveis, mas quer poder
trocar de placa (uma maior, ou outra variante) no futuro, e também deixar
o projeto reaproveitável por outras pessoas com pinout diferente.

**Antes**: cada driver (`lib/mcp23017`, `lib/mux4067`, `lib/encoders`,
`lib/ws2812`) tinha seus próprios pinos default, e `lib/inputs`/`main.cpp`
simplesmente usavam esses defaults sem passar nada explícito — pra mudar
um pino era preciso editar o `.h` de dentro da lib.

**Depois**: `include/board_config.h` é o único arquivo com pino/canal/
endereço deste projeto. `lib/inputs/inputs.cpp` e `src/main.cpp` leem os
valores de lá e passam explicitamente pra cada `_init()`. Os drivers de
baixo nível continuam com seus próprios defaults (agora só usados se
alguém pegar uma lib sozinha, fora deste projeto, sem `board_config.h`) —
mantendo cada driver genérico e reutilizável independentemente.

Mudança puramente estrutural, sem efeito em runtime: RAM/Flash do
`esp32s3-supermini` idênticos antes/depois (89624 B / 911849 B).

Detalhe de build necessário: o diretório `include/` do PlatformIO não é
visível por padrão para bibliotecas privadas em `lib/` (só para `src/`) —
foi preciso adicionar `-I include` ao `build_flags` dos envs que
`#include board_config.h` transitivamente (`esp32s3-supermini`, que o
`ota` herda via `extends`, e `inputs-test`).

Pra trocar de placa/pinout: edite só `include/board_config.h`. Nenhum
outro arquivo precisa mudar (a menos que a própria placa exija uma
`board.json` diferente — ver `boards/esp32-s3-fh4r2.json`, que é sobre o
chip/flash/PSRAM, não sobre os pinos de entrada/saída).

## Auditoria das 12 regras (verificada por grep/leitura, não por memória)

| # | Regra | Verificação | Resultado |
|---|---|---|---|
| 1 | HID funciona sem SimHub | `Gamepad.send()` no fim do `loop()` roda incondicionalmente; nada em `simhub.*` é lido pelo bitmask de botões | ✅ |
| 2 | SimHub funciona sem alterar HID | `grep "Gamepad\|USBHID" lib/simhub/*` → nenhuma ocorrência | ✅ |
| 3 | OTA funciona sem quebrar HID | Bloco de OTA (`ArduinoOTA.handle()`, callbacks) intocado desde a baseline; `if (otaRunning) return;` já existia e segue protegendo o resto (inputs/simhub/ws2812 incluídos) | ✅ |
| 4 | OTA não depende da CDC sendo lida | OTA é 100% WiFi (UDP/TCP); `grep "Serial.read\|Serial.available"` dentro de `startOta()`/callbacks → nenhuma ocorrência | ✅ |
| 5 | MCP23017 não acessa HID diretamente | `grep "Gamepad\|USBHID" lib/mcp23017/*` → nenhuma ocorrência | ✅ |
| 6 | 74HC4067 não acessa HID diretamente | `grep "Gamepad\|USBHID" lib/mux4067/*` → nenhuma ocorrência | ✅ |
| 7 | Encoders não acessam HID diretamente | `grep "Gamepad\|USBHID" lib/encoders/*` → nenhuma ocorrência | ✅ |
| 8 | SimHub não conhece GPIO | `grep "pinMode\|digitalRead\|digitalWrite" lib/simhub/*` → nenhuma ocorrência (só usa `Serial`) | ✅ |
| 9 | LED driver não conhece telemetria | `lib/ws2812` não inclui `simhub.h`; tipo de cor próprio (`Ws2812Color`); ponte fica só em `main.cpp` | ✅ |
| 10 | Sem matriz de botões | Nenhum scan de linha/coluna em nenhum módulo; "matriz" no código só se refere à matriz física de LEDs 8x8 (saída, não entrada) | ✅ |
| 11 | Sem delays bloqueantes | Ver tabela detalhada abaixo — todo `delay()`/`delayMicroseconds()` do projeto é pontual/documentado, nenhum novo introduzido nesta etapa | ✅ (com ressalvas já conhecidas, não novas) |
| 12 | Sem refatoração desnecessária no USB validado | `[env:esp32s3-supermini]` no `platformio.ini` (flags de USB, `USB_PRODUCT`/`USB_MANUFACTURER`, board) idêntico; `USB.h`/`USBHIDGamepad`/`Gamepad.begin()`/`USB.begin()` não tocados | ✅ |

### Detalhe da regra 11 — inventário completo de `delay()`

| Local | Duração | Quando roda | Já existia antes desta etapa? |
|---|---|---|---|
| `main.cpp`, comando `BOOTLOADER` | 100ms | Só ao reiniciar pra modo download (ação explícita do usuário) | Sim (etapa 5) |
| `main.cpp`, `setup()` | 300ms | Só no boot, antes do `loop()` começar | Sim (baseline original) |
| `main.cpp`, fim do `loop()` | 5ms | Toda iteração — é o ritmo geral do laço principal | Sim (baseline original) |
| `mux4067_init()` | 30µs × canais em uso | Só uma vez, na inicialização | Sim (etapa do 74HC4067) |
| `simhub_process_packet()` (`readByteUntil`) | até 100ms **no total**, não por byte | Só quando um frame SimHub já começou (cabeçalho 6×0xFF visto) e trava no meio | Sim (etapa do SimHub) — **não é `delay()`**, é um busy-wait com orçamento de tempo, documentado como decisão de engenharia em `docs/SIMHUB_PROTOCOL.md` |

Nenhum desses é novo nesta etapa. `ws2812_show()` (novo) explicitamente
**não** bloqueia — usa `rmtWrite()` assíncrono, não `rmtWriteBlocking()`.

### Risco residual conhecido (não corrigido, por "não refatorar desnecessariamente")

Durante a janela em que o WiFi está conectado e o OTA já está pronto
(`otaReady=true`) mas nenhuma transferência está em andamento
(`otaRunning=false`), um frame SimHub corrompido/travado no meio pode
segurar `serialCommands()` por até 100ms antes do próximo
`ArduinoOTA.handle()`. Isso não quebra OTA (a regra 4 é sobre
*dependência*, não sobre latência, e o handshake do OTA por rede tolera
bem mais que 100ms de jitter) mas é um acoplamento de latência real entre
as duas camadas. Não alterado agora porque (a) é um caso raro
(SimHub tem que estar mandando dado corrompido/incompleto e nunca
completar), (b) qualquer mudança seria uma refatoração do parser SimHub
já validado no teste anterior, e a regra 12 pede pra não refatorar sem
necessidade comprovada. Registrado aqui para se algum dia o comportamento
de OTA parecer "engasgado" com o SimHub conectado.

## Números finais (compilados nesta sessão)

| Env | RAM | Flash |
|---|---|---|
| `esp32s3-supermini` (produção completa) | 90264 B (27,5%) | 909481 B (69,4%) |
| `ota` (mesmo binário, protocolo OTA) | idêntico ao acima | idêntico ao acima |
| `diag` | 18664 B (5,7%) | 261061 B (19,9%) |
| `mcp-test` | 19068 B (5,8%) | 281201 B (21,5%) |
| `mux-test` | 18760 B (5,7%) | 262981 B (20,1%) |
| `encoder-test` | 19448 B (5,9%) | 264065 B (20,1%) |
| `inputs-test` | 20152 B (6,1%) | 285373 B (21,8%) |
| `simhub-test` | 32492 B (9,9%) | 302649 B (23,1%) |
| `ws2812-test` | 43472 B (13,3%) | 261937 B (20,0%) |

O salto de RAM do binário de produção (63512B → 90264B, +26752B) vem quase
todo do buffer fixo do RMT (`WS2812_MAX_LEDS * 24 * 4 bytes` = 24576B) —
esperado e documentado em `lib/ws2812/ws2812.h`.

---

## Teste completo pedido — o que foi executado vs. o que fica pendente

### Executado nesta sessão (sem hardware — build e auditoria estática)

- [x] Compilação de **todos os 9 envs** (produção + 7 testes isolados +
  OTA) sem erro.
- [x] Auditoria das 12 regras de arquitetura (tabela acima).
- [x] Confirmação de que nenhum módulo novo é sequer *linkado* no binário
  de produção sem passar pela integração explícita em `main.cpp` (LDF
  "chain mode": só entra o que é `#include`ado).

### Pendente — precisa do hardware físico montado (não pôde ser executado)

Esta seção é a checklist definitiva. Cada linha diz **o que testar**, **o
critério de sucesso** e, se falhar, **qual env isolado usar pra achar a
causa** (regra do enunciado: "isolar a camada responsável antes de
modificar código").

| Teste | Critério de sucesso | Se falhar, isolar com |
|---|---|---|
| Cada push button (11) individualmente | Bit correspondente no `joy.cpl` acende/apaga exatamente ao apertar/soltar | `mcp-test` (lê direto do MCP23017, sem passar por `inputs`/HID) |
| SW dos 4 encoders | Idem, 4 botões (12-15) | `mux-test` (74HC4067 direto) |
| 4 encoders — CW/CCW | Cada detent físico gera exatamente 1 evento no sentido certo | `encoder-test` (decoder de quadratura isolado) |
| Freio de estacionamento | Estado acompanha a posição da alavanca | `mux-test` (canal C8) |
| Ignição (3 posições) | ON e IGN corretos, IGN só durante o crank (ver `docs/INPUTS_PINOUT.md` seção 2) | `mcp-test` (GPB4/GPB5) |
| Start Engine | Botão + LED (se já ligado) | `mcp-test` (GPB3) para o botão; LED é fiação separada, sem firmware |
| 4 chaves caça | Estado acompanha a posição da chave | `mux-test` (canais C4-C7) |
| Múltiplos botões simultâneos | Sem interferência entre bits (não deveria haver — não há matriz) | `inputs-test` (várias entradas ao mesmo tempo, ver o log) |
| HID no Windows | `joy.cpl` mostra os 31 controles reais + heartbeat no bit 32 | `docs/BASELINE.md` (teste original) + `docs/INPUTS_PINOUT.md` seção 10 (mapa de bits) |
| CDC | `PING`→`PONG`, `VERSION`, `IP`, `SETLEDS <n>` respondem | Qualquer terminal serial na porta COM |
| SimHub — protocolo | `proto`/`ledsc`/`sleds` respondem certo, RGB chega correto | `simhub-test` + `scripts/simhub_test_send.py` |
| LEDs (WS2812) | Cores aparecem certas na matriz/fita, sem flicker nem cor trocada (R/G/B) | `ws2812-test` (varredura de cores fixas, sem SimHub) |
| WiFi | Conecta sozinho, reconecta sozinho, portal só com BOOT 5s | `docs/BASELINE.md` (já validado antes, não mexido) |
| OTA | Upload completo via `pio run -e ota -t upload`, HID continua funcionando durante e depois | `docs/BASELINE.md` (5 ciclos já validados antes desta integração) |
| **HID + SimHub juntos** | Board conectado no Windows como HID **e** no SimHub ao mesmo tempo, sem um atrapalhar o outro | Se falhar, isolar: primeiro `simhub-test` sozinho (confirma que o parser funciona), depois o binário completo — se só o completo falha, o problema está na convivência (`serialCommands()` despachando pros dois protocolos), não em nenhum parser individual |

### Ordem de bring-up recomendada (minimiza risco de não saber qual camada falhou)

1. `mcp-test` — só o MCP23017, sem mais nada.
2. `mux-test` — só o 74HC4067.
3. `encoder-test` — só os 4 KY-040.
4. `inputs-test` — os três juntos, via a API unificada.
5. `ws2812-test` — só a fita/matriz, cores fixas.
6. `simhub-test` — só o protocolo, com `scripts/simhub_test_send.py`.
7. `esp32s3-supermini` (produção completa) — tudo junto. Se algo que
   funcionou isolado falhar aqui, o problema é de **integração**
   (contenção de recurso, timing, memória), não do módulo em si — volte
   pra tabela acima pra saber por onde recomeçar a isolar.
