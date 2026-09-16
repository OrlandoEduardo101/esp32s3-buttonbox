🇧🇷 Português | [🇺🇸 English](HANDOFF.en.md)

# Button Box — contexto e handoff

## Objetivo
Button box USB para SimHub: HID Game Controller com 32 botões + encoders,
reconhecido no `joy.cpl` do Windows.

---

## Parte 1 — STM32F103 (abandonado). O que foi investigado

Projeto: `~/Projects/arduino/STM32-SimHub-ButtonBox` (PlatformIO + TinyUSB + stm32cube).
USB composto: HID gamepad (32 botões, Report ID 1) + CDC ACM.

**Sintoma:** o dispositivo enumera e aparece no `joy.cpl` com 32 botões, status OK,
mas NENHUM botão aciona — nem um heartbeat gerado pelo próprio firmware que alterna
o botão 31 a cada 1 s. A CDC funcionava (PING/PONG respondendo).

### Medições feitas por SWD (ST-Link + OpenOCD, com a CPU rodando)

- O firmware **lê o pino e monta o report certo**: `s_report_work` com bit0=1 ao
  fechar o jumper GND→PB9.
- `send_count` **incrementa** (reports aceitos e transmitidos pela pilha USB).
- Endpoint HID IN (a TinyUSB mapeou no `EP1R` com `EA=3`): **`STAT_TX=VALID` travado**
  — o report fica carregado no buffer e o host **para de mandar token IN**.
- Clock **exatamente 48 MHz**: `RCC_CR` com HSERDY+PLLRDY, `RCC_CFGR` com PLL×9 e
  USBPRE=/1.5.
- IRQ do USB habilitada no NVIC, sem interrupção presa, barramento ativo (SOF),
  sem suspend.
- Report descriptor lido do binário — livro-texto, sem erro:
  `05 01 09 05 A1 01 85 01 05 09 19 01 29 20 15 00 25 01 75 01 95 20 81 02 C0`
- R10 (pull-up de D+) medido pelo usuário: **1,5 kΩ** (correto).
- **O número de reports entregues antes de travar é ALEATÓRIO**: 7, 8, 14, 23, 68,
  90, 337. Não é limite determinístico — é falha intermitente.

### Tentativas que NÃO resolveram

1. Pulso de desconexão em D+ (PA12 low 50 ms) no boot. *Resolveu outro problema
   real* — sem ele, após gravar por SWD o host não reenumera e o dispositivo fica
   num estado obsoleto (`cfg=0` travado) até replug físico. Verificado: com o pulso,
   `cfg` vai a 1 sozinho. Mas não consertou os botões.
2. Implementar `tud_hid_get_report_cb` (era stub retornando 0).
3. Remover o Report ID do descriptor e enviar sem prefixo.
4. Adicionar eixo dummy X/Y — o `joy.cpl` passou a mostrar "2 eixos e 32 botões",
   mas continuou sem acionar botão.
5. Trocar o PID várias vezes (0007→000A) para derrotar o cache de descriptor do Windows.
6. Reduzir a taxa de polling: `bInterval` 8→32 ms deu falha idêntica; 64 ms fez o
   Windows **desconfigurar** o dispositivo (`cfg=0`).
7. Build **HID-only** (sem CDC, sem IAD, 1 interface): **falha idêntica** — portanto
   a composição com CDC não é a variável.

### Causa provável: o MCU é clone

Lido por SWD:
- `DBGMCU_IDCODE = 0x20030410`
- Registrador de tamanho de flash = **128 KB** numa peça vendida como C8 (64 KB)
- **UID de 96 bits em `0x1FFFF7E8` = TODOS ZERO**

STM32F103 genuíno da ST sempre tem UID único de fábrica. UID zerado = CKS32/CS32/APM32.
O padrão observado (endpoints **bulk** da CDC funcionam, endpoint **interrupt IN** do
HID nunca sustenta) é compatível com quirk de USB desses clones.

### Estado do projeto STM32
**Revertido ao original** a pedido do usuário, e o firmware composite original foi
regravado na placa. Nenhuma alteração experimental permaneceu.

---

## Parte 2 — Decisão de trocar de placa

Restrição dura levantada antes de investir tempo:
- **ESP32-C3 / C6** (o "Super Mini" mais vendido): USB nativo é *USB Serial/JTAG* de
  função fixa → só CDC. **Não faz HID.** Caminho morto.
