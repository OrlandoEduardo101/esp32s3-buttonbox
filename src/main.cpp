// Button box USB HID (ESP32-S3) + WiFi + OTA.
//
// Lições que este firmware incorpora, todas aprendidas na marra:
//
//  1. USB HID sobe PRIMEIRO. O button box tem que funcionar mesmo sem rede.
//  2. Sem portal cativo no caminho normal: as credenciais vêm de secrets.h e
//     o WiFi reinsiste sozinho para sempre. O portal só abre se você pedir
//     (segurando BOOT por 5 s) — antes ele abria sozinho a cada falha e a
//     placa ficava presa em modo AP, offline, sem reconectar nunca mais.
//  3. WiFi.setSleep(false) só DEPOIS de WiFi.begin(). Chamado antes, ele
//     inicializa o rádio em STA e impede o AP de subir. E tem que ser
//     REAPLICADO a cada reconexão (onWifiConnected) — aplicar uma vez só, no
//     startOta(), deixava o power save voltar sozinho depois de qualquer
//     queda de rede.
//  4. Economia de energia desligada: com ela o ping de LAN variava de 66 ms a
//     355 ms, e o OTA corrompia no meio da transferência.
//
// Botões 1-31: entradas reais da Button Box (MCP23017 + 74HC4067 +
// encoders), via a camada unificada lib/inputs — mapa completo logo
// abaixo. O botão BOOT da própria placa (GPIO0) NÃO alimenta mais nenhum
// bit do HID (isso era só um valor simulado de bring-up); ele continua
// servindo só para o gesto de abrir o portal de WiFi (segurar 5 s).
//
// Console na serial (COM do Windows): PING -> PONG, VERSION, IP
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include "USB.h"
#include "USBHIDGamepad.h"
#include "secrets.h"
#include "esp32-hal-tinyusb.h" // usb_persist_restart() — ver nota no comando BOOTLOADER
#include "esp_system.h"
#include <Preferences.h>   // NVS: persistir brilho dos LEDs entre boots
#include "inputs.h" // camada unificada de entradas (MCP23017+74HC4067+encoders)
#include "simhub.h" // protocolo Standard Serial do SimHub, sobre o mesmo CDC
#include "ws2812.h" // driver da fita/matriz WS2812 (RMT) — so desenha, nao conhece SimHub
#include "led_idle.h" // animacao de espera — so usada com o SimHub desconectado
#include "board_config.h" // mapa unico de pinos deste projeto — mude so aqui pra outra placa

USBHIDGamepad Gamepad;

// NVS: namespace "bbox", chave "brightness". Preferences e' thread-safe
// entre o loop e callbacks do WiFiManager (que roda em outra task), mas
// aqui so escrevemos do loop — sem risco de concorrencia.
static Preferences g_prefs;

// Carrega o brilho do NVS e aplica no driver; usado so no setup().
static void nvsLoadBrightness() {
  g_prefs.begin("bbox", /*readOnly=*/false);
  const uint8_t saved = g_prefs.getUChar("brightness", WS2812_BRIGHTNESS_DEFAULT_PCT);
  g_prefs.end();
  ws2812_set_brightness(saved); // grama automaticamente para 25-75
  Serial.printf("[brightness] carregado do NVS: %u%%\n", ws2812_get_brightness());
}

// Salva o brilho atual no NVS. So chama depois de ws2812_set_brightness().
static void nvsSaveBrightness() {
  g_prefs.begin("bbox", /*readOnly=*/false);
  g_prefs.putUChar("brightness", ws2812_get_brightness());
  g_prefs.end();
}


