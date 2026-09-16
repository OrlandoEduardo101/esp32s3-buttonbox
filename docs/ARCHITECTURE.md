🇧🇷 Português | [🇺🇸 English](ARCHITECTURE.en.md)

# ARCHITECTURE.md — Estado atual verificado (baseline pós-etapa 5)

> Este documento descreve o firmware **exatamente como ele existe agora**, sem
> nenhuma alteração de código feita durante esta auditoria. Todo fato aqui é
> verificado por leitura direta do código-fonte deste projeto, do core
> Arduino-ESP32 instalado (`~/.platformio/packages/framework-arduinoespressif32`)
> e/ou de `compile_commands.json` (macros efetivamente usadas na última
> compilação) — não por suposição nem só pela tela do Windows (que pode ter
> cache de driver, como já vimos no passado).
>
> Gerado em 2026-09-15. Se o código mudar depois desta data, este documento
> pode ficar desatualizado — trate-o como uma fotografia, não como fonte viva.

---

## 1. Como o dispositivo aparece no Windows

Ao conectar via USB, o Windows enumera **um único dispositivo USB composto**
com duas funções (interfaces), reconhecidas por dois drivers diferentes:

- **`joy.cpl` (Controladores de jogo)**: aparece como `ESP32S3-SimHub-ButtonBox`,
  status OK, com **32 botões** e **4 eixos** (X, Y, Z, Rotação Z, Rotação X,
  Rotação Y — 6 eixos declarados no descriptor, mas o `main.cpp` só usa o
  bitmask de botões; eixos ficam sempre em 0). O Botão 32 pisca sozinho
  (heartbeat).
- **Porta COM (Gerenciador de Dispositivos → Portas)**: aparece com o mesmo
  nome de produto, atribuída pelo driver serial padrão do Windows (`usbser.sys`),
  usável em qualquer terminal serial (VS Code Serial Monitor, PuTTY, etc.) a
  115200 baud.

Confirmado por captura de tela real do usuário (joy.cpl com 32 pontos, botão
32 aceso) e por sessão de Serial Monitor respondendo aos comandos de texto.

## 2. VID/PID

- **VID = `0x303A`** (Espressif Systems) — fixo, vem do core (`USB_ESPRESSIF_VID`
  em `esp32-hal-tinyusb.h`), nunca sobrescrito neste projeto.
- **PID = `0x1001`** — vem de `variants/esp32s3/pins_arduino.h`
  (`#define USB_PID 0x1001`), que é incluído **antes** do fallback do
  `USB.cpp` (`#ifndef USB_PID #define USB_PID 0x0002`). Como o board.json
  deste projeto usa `"variant": "esp32s3"`, o define do variant vence e o
  app enumera como **`303A:1001`**.

  > **CORREÇÃO (2026-09-16).** A versão anterior deste documento afirmava
  > que o PID era `0x0002` e que qualquer `303A:1001` visto em `device
  > list` seria "a interface USB-Serial-JTAG separada". **As duas
  > afirmações estavam erradas.** O erro veio de verificar só o
  > `compile_commands.json` (onde de fato não há `-DUSB_PID`) e concluir
  > dali, sem checar o header do variant. Consequência prática: ver
  > `303A:1001` numa porta **não** significa que a placa está em modo
  > bootloader — é o app rodando normalmente.

## 3. Manufacturer

`SimRacing_DIY` — definido explicitamente em `platformio.ini`:
```
-DUSB_MANUFACTURER=\"SimRacing_DIY\"
```
Sem essa flag, o core usaria o default `"Espressif Systems"`.

## 4. Product

`ESP32S3-SimHub-ButtonBox` — definido explicitamente em `platformio.ini`:
```
-DUSB_PRODUCT=\"ESP32S3-SimHub-ButtonBox\"
```
Sem essa flag, o core usaria como default a macro `ARDUINO_BOARD`, que o
PlatformIO injeta automaticamente a partir do campo `"name"` de
`boards/esp32-s3-fh4r2.json` (`"ESP32-S3-FH4R2 (Dingyimei S3 Zero)"`) — foi
exatamente esse vazamento que causava o nome estranho no `joy.cpl` antes desta
correção (já aplicada e confirmada, faz parte da baseline atual).

## 5. Serial Number

Não é fixo no código — é **derivado automaticamente do endereço MAC** do chip
em tempo de execução. Mecanismo (`USB.cpp`, `ESPUSB::begin()`):
```
USB_SERIAL default = "__MAC__"
  -> if (serial_number == "__MAC__"): lê esp_efuse_mac_get_default(),
     formata como "%02X%02X%02X%02X%02X%02X" (12 chars hex maiúsculos)
```
Para a placa usada nos testes (MAC `AC:27:6E:CC:FA:B8`), o serial observado é
`AC276ECCFAB8` — bate exatamente com a fórmula acima.