- **ESP32-S2 / S3**: USB OTG completo → TinyUSB → **HID funciona**.

Confirmado por esptool que a placa do usuário é **ESP32-S3 (QFN56) rev v0.2, 4 MB
flash XMC, 2 MB PSRAM quad, cristal 40 MHz**.

Fator decisivo: o usuário **já tinha código de HID gamepad funcionando** em
`~/Projects/arduino/ESP-SimHub-ESP32S3-SCREEN/src/main_buttons.cpp`
(`USBHIDGamepad`, 32 botões + 4 eixos).

### Resultado
Projeto de teste: `~/Projects/arduino/esp32s3-buttonbox-test`

**HID FUNCIONA.** macOS anexou o driver (`PrimaryUsage=5` = Gamepad) e o
`InputReportCount` sobe continuamente (126→134→143). No Windows o Botão 32 pisca
no `joy.cpl`. Contraste direto com o STM32.

---

## Armadilhas do ESP32-S3 descobertas (importante)

1. **`ARDUINO_USB_MODE=0` é obrigatório para HID** (USB-OTG/TinyUSB). Com `=1` o core
   usa o USB-Serial-JTAG de hardware, que **não faz HID**. O board JSON força `=1`,
   então precisa ser sobrescrito via `board_build.extra_flags` — `build_flags` sozinho
   não vence.
   > **Achado para o projeto ESP-SimHub:** o env `dingyimei-s3-zero` está com
   > `ARDUINO_USB_MODE=1` enquanto o `main_buttons.cpp` usa `USBHIDGamepad`.
   > **Nesse modo o HID não funciona.**
2. **Board definition importa:** usar `esp32-s3-devkitc-1` (8 MB, sem PSRAM) num chip
   de 4 MB causou **BOOT LOOP** (tabela de partição não cabia). O correto é o board
   customizado `esp32-s3-fh4r2` (4 MB + 2 MB PSRAM quad, `partitions=default.csv`),
   que já existe em `ESP-SimHub-ESP32S3-SCREEN/boards/`.
3. Após reset do esptool a placa fica em **modo DOWNLOAD** (`boot:0x20`); só um
   **ciclo de energia físico** boota o app.
4. Com `MODE=0` o USB-Serial-JTAG some, então o esptool **não consegue auto-resetar**
   pelo CDC do TinyUSB → gravação por cabo exige segurar BOOT.
   `upload_speed=115200` **não resolve** (testado).
5. **WiFiManager:** `autoConnect()` reabre o portal a cada falha e a placa fica presa
   em modo AP para sempre. Melhor: conectar com credencial salva/fixa e reinsistir
   sozinho; abrir o portal só sob pedido explícito.
6. **`WiFi.setSleep(false)` só DEPOIS de `WiFi.begin()`.** Chamado antes da escolha de
   modo, inicializa o rádio em STA e impede o AP de subir.
7. **Não escrever na Serial durante o OTA** — escrita no CDC do TinyUSB bloqueia
   quando o host não está lendo, travando o loop.
8. **`espota` não resolve mDNS `.local`** — precisa do IP.

---

## Estado atual do projeto ESP32-S3

Funcionando (verificado):
- HID gamepad (Botão 32 heartbeat, Botão 1 = BOOT da placa)
- WiFi conectando sozinho (`Orlando_tplink`, `192.168.0.117`, RSSI −64)

Falhando (verificado):
- **OTA**: transfere 100% e quebra na verificação com **`Flash Read Failed`**
- **Gravação USB sem botão BOOT**

Correções preparadas mas **NÃO testadas** (a placa foi desconectada):
- `-DBOARD_HAS_PSRAM` — hipótese para o `Flash Read Failed`: o board usa
  `memory_type=qio_qspi` (flash + PSRAM); sem declarar a PSRAM, a releitura da flash
  na verificação do OTA falha. O env equivalente do ESP-SimHub, que funciona, tem a flag.
- Script `enter_bootloader.py` sem toggle de DTR/RTS (abrir a porta resetava a placa
  antes de ela ler o comando `BOOTLOADER`, que força modo download por software via
  `RTC_CNTL_FORCE_DOWNLOAD_BOOT`).

## Próximo passo de valor real
Portar a lógica do `main_buttons.cpp` (matriz 5×4 + 4 encoders) para o projeto de
teste, adaptando a pinagem para a SuperMini.