// Mapa explicito: INPUT LOGICAL ID (lib/inputs/inputs.h) -> bit do HID
// (uint32_t buttons do USBHIDGamepad). Regra unica e verificavel: o bit
// usado e o proprio valor numerico do InputId (0-30) — o enum ja e 0-based
// e sequencial, entao nao existe indireção nem tabela separada para
// desatualizar. Bit 31 (Botao 32) fica LIVRE — era do heartbeat de bring-up,
// removido depois que os 31 controles reais passaram a responder.
//
//   INPUT LOGICAL ID                 -> HID BUTTON (bit / Nº no joy.cpl)
//   INPUT_BUTTON_01                  -> bit 0  / Botao 1
//   INPUT_BUTTON_02                  -> bit 1  / Botao 2
//   INPUT_BUTTON_03                  -> bit 2  / Botao 3
//   INPUT_BUTTON_04                  -> bit 3  / Botao 4
//   INPUT_BUTTON_05                  -> bit 4  / Botao 5
//   INPUT_BUTTON_06                  -> bit 5  / Botao 6
//   INPUT_BUTTON_07                  -> bit 6  / Botao 7
//   INPUT_BUTTON_08                  -> bit 7  / Botao 8
//   INPUT_BUTTON_09                  -> bit 8  / Botao 9
//   INPUT_BUTTON_10                  -> bit 9  / Botao 10
//   INPUT_BUTTON_11                  -> bit 10 / Botao 11
//   INPUT_BUTTON_12 (SW encoder 1)   -> bit 11 / Botao 12
//   INPUT_BUTTON_13 (SW encoder 2)   -> bit 12 / Botao 13
//   INPUT_BUTTON_14 (SW encoder 3)   -> bit 13 / Botao 14
//   INPUT_BUTTON_15 (SW encoder 4)   -> bit 14 / Botao 15
//   INPUT_IGNITION_ON                -> bit 15 / Botao 16
//   INPUT_IGNITION_IGN               -> bit 16 / Botao 17
//   INPUT_START_ENGINE               -> bit 17 / Botao 18
//   INPUT_HANDBRAKE                  -> bit 18 / Botao 19
//   INPUT_KILL_SWITCH_01             -> bit 19 / Botao 20
//   INPUT_KILL_SWITCH_02             -> bit 20 / Botao 21
//   INPUT_KILL_SWITCH_03             -> bit 21 / Botao 22
//   INPUT_KILL_SWITCH_04             -> bit 22 / Botao 23
//   INPUT_ENCODER_01_CW              -> bit 23 / Botao 24  [pulso, nao nivel]
//   INPUT_ENCODER_01_CCW             -> bit 24 / Botao 25  [pulso, nao nivel]
//   INPUT_ENCODER_02_CW              -> bit 25 / Botao 26  [pulso, nao nivel]
//   INPUT_ENCODER_02_CCW             -> bit 26 / Botao 27  [pulso, nao nivel]
//   INPUT_ENCODER_03_CW              -> bit 27 / Botao 28  [pulso, nao nivel]
//   INPUT_ENCODER_03_CCW             -> bit 28 / Botao 29  [pulso, nao nivel]
//   INPUT_ENCODER_04_CW              -> bit 29 / Botao 30  [pulso, nao nivel]
//   INPUT_ENCODER_04_CCW             -> bit 30 / Botao 31  [pulso, nao nivel]
//   (livre)                          -> bit 31 / Botao 32
//
// HID report descriptor NAO foi alterado (continua uint32_t buttons via
// USBHIDGamepad, 32 bits, sem eixos/hat usados) — os 31 IDs cabem no
// espaço que ja existia, exatamente como verificado em
// docs/INPUTS_PINOUT.md secao 9 antes desta integracao.

// Pulso momentaneo dos encoders: INPUT_ENCODER_xx_CW/CCW só existem como
// EVENTO (lib/inputs não tem "nível" pra rotação) — aqui cada evento
// pendente vira um pulso no bit do HID: sobe por PULSE_HIGH_MS, desce e
// fica em LOW por pelo menos PULSE_LOW_GAP_MS antes do próximo pulso poder
// começar. Isso garante uma borda de subida E de descida visível pro host
// mesmo com vários detents em sequência rápida (o jogo/SimHub vê "aperta e
// solta" um número exato de vezes, nunca um "segurando contínuo").
enum class EncoderPulsePhase : uint8_t { Idle, High, LowGap };
struct EncoderPulseState {
  EncoderPulsePhase phase = EncoderPulsePhase::Idle;
  uint32_t          phaseUntilMs = 0;
};
// indice i (0-7) corresponde ao InputId (INPUT_ENCODER_01_CW + i)
static EncoderPulseState encoderPulse[8];

static const uint32_t ENCODER_PULSE_HIGH_MS    = 30;
static const uint32_t ENCODER_PULSE_LOW_GAP_MS = 20;