`platformio.ini` também usa esse MAC (`custom_expected_mac`,
`custom_board_mac`) para os scripts de auto-detecção de porta — ver itens 12
e 15.

## 6. Interfaces USB

Dispositivo composto TinyUSB com `ARDUINO_USB_MODE=0` (USB-OTG nativo) e
`ARDUINO_USB_CDC_ON_BOOT=1`:

- **Interface HID** — classe HID, um endpoint IN de interrupt. Relatórios de
  gamepad (ver item 7).
- **Interface CDC ACM** (2 sub-interfaces: Control + Data, como todo CDC
  padrão TinyUSB) — porta serial virtual, usada para os comandos de texto e
  logs (ver item 10).

Não há descriptor de composite escrito à mão neste projeto (diferente do
projeto STM32 anterior, onde `usb_descriptors.c` montava tudo manualmente) —
aqui o core Arduino-ESP32 monta o descriptor automaticamente a partir de quais
classes (`USB.begin()`, `USBHIDGamepad`, `USBCDC` via `ARDUINO_USB_CDC_ON_BOOT`)
são inicializadas em `setup()`.

## 7. HID report descriptor

Vem pronto do core Arduino-ESP32 (`libraries/USB/src/USBHIDGamepad.cpp`), via
o template padrão do TinyUSB:
```c
static const uint8_t report_descriptor[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(HID_REPORT_ID_GAMEPAD))
};
```
Nenhum byte desse descriptor é customizado neste projeto — é o template padrão
`TUD_HID_REPORT_DESC_GAMEPAD` do TinyUSB (`class/hid/hid_device.h`), com
**Report ID = 3** (`HID_REPORT_ID_GAMEPAD`, 4º valor do enum
`HID_REPORT_ID_NONE=0, KEYBOARD=1, MOUSE=2, GAMEPAD=3` em `USBHID.h`).

Layout do report (struct `hid_gamepad_report_t`, 8 bytes + 1 byte de Report ID):

| Campo     | Tamanho | Tipo   | Uso no firmware              |
|-----------|---------|--------|-------------------------------|
| Report ID | 1 byte  | —      | sempre `3`, implícito         |
| X         | 1 byte  | int8   | sempre `0` (não usado)        |
| Y         | 1 byte  | int8   | sempre `0` (não usado)        |
| Z         | 1 byte  | int8   | sempre `0` (não usado)        |
| Rz        | 1 byte  | int8   | sempre `0` (não usado)        |
| Rx        | 1 byte  | int8   | sempre `0` (não usado)        |
| Ry        | 1 byte  | int8   | sempre `0` (não usado)        |
| Hat/DPad  | 1 byte  | uint8  | sempre `0` (centro, não usado)|
| Buttons   | 4 bytes | uint32 | bitmask, bit N = botão N+1    |

`main.cpp` só usa o último campo:
```cpp
Gamepad.send(0, 0, 0, 0, 0, 0, 0, buttons);
//           x  y  z  rz rx ry hat  buttons
```
Os 6 eixos e o hat existem porque fazem parte do template fixo do TinyUSB —
não há como omiti-los sem escrever um descriptor customizado (o que este
projeto não faz). É por isso que o `joy.cpl` mostra eixos parados em zero.

## 8. Quantidade de botões atualmente expostos

**ATUALIZADO** desde a versão original deste documento (2026-09-15): a
camada unificada `lib/inputs` foi integrada ao HID, substituindo os
valores simulados. **32 botões** no descriptor (`uint32_t buttons`, bits
0–31, `HID_REPORT_COUNT(32)`, inalterado), dos quais:

- **Bits 0–30 (Botões 1–31)** = os 31 controles reais da Button Box
  (MCP23017 + 74HC4067 + 4× KY-040), via `lib/inputs`. Mapa completo
  (INPUT LOGICAL ID → bit) em `docs/INPUTS_PINOUT.md` seção 10.
- **Bit 31 (Botão 32)** = heartbeat de software, alterna a cada 1000 ms —
  **mantido de propósito** até os 31 controles reais acima estarem
  comprovadamente funcionando na bancada física (hardware ainda não
  montado no momento desta integração); depois disso deve ser removido.

O antigo "Bit 0 = botão BOOT da placa" foi removido — era um valor
simulado de bring-up, substituído pelo `INPUT_BUTTON_01` real (MCP23017).
O botão BOOT físico continua existindo só para abrir o portal de WiFi
(segurar 5s), sem mais nenhuma ligação com o HID.

## 9. Como o heartbeat do botão 32 funciona

