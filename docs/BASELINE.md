🇧🇷 Português | [🇺🇸 English](BASELINE.en.md)

# BASELINE.md — Testes que comprovadamente passaram

> Checklist de regressão para este firmware. Cada item abaixo foi observado
> funcionando, com a evidência descrita. Se uma mudança futura quebrar algum
> destes, é regressão — compare com este documento antes de investigar do
> zero. Gerado em 2026-09-15, referente ao estado de código descrito em
> `docs/ARCHITECTURE.md`.

## Ambiente de build verificado

```
$ pio run -e esp32s3-supermini
PLATFORM: Espressif 32 (6.12.0) > ESP32-S3-FH4R2 (Dingyimei S3 Zero)
HARDWARE: ESP32S3 240MHz, 320KB RAM, 4MB Flash
PACKAGES:
 - framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b
 - tool-esptoolpy @ 2.40900.250804 (4.9.0)
 - toolchain-xtensa-esp32s3 @ 8.4.0+2021r2-patch5
RAM:   18.9% (61768 / 327680 bytes)
Flash: 67.2% (881097 / 1310720 bytes)   <- dentro da partição OTA de 1.25 MB
[SUCCESS]
```
Placa de referência: ESP32-S3 (QFN56) rev v0.2, 4 MB flash XMC, 2 MB PSRAM
quad, cristal 40 MHz, MAC `AC:27:6E:CC:FA:B8`.

---

## 1. Compila sem erros

`pio run -e esp32s3-supermini` termina com `[SUCCESS]`, RAM/Flash dentro dos
limites da partição (ver acima). Reexecutado nesta auditoria (2026-09-15) —
build limpo, sem warnings de flags conflitantes.

## 2. HID reconhecido no Windows

- Dispositivo aparece no `joy.cpl` como `ESP32S3-SimHub-ButtonBox`, status OK.
- **32 botões**, **4-6 eixos** listados (X, Y, Z, Rotação X/Y/Z, todos parados
  em zero — esperado, ver `ARCHITECTURE.md` item 7).
- **Botão 32 pisca sozinho** a cada ~1 s (heartbeat de software, sem
  nenhuma fiação conectada).
- Evidência: captura de tela do usuário mostrando exatamente esse estado.

## 3. Botão 1 = BOOT físico — **DESATUALIZADO, não vale mais**

Válido só até a integração da camada `lib/inputs` (ver
`docs/SYSTEM_INTEGRATION.md`): o BOOT **não alimenta mais nenhum bit do
HID** — era um valor simulado de bring-up, substituído pelo
`INPUT_BUTTON_01` real (MCP23017). O botão físico BOOT continua existindo
só para abrir o portal de WiFi (segurar 5s). Mantido aqui riscado, em vez
de apagado, pra não perder o histórico de por que existia.

## 4. CDC (porta COM) responde

Serial Monitor (VS Code / `pio device monitor`, 115200 baud) mostra as linhas
de status periódicas (`[status] HID ok | WiFi ip=... rssi=... | OTA=pronto`)
e responde aos comandos:
- `PING` → `PONG`
- `VERSION` → `ESP32S3_BUTTONBOX_HID_OTA`
- `IP` → IP atual
Evidência: sessão de Serial Monitor capturada pelo usuário em porta COM real
do Windows, simultânea ao HID funcionando (prova de que HID + CDC coexistem
no mesmo dispositivo composto).

## 5. WiFi conecta e reconecta sozinho

Placa conecta à rede configurada em `secrets.h` (SSID `Orlando_tplink` na
sessão de referência), reporta IP e RSSI no status serial. Reconecta sozinha
após queda, sem nunca abrir o portal de configuração por conta própria
(comportamento anterior, corrigido, que prendia a placa em modo AP).

## 6. OTA — 5 ciclos consecutivos bem-sucedidos

Após a correção de `-DBOARD_HAS_PSRAM` (que eliminava o antigo
`Flash Read Failed` na verificação pós-upload), **5 uploads OTA consecutivos**
completaram com sucesso (`pio run -e esp32s3-ota -t upload`), sem nenhuma
falha de verificação. Este é o teste de regressão mais importante para
qualquer mudança em `platformio.ini` ou nas flags de build.

## 7. USB flash sem segurar o botão BOOT

`scripts/enter_bootloader.py` (roda como `pre:` do env `esp32s3-supermini`)
consegue colocar a placa em modo download sozinho, via o comando serial
`BOOTLOADER`, sem intervenção manual no botão. Confirmado funcionando de
ponta a ponta.

## 8. Comando `BOOTLOADER` não prende mais a placa

Este é o teste mais recente e mais crítico desta baseline. Sequência
verificada manualmente:
1. Enviar `BOOTLOADER` pela serial com o firmware corrigido rodando.
2. Placa entra em modo download (confirmado: nova porta aparece,
   `device list` mostra `Description: USB JTAG/serial debug unit`,
   `VID:PID=303A:1001`).