static void updateEncoderPulses(uint32_t &buttons, uint32_t now) {
  for (uint8_t i = 0; i < 8; i++) {
    const InputId    id  = (InputId)(INPUT_ENCODER_01_CW + i);
    const uint32_t   bit = (1UL << id);
    EncoderPulseState &p = encoderPulse[i];

    switch (p.phase) {
      case EncoderPulsePhase::Idle: {
        const InputEventType ev = inputs_get_event(id);
        if (ev == INPUT_EVENT_CW || ev == INPUT_EVENT_CCW) {
          p.phase = EncoderPulsePhase::High;
          p.phaseUntilMs = now + ENCODER_PULSE_HIGH_MS;
          buttons |= bit;
        } else {
          buttons &= ~bit;
        }
        break;
      }
      case EncoderPulsePhase::High:
        buttons |= bit;
        if ((int32_t)(now - p.phaseUntilMs) >= 0) {
          p.phase = EncoderPulsePhase::LowGap;
          p.phaseUntilMs = now + ENCODER_PULSE_LOW_GAP_MS;
          buttons &= ~bit;
        }
        break;
      case EncoderPulsePhase::LowGap:
        buttons &= ~bit;
        if ((int32_t)(now - p.phaseUntilMs) >= 0) {
          p.phase = EncoderPulsePhase::Idle;
        }
        break;
    }
  }
}

static const uint8_t PIN_BOOT  = BOARD_BOOT_PIN;
static const char   *AP_NAME   = "ButtonBox-Setup";

static WiFiManager wm;
static bool otaReady    = false;
static bool otaRunning  = false;
static bool portalAtivo = false;

static void startOta() {
  if (otaReady) return;

  // NAO mexe em WiFi.setSleep() aqui: esta funcao e' guardada por otaReady,
  // entao roda UMA vez na vida da placa. A economia de energia agora e'
  // desligada em onWifiConnected(), a cada (re)conexao — ver a nota la.
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA
    .onStart([]()              { otaRunning = true;  Serial.println("\n[OTA] iniciando"); })
    .onEnd([]()                { Serial.println("\n[OTA] concluido"); })
    // SEM print de progresso: escrever no CDC do TinyUSB bloqueia quando o
    // host nao esta lendo a porta (e ninguem le durante o upload). Isso
    // travava o loop, ArduinoOTA.handle() parava de ser chamado e a
    // transferencia morria pela metade — era o "Error Uploading" em ~29%.
    .onProgress([](unsigned int, unsigned int) { })
    .onError([](ota_error_t e) { otaRunning = false; Serial.printf("\n[OTA] erro %u\n", e); });
  ArduinoOTA.begin();
  MDNS.begin(OTA_HOSTNAME);

  otaReady = true;
  Serial.println("[OTA] pronto");
}

// Chamada a cada iteracao do loop; o corpo so roda na BORDA de conexao.
//
// Por que existe: WiFi.setSleep(false) estava dentro de startOta(), que tem
// `if (otaReady) return;` logo na primeira linha — ou seja, era aplicado uma
// unica vez e nunca mais. Toda reconexao (queda do AP, roaming,
// setAutoReconnect, ou o WiFi.disconnect()+begin() do wifiKeepAlive) passava
// a correr o risco de voltar com power save ligado, sem ninguem reaplicar.
//
// O sintoma bate com a nota 4 do cabecalho deste arquivo: com economia de
// energia, o ping de LAN varia muito e o OTA corrompe no meio. Medido em
// 2026-09-24 com a placa nesse estado: 70% de perda de pacote, ~423 ms de
// media e respostas ICMP DUPLICADAS — enquanto o mesmo Mac pingava o gateway
// com 0% de perda. O elo ruim era AP -> ESP32, nao a LAN.
static void onWifiConnected() {
  static bool wasConnected = false;

  if (WiFi.status() != WL_CONNECTED) {
    wasConnected = false; // armado para reaplicar quando voltar
    return;
  }

  if (!wasConnected) {
    wasConnected = true;
    WiFi.setSleep(false); // <- o ponto: reaplicado a CADA reconexao
    Serial.printf("[WiFi] conectado ip=%s rssi=%d sleep=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  WiFi.getSleep() ? "ON" : "OFF");
  }

  startOta();
}

// Reinsiste na rede para sempre. Nunca abre o portal por conta própria.
static void wifiKeepAlive() {
  if (portalAtivo) return;
  if (WiFi.status() == WL_CONNECTED) return;

  static uint32_t lastTry = 0;
  const uint32_t now = millis();
  if (now - lastTry < 10000) return;
  lastTry = now;

  Serial.println("[WiFi] sem conexao, tentando novamente...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void statusReport() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last < 5000) return;
  last = now;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[status] HID ok | WiFi ip=%s rssi=%d | OTA=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                  otaReady ? "pronto" : "iniciando");
  } else {
    Serial.printf("[status] HID ok | WiFi desconectado | portal=%s\n",
                  portalAtivo ? "ATIVO" : "nao");
  }
}