Em `loop()`, sem depender de WiFi/OTA/interrupção alguma:
```cpp
if (now - lastBeatMs >= 1000) {
  lastBeatMs = now;
  beatOn = !beatOn;
  if (beatOn) buttons |= (1UL << 31);
  else        buttons &= ~(1UL << 31);
}
```
Alterna o bit 31 (Botão 32) a cada 1 segundo, puramente por software/`millis()`.
Serve como prova de vida do HID sem exigir nenhuma fiação — é o primeiro
teste a fazer em qualquer placa nova (ver `BASELINE.md`).

## 10. Como a CDC é usada

A CDC (porta COM) é usada só para um console de texto simples,
linha-a-linha, processado em `serialCommands()`:

| Comando      | Resposta / efeito                                            |
|--------------|----------------------------------------------------------------|
| `PING`       | responde `PONG`                                                |
| `VERSION`    | responde `ESP32S3_BUTTONBOX_HID_OTA`                            |
| `IP`         | responde o IP atual (`WiFi.localIP()`)                          |
| `BOOTLOADER` | responde `REBOOTING_TO_BOOTLOADER`, depois entra em modo download via `usb_persist_restart(RESTART_BOOTLOADER)` — ver item 15 sobre por que **não** é mais um `REG_WRITE` direto |

Também emite logs periódicos não-solicitados (`statusReport()`, a cada 5 s):
estado do WiFi/OTA/portal. **Durante upload OTA (`otaRunning == true`), a CDC
não é usada para nada** — nenhuma escrita acontece, de propósito (ver item 12).

Comandos são case-insensitive (convertidos para maiúsculo antes do `strcmp`),
terminados por `\n` (buffer de 32 bytes, `\r` ignorado).

## 11. Como o WiFi conecta

Modo estação (STA) sempre, credenciais fixas vindas de `include/secrets.h`
(`WIFI_SSID`, `WIFI_PASS` — arquivo git-ignorado, valores nunca expostos neste
documento):

```cpp
// setup()
WiFi.mode(WIFI_STA);
WiFi.setAutoReconnect(true);
WiFi.begin(WIFI_SSID, WIFI_PASS);
```

Se a conexão cair ou nunca subir, `wifiKeepAlive()` (chamado todo `loop()`,
mas throttled a 1 tentativa a cada 10 s) chama `WiFi.disconnect()` +
`WiFi.begin()` de novo, **para sempre, sem desistir e sem abrir portal
sozinho**. Isso é proposital (comentário no topo do `main.cpp`): uma versão
anterior abria o portal de configuração automaticamente a cada falha e a
placa ficava presa em modo AP, offline, sem nunca mais reconectar.

O portal de configuração (`WiFiManager`, para trocar de rede sem recompilar)
só abre por pedido explícito: segurar o botão BOOT por 5 segundos **durante a
operação normal** (não no boot — GPIO0 baixo no reset entra em modo download
e o app nem chega a rodar):
```cpp
if (bootDown && !portalAtivo && now - bootHeldSince >= 5000) {
  wm.setConfigPortalBlocking(false);
  portalAtivo = wm.startConfigPortal(AP_NAME);  // AP_NAME = "ButtonBox-Setup"
}
```

`WiFi.setSleep(false)` só é chamado dentro de `startOta()`, **depois** de
`WiFi.begin()` já ter sido chamado em `setup()` — chamar antes, com o rádio
ainda não inicializado em STA, impede o portal AP de subir (lição documentada
no topo do arquivo).

## 12. Como o OTA é iniciado

`ArduinoOTA` + `ESPmDNS`, inicializados uma única vez por `startOta()`,
chamado de `loop()` só quando há WiFi conectado:
```cpp
void loop() {
  if (WiFi.status() == WL_CONNECTED) startOta();
  if (otaReady) ArduinoOTA.handle();
  if (otaRunning) return;   // nada mais roda durante upload
  ...
}
```
`startOta()` é guardado por `otaReady` (só roda uma vez): registra hostname
(`OTA_HOSTNAME`, de `secrets.h`), callbacks de `onStart`/`onEnd`/`onError`, e
**propositalmente nenhum callback de `onProgress`** — escrever na CDC durante
o upload bloqueia quando o host não está lendo a porta, o que travava
`ArduinoOTA.handle()` e matava a transferência pela metade (era a causa do
antigo "Error Uploading" em ~29%, resolvida antes desta baseline).

Uma vez que o upload OTA começa (`otaRunning = true` no `onStart`), o
`loop()` retorna cedo logo depois de chamar `ArduinoOTA.handle()` — nenhuma
outra lógica (HID, serial, WiFi keep-alive) roda até o upload terminar.

## 13. Como o firmware é atualizado

Dois caminhos, ambos via o mesmo `platformio.ini`:

- **USB (cabo)**: `pio run -e esp32s3-supermini -t upload`. O
  `extra_scripts = pre:scripts/enter_bootloader.py` roda antes do upload:
  acha a porta da placa pelo MAC (`custom_expected_mac`), abre com
  `dtr=False, rts=False`, manda `BOOTLOADER\n` (o mesmo comando do item 10),
  espera até 10 s por uma nova porta em modo download, e reaponta
  `UPLOAD_PORT` para ela. **Não exige segurar o botão BOOT manualmente** —
  isso já era um problema resolvido antes desta baseline.
- **OTA (WiFi)**: `pio run -e esp32s3-ota -t upload` (env `[env:ota]`, que
  estende `esp32s3-supermini` com `upload_protocol = espota`). O
  `extra_scripts = pre:scripts/ota_port_by_mac.py` descobre o IP atual da
  placa pelo MAC (`arp -a` após um ping de broadcast) e substitui
  `UPLOAD_PORT` por ele — contorna o fato de o `espota` não resolver nomes
  `.local` (mDNS) e o IP poder mudar por DHCP.

Em ambos os casos, o binário sobe para a partição OTA ociosa (`ota_0`/`ota_1`,
1.25 MB cada, ver `default.csv`) e o bootloader alterna para ela no próximo
boot — não há necessidade de apagar a flash inteira.

## 14. Qual board/env do PlatformIO deve ser usado

- **Board**: `esp32-s3-fh4r2` (`boards/esp32-s3-fh4r2.json`, custom, **não** o
  `esp32-s3-devkitc-1` genérico) — 4 MB flash + 2 MB PSRAM quad
  (`memory_type=qio_qspi`), partições `default.csv`. Usar o devkit genérico
  (8 MB, sem esta PSRAM) causa **boot loop** nesta placa física (histórico,
  documentado em `HANDOFF.md`).
- **Env para desenvolvimento normal (compilar + gravar + rodar)**:
  `[env:esp32s3-supermini]` — o único com o firmware de produção
  (`src_filter = +<main.cpp>`), HID + CDC + WiFi + OTA completos.
- **Env `[env:diag]`**: firmware mínimo alternativo (`src/diag.cpp`,
  `ARDUINO_USB_MODE=1`, só imprime `"alive N"` a cada 500 ms). Existe só para
  isolar problemas de toolchain/board/gravação da lógica de HID — **não é o
  firmware do produto**, não expõe HID nenhum.
- **Env `[env:ota]`**: mesmo firmware de `esp32s3-supermini` (via `extends`),
  só troca o protocolo de upload para WiFi.

## 15. Quais arquivos são críticos e não devem ser alterados sem necessidade

| Arquivo | Por quê é crítico |
|---|---|
| `src/main.cpp` | Toda a lógica de HID, WiFi, OTA e console serial. Contém, em comentários no topo e ao redor do comando `BOOTLOADER`, o histórico de bugs já resolvidos (portal preso, `setSleep` antes do `begin`, escrita na CDC travando OTA, registrador RTC prendendo em bootloader) — remover esses comentários ou desfazer essas decisões reintroduz bugs já corrigidos. |
| `platformio.ini` | `board_build.extra_flags` com `ARDUINO_USB_MODE=0` é o que liga o HID (o board.json por padrão traz `MODE=1`, que **não faz HID**). `-DBOARD_HAS_PSRAM` é o que resolve o `Flash Read Failed` do OTA. `-DUSB_PRODUCT`/`-DUSB_MANUFACTURER` evitam o vazamento do nome do board.json pro descriptor USB. `upload_speed=115200` é proposital (velocidade alta falha o handshake do esptool sobre a CDC do TinyUSB). |
| `boards/esp32-s3-fh4r2.json` | Board definition customizada (4 MB flash + PSRAM); trocar por um board genérico já causou boot loop no passado. |
| `scripts/enter_bootloader.py` | Mecanismo que permite gravar por USB sem apertar o botão BOOT manualmente; depende do comando `BOOTLOADER` em `main.cpp` estar usando `usb_persist_restart()` (não o registrador RTC direto). |
| `scripts/ota_port_by_mac.py` | Mecanismo que permite o `espota` achar a placa sem IP fixo nem mDNS funcionando. |
| `include/secrets.h` | Credenciais de WiFi e hostname OTA (`WIFI_SSID`, `WIFI_PASS`, `OTA_HOSTNAME`). Git-ignorado — nunca deve ser commitado nem ter seu conteúdo impresso/logado. |
| `~/.platformio/packages/framework-arduinoespressif32` (core instalado) | Não é deste projeto, mas o comportamento de USB/HID/CDC descrito aqui depende da versão instalada (`espressif32@6.12.0` / `framework-arduinoespressif32@3.20017.241212`, ver `docs/BASELINE.md`). Uma atualização do core pode mudar defaults (ex.: `USB_PID`, layout do report HID). |

Arquivo **não** crítico e livre para uso exploratório: `src/diag.cpp` (env de
diagnóstico isolado, não afeta o firmware de produção).