3. Gravar um firmware novo por essa porta.
4. **Placa volta a rodar o app sozinha**, sem nenhum ciclo de energia físico
   — `device list` volta a mostrar `Description: ESP32S3-SimHub-ButtonBox`.

Antes da correção (troca de `REG_WRITE(RTC_CNTL_OPTION1_REG, ...)` por
`usb_persist_restart(RESTART_BOOTLOADER)`), este mesmo teste prendia a placa
permanentemente em bootloader — só um power-cycle físico (retirar a
alimentação por completo) resolvia. **Qualquer regressão que volte a usar o
registrador RTC diretamente reintroduz este bug.**

## 9. Protocolo SimHub — reconhecido pelo próprio SimHub (aba Arduino)

**Substitui a versão anterior deste item**, que documentava evidência do
protocolo antigo (`proto`/`ledsc`/`sleds`, descartado — ver
`docs/SIMHUB_PROTOCOL.md`, seção "Erro anterior"). A evidência atual é do
protocolo real (transporte ARQ + comandos), testado em 2026-09-16 direto
no SimHub do usuário (não só via `scripts/simhub_test_send.py`), com o
firmware de produção já integrado:

- Aba **Arduino** do SimHub mostrou o dispositivo como **`Connected`**
  (antes: `Unrecognized`/`Port not scanned` — ver o histórico de correções
  em `docs/SIMHUB_PROTOCOL.md`).
- **Connected device informations**: `Device name = ESP32S3-ButtonBox`,
  `Firmware Revision = j`, `Features list = NIXR`, `RGB Leds = 10`,
  `RGB Matrix = True`, `Unique Id = AC276ECCFAB8`.
- **Communication statistics**: `FPS ≈ 55-58`, **`Corrupted = 0`**,
  **`Reemited = 0`**, `Reemited af. wait = 0` — confirma que o transporte
  ARQ (checksum CRC8 + confirmação por pacote) está saudável, não só que
  o handshake inicial funcionou.
- Evidência: captura de tela do usuário na aba Arduino do SimHub.

Também confirmado nessa sessão: a contagem de LEDs da fita configurada via
`SETLEDS` (persistida em NVS) **sobreviveu a um upload OTA** entre um
teste e outro — a partição NVS não é afetada por atualização OTA.

Pendente: acender os LEDs físicos de verdade e configurar os efeitos
dentro do SimHub (matriz+fita ainda não montadas fisicamente no momento
deste teste) — próximo passo, não falha deste teste.

---

## Problema conhecido, não corrigido nesta baseline

- **PlatformIO às vezes autodetecta a porta serial errada** durante upload
  USB (observado escolhendo `/dev/cu.G900A`, que era o celular do usuário
  conectado ao Mac, não a placa). Contorno atual: os scripts em
  `scripts/` já filtram por MAC (`custom_expected_mac`/`custom_board_mac`),
  o que resolve a maioria dos casos; quando falha mesmo assim, passar
  `--upload-port` explicitamente ou usar OTA em vez de USB. **Não foi
  investigado a fundo nem corrigido** — fora de escopo desta auditoria
  (documentação apenas, sem mudança de código).

---

## Guia rápido para um novo desenvolvedor

**1. Compilar**
```
cd ~/Projects/arduino/esp32s3-buttonbox-test
pio run -e esp32s3-supermini
```

**2. Gravar (primeira vez, ou quando USB estiver disponível)**
```
pio run -e esp32s3-supermini -t upload
```
Não precisa segurar nenhum botão — `scripts/enter_bootloader.py` cuida disso
sozinho enviando o comando `BOOTLOADER` pela CDC.

**3. Conectar**
Plugar a placa via USB-C. Ela deve aparecer no Windows como dois dispositivos:
um controlador de jogo (`joy.cpl`) e uma porta COM.

**4. Verificar HID**
Abrir `joy.cpl` no Windows → deve aparecer `ESP32S3-SimHub-ButtonBox`, 32
botões, status OK, com o **Botão 32 piscando sozinho** a cada segundo (não
precisa de nenhuma fiação para este teste). Segurar o botão físico BOOT da
placa deve acender o Botão 1.

**5. Verificar CDC**
```
pio device monitor -e esp32s3-supermini
```
(ou qualquer terminal serial na porta COM correspondente, 115200 baud).
Digitar `PING` e apertar Enter deve responder `PONG`. Deve também aparecer
uma linha de status a cada 5 segundos.

**6. Fazer OTA**
Com a placa já rodando o firmware e conectada ao WiFi (`secrets.h`
preenchido com as credenciais corretas):
```
pio run -e esp32s3-ota -t upload
```
O script `scripts/ota_port_by_mac.py` acha o IP da placa sozinho pelo MAC.
Acompanhar o Serial Monitor antes de desconectar: deve mostrar
`[OTA] iniciando` e depois `[OTA] concluido`.

**Critério de sucesso desta baseline**: qualquer mudança futura no firmware
deve continuar passando em todos os 8 testes acima, sem exceção, antes de ser
considerada pronta.