// A CDC agora carrega DOIS protocolos no mesmo fluxo de bytes: o console
// de texto de sempre (PING/VERSION/IP/BOOTLOADER, terminado em '\n') e o
// binário Standard Serial do SimHub (lib/simhub), que sempre começa com 6
// bytes 0xFF — sequência que nunca apareceria no início de um comando de
// texto válido. simhub_feed_header_byte() é alimentada com TODO byte
// antes de decidir o que fazer com ele: se fechar os 6x 0xFF, o resto do
// frame é entregue a simhub_process_packet() e o buffer de linha de texto
// em andamento é descartado (não fazia sentido de qualquer forma, já que
// começava com 0xFF); caso contrário, um 0xFF isolado (prefixo que não
// fechou) é descartado sem nunca entrar no parser de texto — não há como
// esse byte fazer parte de um comando de texto válido mesmo assim.
static void serialCommands() {
  static char line[32];
  static uint8_t len = 0;

  while (Serial.available() > 0) {
    const uint8_t raw = (uint8_t)Serial.read();

    if (simhub_is_header_byte(raw)) {
      simhub_process_packet();
      len = 0;
      continue;
    }

    const char c = (char)raw;
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      for (uint8_t i = 0; i < len; i++) line[i] = (char)toupper((int)line[i]);
      if      (strcmp(line, "PING") == 0)    Serial.println("PONG");
      else if (strcmp(line, "VERSION") == 0) Serial.println("ESP32S3_BUTTONBOX_HID_OTA");
      else if (strcmp(line, "IP") == 0)      Serial.println(WiFi.localIP().toString());
      else if (strcmp(line, "RSSI") == 0) {
        // Diagnostico de link. Existe porque o OTA falhando "no meio" quase
        // nunca e' bug do OTA: ou o sinal esta fraco, ou o power save voltou.
        // Leitura de RSSI: -50 otimo, -67 e' o piso pratico pra transferencia
        // confiavel, -75 pra baixo o OTA vai quebrar. sleep=ON com OTA
        // instavel e' bug de firmware, nao de rede.
        if (WiFi.status() != WL_CONNECTED) {
          Serial.println("RSSI_OFFLINE");
        } else {
          Serial.printf("RSSI %d dBm sleep=%s ip=%s\n", WiFi.RSSI(),
                        WiFi.getSleep() ? "ON" : "OFF",
                        WiFi.localIP().toString().c_str());
        }
      }
      else if (strncmp(line, "SETLEDS ", 8) == 0) {
        // Quantos LEDs tem a FITA (a matriz e' fixa em 8x8 = 64 pelo
        // proprio protocolo do SimHub). Persiste em NVS, sobrevive a
        // reboot — existe pra nao precisar recompilar quando a contagem
        // exata da fita mudar. Ver docs/SIMHUB_PROTOCOL.md.
        const int n = atoi(line + 8);
        if (n > 0 && simhub_set_strip_count((uint16_t)n)) {
          Serial.printf("LEDS_SET %d\n", n);
        } else {
          Serial.printf("LEDS_INVALID (1-%u)\n", (unsigned)SIMHUB_STRIP_COUNT_MAX);
        }
      }
      else if (strcmp(line, "DUMPLEDS") == 0) {
        // Comando NOSSO (nao faz parte do protocolo do SimHub) pra
        // verificar os framebuffers recebidos sem precisar dos LEDs
        // fisicos acesos — util pra bancada.
        const uint16_t strip = simhub_get_strip_count();
        Serial.printf("[dumpleds] conectado=%s matriz=%u fita=%u\n",
                      simhub_is_connected() ? "sim" : "nao",
                      (unsigned)SIMHUB_MATRIX_LED_COUNT, (unsigned)strip);
        Serial.print("[dumpleds] matriz ");
        for (uint16_t i = 0; i < 4; i++) {
          const SimhubColor c = simhub_get_matrix_led(i);
          Serial.printf("M%u=(%3u,%3u,%3u) ", i, c.r, c.g, c.b);
        }
        Serial.println();
        Serial.print("[dumpleds] fita ");
        const uint16_t toShow = (strip < 4) ? strip : 4;
        for (uint16_t i = 0; i < toShow; i++) {
          const SimhubColor c = simhub_get_strip_led(i);
          Serial.printf("LED%u=(%3u,%3u,%3u) ", i, c.r, c.g, c.b);
        }
        Serial.println();
      }
      else if (strncmp(line, "BRIGHTNESS ", 11) == 0) {
        // Define o limite global de brilho dos LEDs (25-75%).
        // Exemplos: "BRIGHTNESS 60" -> 60%, "BRIGHTNESS 25" -> mínimo,
        //           "BRIGHTNESS 75" -> máximo.
        // Persiste em NVS: sobrevive a reboot/power cycle.
        // Resposta: "BRIGHTNESS_SET <valor_real>%" — o valor real pode ser
        // diferente do pedido se estiver fora da faixa permitida.
        const int pct = atoi(line + 11);
        if (pct < 1 || pct > 100) {
          Serial.printf("BRIGHTNESS_INVALID (25-%u)\n",
                        (unsigned)WS2812_BRIGHTNESS_MAX_PCT);
        } else {
          const uint8_t applied = ws2812_set_brightness((uint8_t)pct);
          nvsSaveBrightness();
          Serial.printf("BRIGHTNESS_SET %u%%\n", (unsigned)applied);
        }
      }
      else if (strcmp(line, "BRIGHTNESS") == 0) {
        // Sem argumento: so consulta o valor atual, sem alterar nem gravar.
        Serial.printf("BRIGHTNESS_GET %u%%\n", (unsigned)ws2812_get_brightness());
      }
      else if (strcmp(line, "BOOTLOADER") == 0) {
        // Entra em modo download por software, dispensando segurar o botao
        // BOOT. Com ARDUINO_USB_MODE=0 o USB-Serial-JTAG some, e o esptool
        // nao consegue resetar a placa sozinho pelo CDC do TinyUSB — sem isto
        // toda gravacao por cabo exigiria a dancinha do botao.
        //
        // usb_persist_restart(RESTART_BOOTLOADER), NAO REG_WRITE direto no
        // registrador RTC_CNTL_OPTION1_REG/RTC_CNTL_FORCE_DOWNLOAD_BOOT: essa
        // era a versao anterior, e ela PRENDE A PLACA PERMANENTEMENTE em modo
        // bootloader. Esse registrador vive no dominio RTC do chip, que so e
        // zerado por um power-on reset de verdade (queda completa de energia
        // por vaaarios segundos) — nem esp_restart() nem o "hard reset via
        // RTS" que o esptool faz ao fim de toda gravacao o limpam. Uma vez
        // setado, TODO boot seguinte (inclusive apos gravar um firmware novo)
        // entra direto no ROM bootloader, sem nunca chegar a rodar o app —
        // reproduzido e confirmado na pratica durante o teste desta funcao.
        //
        // usb_persist_restart() e a API oficial do core Arduino-ESP32 pra
        // isto (e o que o proprio mecanismo de auto-reset do PlatformIO/
        // Arduino IDE usa internamente, via 1200bps-touch ou DTR/RTS). No
        // ESP32-S3 ela troca o periferico USB ativo pra modo Serial-JTAG por
        // SOFTWARE (usb_switch_to_cdc_jtag()), sem tocar nesse registrador
        // RTC — nao ha risco de ficar preso.
        Serial.println("REBOOTING_TO_BOOTLOADER");
        Serial.flush();
        delay(100);
        usb_persist_restart(RESTART_BOOTLOADER);
      }
      len = 0;
      continue;
    }
    if (len < sizeof(line) - 1) line[len++] = c;
    else                        len = 0;
  }
}

void setup() {
  pinMode(PIN_BOOT, INPUT_PULLUP);

  // HID antes de tudo: enumera mesmo que o WiFi nunca suba.
  Gamepad.begin();
  USB.begin();

  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32-S3 ButtonBox (HID + OTA) ===");

  // MCP23017 + 74HC4067 + 4x KY-040 — nao bloqueia, nao depende de WiFi/USB.
  // Hardware ainda em bancada de testes: se o MCP23017 nao estiver
  // fisicamente ligado ainda, os bits correspondentes simplesmente ficam
  // sempre "solto" (pull-up) — nao trava nem impede o resto do firmware.
  inputs_init();
  Serial.println("[inputs] MCP23017 + 74HC4067 + encoders inicializados");

  // Protocolo Standard Serial do SimHub, sobre a mesma CDC — nao mexe no
  // Serial.begin() em si (ja chamado acima), so zera o estado do parser.
  simhub_init();
  Serial.printf("[simhub] pronto: matriz 8x8 (%u) + fita (%u)\n",
                (unsigned)SIMHUB_MATRIX_LED_COUNT, (unsigned)simhub_get_strip_count());

  // Driver da fita/matriz WS2812 — so desenha o que o framebuffer do
  // SimHub tiver; nao sabe (nem precisa saber) o que cada pixel significa.
  if (!ws2812_init(BOARD_WS2812_PIN)) {
    Serial.println("[ws2812] AVISO: falha ao inicializar canal RMT");
  }

  // Brilho: restaura do NVS (padrao 60% se nunca foi salvo). Deve ser
  // chamado APOS ws2812_init(), pois ws2812_set_brightness() so escreve
  // em RAM — o driver precisa estar pronto antes de qualquer ws2812_show().
  nvsLoadBrightness();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] conectando em \"%s\"...\n", WIFI_SSID);
}

void loop() {
  if (portalAtivo) wm.process();
  onWifiConnected(); // reaplica setSleep(false) e sobe o OTA na (re)conexao
  if (otaReady) ArduinoOTA.handle();

  // Durante a atualizacao NADA mais roda: qualquer escrita na serial pode
  // bloquear no CDC e matar a transferencia.
  if (otaRunning) return;

  wifiKeepAlive();
  serialCommands();
  statusReport();

  inputs_update(); // MCP23017 + 74HC4067 + 4x KY-040 — nao bloqueia (lib/inputs)
  simhub_update(); // so verifica timeout de conexao — nao le Serial, nao bloqueia

  // Ponte SimHub -> WS2812: o UNICO lugar do firmware que conhece os dois
  // ao mesmo tempo (nem lib/simhub nem lib/ws2812 se conhecem — regra 9 da
  // arquitetura). Reenvia o framebuffer inteiro a cada iteracao do loop;
  // como ws2812_show() e' assincrono (rmtWrite, nao bloqueia) e o loop ja
  // tem um piso de ~5ms (delay(5) no final), isso nunca chama de novo
  // antes da transmissao anterior (~2,2ms para 74 LEDs) terminar.
  //
  // Layout fisico: UMA cadeia WS2812 so — a matriz 8x8 primeiro (pixels
  // 0-63), a fita logo depois (DOUT da matriz -> DIN da fita). Pro SimHub
  // eles continuam sendo dois dispositivos logicos separados (RGB Matrix e
  // RGB Leds); quem junta os dois numa cadeia unica e' este trecho.
  {
    static Ws2812Color ledBuf[WS2812_MAX_LEDS];

    // Sem SimHub conectado (recem-ligado, ou SimHub fechado ha mais de 5 s)
    // os framebuffers dele estao zerados — em vez de deixar tudo apagado,
    // mostra a animacao de espera (lib/led_idle). Assim que o SimHub voltar
    // a falar com a placa, simhub_is_connected() fica true e as cores dele
    // assumem na hora. A animacao nao e' efeito de jogo: so existe nesse
    // estado, e LED_IDLE_BRIGHTNESS = 0 (em led_idle.h) a desliga.
    static LedIdleColor idleMatrix[SIMHUB_MATRIX_LED_COUNT];
    static LedIdleColor idleStrip[SIMHUB_STRIP_COUNT_MAX];
    const bool idle = !simhub_is_connected();
    const uint16_t stripCount = simhub_get_strip_count();
    uint16_t total = SIMHUB_MATRIX_LED_COUNT + stripCount;
    if (total > WS2812_MAX_LEDS) total = WS2812_MAX_LEDS;
    if (idle) {
      led_idle_render(millis(), idleMatrix, idleStrip, stripCount);
    }

    // Matriz: o SimHub manda os 64 pixels em ordem linear (linha a linha).
    // Paineis 8x8 de WS2812 quase sempre sao ligados em serpentina (linhas
    // alternadas invertidas) — MATRIX_SERPENTINE faz esse remapeamento.
    // Se a sua matriz for ligada em linhas retas, e' so por false aqui.
    static const bool MATRIX_SERPENTINE = true;
    for (uint16_t i = 0; i < SIMHUB_MATRIX_LED_COUNT; i++) {
      uint16_t phys = i;
      if (MATRIX_SERPENTINE) {
        const uint16_t y = i / 8;
        uint16_t x = i % 8;
        if ((y % 2) == 0) x = 7 - x;
        phys = y * 8 + x;
      }
      if (idle) {
        ledBuf[phys] = {idleMatrix[i].r, idleMatrix[i].g, idleMatrix[i].b};
      } else {
        const SimhubColor c = simhub_get_matrix_led(i);
        ledBuf[phys] = {c.r, c.g, c.b};
      }
    }

    // Fita, logo depois da matriz na mesma cadeia.
    for (uint16_t j = 0; SIMHUB_MATRIX_LED_COUNT + j < total; j++) {
      if (idle) {
        ledBuf[SIMHUB_MATRIX_LED_COUNT + j] = {idleStrip[j].r, idleStrip[j].g, idleStrip[j].b};
      } else {
        const SimhubColor c = simhub_get_strip_led(j);
        ledBuf[SIMHUB_MATRIX_LED_COUNT + j] = {c.r, c.g, c.b};
      }
    }

    ws2812_show(ledBuf, total);
  }

  static uint32_t buttons  = 0;
  static uint32_t lastSent = 0xFFFFFFFFu;

  const uint32_t now = millis();

  // O heartbeat de bring-up que piscava o Botao 32 a cada 1 s FOI REMOVIDO:
  // ele existia so pra provar que o HID estava vivo enquanto o hardware nao
  // estava montado, e ja cumpriu esse papel. Com os 31 controles reais
  // respondendo, ele virava ruido — um botao "fantasma" que qualquer jogo ou
  // o SimHub poderia bindar por engano num aprendizado automatico.
  //
  // O bit 31 agora fica LIVRE (o descriptor continua com 32 botoes; e o 32o
  // que deixou de ser usado). E o primeiro slot disponivel se voce adicionar
  // um controle — ver o orcamento de bits em docs/INPUTS_PINOUT.md secao 9.

  // Entradas de nivel (botões, ignição, start, freio, chaves caça): mapa
  // explicito no topo do arquivo — bit = (uint8_t)id. inputs_get_state()
  // já vem debounced (MCP23017/74HC4067 debouncam na própria camada).
  for (uint8_t id = 0; id < INPUT_LEVEL_ID_COUNT; id++) {
    const uint32_t bit = (1UL << id);
    if (inputs_get_state((InputId)id)) buttons |= bit;
    else                                buttons &= ~bit;
  }

  // Encoders: cada detent vira um pulso momentâneo (ver
  // updateEncoderPulses no topo do arquivo), nunca um nível.
  updateEncoderPulses(buttons, now);

  // BOOT segurado 5 s -> abre o portal para trocar de rede. Feito em operação,
  // não no boot: GPIO0 baixo no reset entra em modo download e o app não roda.
  // (BOOT NAO alimenta mais nenhum bit do HID — o antigo "BOOT = Botao 1"
  // era o valor simulado que esta integracao substitui pelo INPUT_BUTTON_01
  // real, vindo do MCP23017. A lógica de WiFi/portal abaixo é a mesma de
  // sempre, intocada.)
  const bool bootDown = (digitalRead(PIN_BOOT) == LOW);
  static uint32_t bootHeldSince = 0;
  if (bootDown) {
    if (bootHeldSince == 0) bootHeldSince = now;
    if (!portalAtivo && now - bootHeldSince >= 5000) {
      Serial.println("[WiFi] BOOT 5s: abrindo portal de configuracao");
      wm.setConfigPortalBlocking(false);
      portalAtivo = wm.startConfigPortal(AP_NAME);
    }
  } else {
    bootHeldSince = 0;
  }

  if (buttons != lastSent) {
    lastSent = buttons;
    Gamepad.send(0, 0, 0, 0, 0, 0, 0, buttons);
  }

  delay(5);
}
