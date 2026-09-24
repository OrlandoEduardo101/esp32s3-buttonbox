🇧🇷 Português | [🇺🇸 English](INPUTS_PINOUT.en.md)

# INPUTS_PINOUT.md — Arquitetura de entradas do hardware final

> Documento de definição de pinout — o **raciocínio** por trás de cada
> escolha de pino/canal. Baseado nas restrições reais do chip ESP32-S3
> (verificadas em `variants/esp32s3/pins_arduino.h` do core Arduino-ESP32
> instalado) e nas capacidades de datasheet do MCP23017 e do 74HC4067.
>
> **Os números que valem de verdade estão em `include/board_config.h`.**
> Trocou de placa ou quer outro pinout? Edite só aquele arquivo — nenhum
> outro `.cpp`/`.h` do projeto tem pino/canal/endereço fixo. Este documento
> continua valendo como explicação de *por que* cada escolha foi feita
> (restrições elétricas, folga de expansão etc.), não como fonte dos
> valores atuais.

## Por que esta distribuição

- **GPIO direto do ESP32-S3**: reservado para o que precisa de menor latência
  e leitura por interrupção — os 8 sinais CLK/DT dos 4 encoders (quadratura
  exige decodificação rápida, sensível a atraso) e o barramento I2C +
  interrupção do MCP23017.
- **MCP23017 (I2C, com interrupção)**: reservado para as entradas de "ação"
  mais frequentes/críticas — os 11 push buttons, o botão Start Engine e a
  chave de ignição. O MCP suporta interrupt-on-change (INTA/INTB) e pull-up
  interno em todos os 16 pinos, então a ESP32 não fica em polling: só acorda
  quando algo muda, com debounce feito em firmware sobre o evento.
- **74HC4067 (mux analógico/digital, sem interrupção)**: reservado para
  entradas de estado, que mudam devagar e toleram varredura por polling —
  os 4 SW dos encoders (clique do botão embutido), as 4 chaves tipo caça e o
  microswitch do freio de estacionamento. Sem interrupção própria, mas o
  scan de 9 canais é trivial em tempo (ESP32-S3 a 240 MHz), e nenhuma dessas
  entradas exige resposta em poucos milissegundos.
- Ambos os expansores ficam com canais livres (2 no MCP, 7 no mux) para
  expansão futura sem redesenhar o pinout.

---

## 1. Tabela de entradas

| ENTRADA | HARDWARE | PINO | TIPO | OBSERVAÇÃO |
|---|---|---|---|---|
| Encoder 1 — CLK | ESP32-S3 direto | GPIO4 | Digital, interrupção (edge) | Quadratura; decodificar CLK+DT juntos, sem debounce por software (debounce é inerente à máquina de estados de quadratura). Saída mapeada como botão virtual +/- fixo — ver seção 8 |
| Encoder 1 — DT | ESP32-S3 direto | GPIO5 | Digital, interrupção (edge) | Ver observação do CLK |
| Encoder 1 — SW | 74HC4067 | C0 | Digital, polling | Clique do encoder; debounce por firmware (~10–20 ms) |
| Encoder 2 — CLK | ESP32-S3 direto | GPIO6 | Digital, interrupção (edge) | Quadratura |
| Encoder 2 — DT | ESP32-S3 direto | GPIO7 | Digital, interrupção (edge) | Quadratura |
| Encoder 2 — SW | 74HC4067 | C1 | Digital, polling | Debounce por firmware |
| Encoder 3 — CLK | ESP32-S3 direto | GPIO10 | Digital, interrupção (edge) | Quadratura |
| Encoder 3 — DT | ESP32-S3 direto | GPIO11 | Digital, interrupção (edge) | Quadratura |
| Encoder 3 — SW | 74HC4067 | C2 | Digital, polling | Debounce por firmware |
| Encoder 4 — CLK | ESP32-S3 direto | GPIO12 | Digital, interrupção (edge) | Quadratura |
| Encoder 4 — DT | ESP32-S3 direto | GPIO13 | Digital, interrupção (edge) | Quadratura |
| Encoder 4 — SW | 74HC4067 | C3 | Digital, polling | Debounce por firmware |
| Push button 1 | MCP23017 | GPA0 | Digital, interrupção (I2C) | Pull-up interno do MCP |
| Push button 2 | MCP23017 | GPA1 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 3 | MCP23017 | GPA2 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 4 | MCP23017 | GPA3 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 5 | MCP23017 | GPA4 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 6 | MCP23017 | GPA5 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 7 | MCP23017 | GPA6 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 8 | MCP23017 | GPA7 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 9 | MCP23017 | GPB0 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 10 | MCP23017 | GPB1 | Digital, interrupção (I2C) | Pull-up interno |
| Push button 11 | MCP23017 | GPB2 | Digital, interrupção (I2C) | Pull-up interno |
| Start Engine (push) | MCP23017 | GPB3 | Digital, interrupção (I2C) | Pull-up interno; ação crítica, latência baixa via interrupção |
| Ignição — posição ON | MCP23017 | GPB4 | Digital, interrupção (I2C) | Confirmado pelo usuário: chave de scooter, 3 posições físicas (1=OFF, 2=ON, 3=IGN/partida). Contato ON fecha na posição 2 e **continua fechado** na posição 3 (não abre durante a partida) |
| Ignição — posição IGN | MCP23017 | GPB5 | Digital, interrupção (I2C) | **Momentâneo**: só fecha enquanto a chave é segurada na posição 3; a chave tem retorno por mola e volta sozinha para a posição 2 ao ser solta — este contato não é biestável, o firmware deve tratá-lo como pulso, não como estado |
| Microswitch freio de estacionamento | 74HC4067 | C8 | Digital, polling | Estado (não pulso) — down=freio acionado, up=freio liberado. Ver lógica de mapeamento na seção 7 |
| Chave caça 1 (ON/OFF) | 74HC4067 | C4 | Digital, polling | Estado, sem urgência |
| Chave caça 2 (ON/OFF) | 74HC4067 | C5 | Digital, polling | Estado, sem urgência |
| Chave caça 3 (ON/OFF) | 74HC4067 | C6 | Digital, polling | Estado, sem urgência |
| Chave caça 4 (ON/OFF) | 74HC4067 | C7 | Digital, polling | Estado, sem urgência |

Total: 31 sinais físicos (12 dos encoders + 11 push buttons + 1 microswitch +
2 da ignição + 1 start + 4 chaves caça).

---

## 2. Mapa completo do MCP23017 (I2C)

| Pino | Uso |
|---|---|
| GPA0 | Push button 1 |
| GPA1 | Push button 2 |
| GPA2 | Push button 3 |
| GPA3 | Push button 4 |
| GPA4 | Push button 5 |
| GPA5 | Push button 6 |
| GPA6 | Push button 7 |
| GPA7 | Push button 8 |
| GPB0 | Push button 9 |
| GPB1 | Push button 10 |
| GPB2 | Push button 11 |
| GPB3 | Start Engine |
| GPB4 | Ignição — ON |
| GPB5 | Ignição — IGN |
| GPB6 | **Livre** (expansão futura) |
| GPB7 | **Livre** (expansão futura) |

Configuração recomendada (para quando for implementar): pull-ups internos
ativados em todos os pinos usados, `GPINTEN` habilitado nos 14 pinos usados,
`INTCON`/`DEFVAL` para interrupção por mudança de estado (não por nível
fixo), bancos A e B com interrupção espelhada (`IOCON.MIRROR = 1`) para usar
uma única linha de INT no ESP32-S3.

### Lógica da chave de ignição (confirmada com o usuário)

Chave de scooter, 3 posições físicas, **não** é uma chave rotativa comum de
3 estados estáveis:

| Posição física | GPB4 (ON) | GPB5 (IGN) | Comportamento mecânico |
|---|---|---|---|
| 1 — OFF | inativo | inativo | Estável — permanece até o usuário mover |
| 2 — ON | **ativo** | inativo | Estável — permanece até o usuário mover |
| 3 — IGN/partida | **ativo** | **ativo** | **Momentâneo** — retorno por mola; some sozinho e a chave volta para a posição 2 assim que o usuário solta |

Consequências para o firmware (ainda não implementado, só registrado aqui
para quando for portar a lógica):
- GPB5 (IGN) deve ser tratado como **pulso de partida**, não como estado —
  nunca fica "travado" ativo sozinho, sempre retorna a 0 quando a mão do
  usuário sai da chave.
- Se o usuário mover a chave direto da posição 3 para a posição 1 (pulando a
  posição 2), o firmware vê GPB4 e GPB5 caindo praticamente juntos — o
  veículo deve ser tratado como desligado (OFF) nesse caso, exatamente como
  se tivesse passado por ON primeiro.
- Não existe uma combinação válida de "IGN ativo com ON inativo" nesta
  chave — se isso for lido, é transição elétrica passageira (debounce), não
  um estado real a ser reportado.

Endereço I2C: 7 bits, `0x20`–`0x27` conforme os pinos de endereço A0/A1/A2 do
chip (a fiar conforme a placa; com um único MCP basta amarrar A0/A1/A2 no GND
→ endereço `0x20`).

---

## 3. Mapa completo do 74HC4067 (mux digital)

| Canal | Uso |
|---|---|
| C0 | Encoder 1 — SW |
| C1 | Encoder 2 — SW |
| C2 | Encoder 3 — SW |
| C3 | Encoder 4 — SW |
| C4 | Chave caça 1 |
| C5 | Chave caça 2 |
| C6 | Chave caça 3 |
| C7 | Chave caça 4 |
| C8 | Microswitch freio de estacionamento |
| C9 | **Livre** (expansão futura) |
| C10 | **Livre** (expansão futura) |
| C11 | **Livre** (expansão futura) |
| C12 | **Livre** (expansão futura) |
| C13 | **Livre** (expansão futura) |
| C14 | **Livre** (expansão futura) |
| C15 | **Livre** (expansão futura) |

Linhas de controle (não são canais de entrada, contam à parte no orçamento de
GPIO do ESP32-S3 — ver seção 4):

| Linha | Função |
|---|---|
| S0 | Bit 0 do endereço do canal |
| S1 | Bit 1 do endereço do canal |
| S2 | Bit 2 do endereço do canal |
| S3 | Bit 3 do endereço do canal |
| SIG | Saída comum — a ESP32 lê aqui o estado do canal atualmente selecionado |

O 74HC4067 não tem pull-up próprio (ao contrário do MCP23017), mas o
firmware liga o pull-up interno da ESP32 na linha SIG, que serve todos os
canais — nenhum resistor por entrada é necessário. Detalhes e a única
recomendação opcional na seção 11.

---

## 4. GPIOs diretos do ESP32-S3 usados

| GPIO | Função |
|---|---|
| GPIO4 | Encoder 1 — CLK |
| GPIO5 | Encoder 1 — DT |
| GPIO6 | Encoder 2 — CLK |
| GPIO7 | Encoder 2 — DT |
| GPIO8 | I2C SDA (para MCP23017) — pino default do core Arduino-ESP32 (`pins_arduino.h`) |
| GPIO9 | I2C SCL (para MCP23017) — pino default do core |
| GPIO10 | Encoder 3 — CLK |
| GPIO11 | Encoder 3 — DT |
| GPIO12 | Encoder 4 — CLK |
| GPIO13 | Encoder 4 — DT |
| GPIO14 | MCP23017 — INT (interrupção espelhada A+B) |
| GPIO15 | 74HC4067 — S0 |
| GPIO16 | 74HC4067 — S1 |
| GPIO17 | 74HC4067 — S2 |
| GPIO18 | 74HC4067 — S3 |
| GPIO21 | 74HC4067 — SIG |

16 GPIOs usados. Todos dentro da faixa GPIO0–21, que é universalmente exposta
em qualquer variante de placa ESP32-S3 (incluindo Super Mini/S3 Zero) — nenhum
depende de pinos cuja disponibilidade varia por fabricante.

Pinos deliberadamente **evitados** nesta alocação (não usar para entradas
sem motivo forte):
- **GPIO0** — já é o botão BOOT físico da placa, já mapeado como Botão 1 do
  HID no firmware atual (`docs/ARCHITECTURE.md` item 8). Strapping pin.
- **GPIO3, GPIO45, GPIO46** — strapping pins do ESP32-S3 (afetam modo de
  boot/tensão da flash); GPIO46 além disso é *input-only*. Usáveis só com
  cuidado extra, não recomendados para a primeira revisão do hardware.
- **GPIO19, GPIO20** — USB nativo (D-/D+), em uso pelo HID/CDC. Nunca usar
  como GPIO neste projeto.
- **GPIO26–GPIO32** — barramento interno para a flash/PSRAM em pacote (SPI0/1).
  Não existem como GPIO utilizável nesta placa.
- **GPIO43, GPIO44** — UART0 TX/RX (default). Livres no uso atual (o projeto
  usa USB CDC nativo, não UART0), mas reservados para depuração serial
  alternativa se um dia for necessário.
- **GPIO48** — já é o LED RGB (NeoPixel) embutido da placa (`PIN_NEOPIXEL`
  no core). Reutilizável se abrirmos mão do LED de status, não recomendado
  agora.

---

## 5. GPIOs ainda livres

| GPIO | Observação |
|---|---|
| GPIO1 | **Usado** — saída de dados WS2812 (matriz 8x8 + fita), ver `docs/SYSTEM_INTEGRATION.md`. Não é mais uma entrada, é a única saída de dados endereçável do projeto |
| GPIO33 | Livre — confirmar fisicamente exposto no módulo específico (só é reservado para PSRAM/flash em modo Octal; esta placa usa Quad, `qspi_2m`, então o pino está disponível a nível de chip) |
| GPIO34 | Livre — mesma observação do GPIO33 |
| GPIO35 | Livre — mesma observação |
| GPIO36 | Livre — mesma observação |
| GPIO37 | Livre — mesma observação |
| GPIO38 | Livre |
| GPIO39 | Livre |
| GPIO40 | Livre |
| GPIO41 | Livre |
| GPIO42 | Livre |
| GPIO43 | Livre (UART0 TX, ver nota acima) |
| GPIO44 | Livre (UART0 RX, ver nota acima) |
| GPIO45 | Livre com cautela (strapping — evitar até ter motivo) |
| GPIO46 | Livre com cautela (strapping, input-only) |
| GPIO47 | Livre |
| GPIO48 | Livre com cautela (LED RGB embutido) |

Isso deixa margem confortável para expansão futura (paddle shifters, freio de
mão analógico, potenciômetros de embreagem/pedaleira, etc.) sem precisar
redesenhar esta alocação — inclusive nos 2 canais livres do MCP23017 e nos 7
canais livres do 74HC4067 antes mesmo de tocar em GPIO extra.

---

## 6. Saídas / atuadores (fora do escopo de entradas, registrado por afetar o orçamento de GPIO)

### LED do botão Start Engine

Botão momentâneo iluminado (aro cromado, tampa vermelha "ENGINE START", LED
interno), modelo comum de sites chineses de sim racing, **3 terminais**: o
LED e a chave compartilham um pino comum (COM), que faz parte tanto do
circuito da chave quanto do cátodo do LED.

Identificação dos 3 pinos com multímetro (antes de soldar):
1. Modo continuidade, testar os 3 pares **com o botão pressionado** — o par
   que fecha só nesse instante é COM + pino da chave.
2. Modo diodo, testar os pares restantes sem pressionar — o par que acende
   fraco e mostra ~1,8–2,2V (numa polaridade) é o LED; ponta vermelha nesse
   teste = ânodo (+), ponta preta = cátodo (COM).
3. O pino que aparece nos dois testes é o COM compartilhado.

Ligação:

| Terminal do botão | Vai para | Observação |
|---|---|---|
| COM (comum, chave+LED) | GND | Um único fio de GND serve pros dois circuitos |
| Chave (NO) | MCP23017 GPB3 | Já alocado na seção 1/2 — pull-up interno do MCP, lê LOW ao pressionar |
| LED (ânodo, +) | **GPIO2** do ESP32-S3, através de resistor de **220 Ω** (ou 150 Ω para mais brilho) | Saída digital dedicada — permite o firmware decidir quando acender (sempre ligado, só com ignição em ON/IGN, piscando durante o crank etc.) em vez de fiar direto num trilho de alimentação |

Resistor calculado para os 3,3 V do GPIO do ESP32-S3 (não usar o trilho de
5V direto no GPIO): `R = (3,3V − Vf_LED) / I_desejada`, com `Vf` típico de
LED vermelho ≈ 2,0V e corrente-alvo 6–9 mA → 220–150 Ω. Não usar resistor
menor que ~100 Ω, para não ultrapassar a corrente segura por pino do
ESP32-S3.

GPIO2 sai da lista de "livres" (seção 5) — reservado para este LED.

---

## 7. Lógica de mapeamento — freio de estacionamento (Euro Truck Simulator)

Comportamento confirmado pelo usuário: o manete é um switch de 2 posições
**estáveis** (não é momentâneo como a chave de ignição) — **para baixo =
freio de estacionamento acionado**, **para cima = freio liberado**.

Ponto de atenção para quando for portar o firmware: o bind padrão do
Euro/American Truck Simulator para freio de estacionamento é um **toggle**
(a tecla/botão alterna o estado a cada pressionada, não segura um nível).
Um switch físico de 2 posições estáveis não bate diretamente com esse
modelo — se o firmware simplesmente espelhasse o nível do switch para o
bit do HID (nível baixo = bit 1 mantido, nível alto = bit 0 mantido), o jogo
receberia isso como "segurar o botão", o que não é o que o toggle espera.

**Decidido com o usuário: opção 1.** Firmware gera um pulso só na transição
de estado do switch (não espelha o nível):
- borda **up→down**: dispara um pulso único de "toggle" — assume que isso
  aciona o freio no jogo;
- borda **down→up**: dispara outro pulso único de "toggle" — assume que isso
  libera o freio.

Isso mantém a posição física do manete sempre coerente com o estado do freio
no jogo, contanto que os dois comecem sincronizados (ex.: sessão sempre
inicia com o manete pra baixo / freio acionado, que é o padrão real de
caminhão parado). Ainda não implementado — só a especificação de
comportamento, conforme o escopo atual (arquitetura/pinout apenas).

---

## 8. Lógica de mapeamento — encoders (decidido: virtual +/- fixo)

**Decidido com o usuário: os 4 KY-040 rodam sempre em modo botão virtual
+/-, sem modo eixo e sem troca de modo.** Não haverá combo de alternância
nem LED de feedback de modo — descartado deliberadamente por não valer a
complexidade extra (4 LEDs, fiação, lógica de sincronização de estado) para
um caso de uso que a maioria dos jogos de simulação não usa (quase todo bind
relevante — cruise control, limpador, espelho, rádio etc. — aceita
tecla/botão discreto; muito poucos aceitam eixo diretamente).

Comportamento (ainda não implementado, só especificado):
- Cada detente de rotação **no sentido horário** gera um pulso momentâneo em
  um botão virtual "+" (apertar e soltar).
- Cada detente **no sentido anti-horário** gera um pulso momentâneo em um
  botão virtual "−", separado do "+".
- Portanto cada encoder ocupa **2 bits do bitmask de botões do HID**
  (um para +, um para −), além do bit do seu próprio SW (clique), que já
  está mapeado como botão normal no 74HC4067 (seção 1/3).
- Sem eixo, sem modo alternativo, sem LED de status de modo — decisão
  fechada para este projeto.

---

## 9. Orçamento de bits do HID (verificação de capacidade)

O HID report descriptor atual do firmware (`docs/ARCHITECTURE.md` item 7)
**não deve ser alterado** — expõe um bitmask fixo de 32 botões
(`uint32_t buttons`). Verificação de que todo o mapeamento decidido até
aqui cabe nesse limite sem precisar mexer no descriptor:

| Origem | Bits |
|---|---|
| Push buttons 1–11 | 11 |
| SW dos 4 encoders (clique) | 4 |
| Start Engine | 1 |
| Ignição — ON | 1 |
| Ignição — IGN | 1 |
| Chaves caça 1–4 | 4 |
| Freio de estacionamento (pulso de toggle) | 1 |
| Encoders — botão virtual +/- (4 × 2) | 8 |
| **Total usado** | **31** |
| Bits livres no HID atual | **1** |

Cabe dentro dos 32 bits existentes, sem exigir mudança no HID descriptor —
compatível com a restrição de não alterar HID/USB descriptors herdada da
baseline anterior. Sobra apenas **1 bit livre** no descriptor atual.

**Opção de expansão confirmada pelo usuário**: se o orçamento de 32 bits for
insuficiente no futuro, o usuário já tem um perfil/descriptor de referência
com **64 slots de botão** disponível para usar como base. Ou seja, o limite
de 32 bits **não é definitivo** — é só o que o firmware atual usa; ampliar
para 64 botões é uma opção real, já disponível, quando/se for necessário.

Importante: isso **é**, por definição, uma alteração do HID report
descriptor (o `uint32_t buttons` do `USBHIDGamepad` teria que virar um
bitmask maior — 64 bits, tipicamente 2× `uint32_t` ou um `uint8_t[8]` em um
report customizado, já que a lib `USBHIDGamepad` do core Arduino-ESP32 usada
hoje é fixa em 32 botões via `TUD_HID_REPORT_DESC_GAMEPAD`). Portanto essa
mudança:
- **Não é necessária agora** — o mapeamento atual cabe nos 32 bits existentes.
- Quando for feita, é uma decisão de firmware deliberada e isolada (trocar o
  report descriptor + a lógica de envio), não uma alteração incidental —
  exatamente o tipo de mudança que a baseline pede pra tratar com cautela,
  só que agora com autorização explícita do usuário para fazê-la **se e
  quando** o 32º bit acabar sendo insuficiente.

---

## 10. Integração com o HID — mapa definitivo INPUT LOGICAL ID → bit do HID

**Implementado.** A camada `lib/inputs` (INPUT_*) foi integrada ao firmware
principal (`src/main.cpp`), substituindo os valores simulados/heartbeat
pelos estados reais das 31 entradas. O HID report descriptor **não foi
alterado** (continua `uint32_t buttons` via `USBHIDGamepad`, 32 bits, sem
uso de eixos/hat) — os 31 IDs coube exatamente no espaço já orçado na seção
9 acima.

Regra do mapa: **bit = valor numérico do `InputId`** (0-30) — o enum já é
0-based e sequencial em `lib/inputs/inputs.h`, então não existe tabela de
indireção separada para desatualizar. A tabela completa (idêntica à do
comentário no topo de `src/main.cpp`):

| INPUT LOGICAL ID | Bit | Botão no joy.cpl | Tipo |
|---|---|---|---|
| INPUT_BUTTON_01..11 | 0-10 | 1-11 | nível |
| INPUT_BUTTON_12 (SW encoder 1) | 11 | 12 | nível |
| INPUT_BUTTON_13 (SW encoder 2) | 12 | 13 | nível |
| INPUT_BUTTON_14 (SW encoder 3) | 13 | 14 | nível |
| INPUT_BUTTON_15 (SW encoder 4) | 14 | 15 | nível |
| INPUT_IGNITION_ON | 15 | 16 | nível |
| INPUT_IGNITION_IGN | 16 | 17 | nível |
| INPUT_START_ENGINE | 17 | 18 | nível |
| INPUT_HANDBRAKE | 18 | 19 | nível |
| INPUT_KILL_SWITCH_01..04 | 19-22 | 20-23 | nível |
| INPUT_ENCODER_01_CW / _CCW | 23 / 24 | 24 / 25 | pulso |
| INPUT_ENCODER_02_CW / _CCW | 25 / 26 | 26 / 27 | pulso |
| INPUT_ENCODER_03_CW / _CCW | 27 / 28 | 28 / 29 | pulso |
| INPUT_ENCODER_04_CW / _CCW | 29 / 30 | 30 / 31 | pulso |
| heartbeat de bring-up (temporário) | 31 | 32 | nível |

Entradas de **nível** (botões/switches/ignição): `inputs_get_state()` já
debounced é espelhado direto no bit — sem lógica extra no `main.cpp`.

Entradas de **encoder** (CW/CCW): como `lib/inputs` só expõe EVENTO pra
rotação (nunca um "nível segurado"), o `main.cpp` traduz cada evento
pendente num **pulso momentâneo** no bit: sobe por 30ms, desce e fica em
LOW por pelo menos 20ms antes do próximo pulso poder começar
(`updateEncoderPulses()`). Isso garante uma borda de subida E de descida
visível pro host mesmo com vários detents em sequência rápida — o
jogo/SimHub sempre vê "aperta e solta" um número exato de vezes, nunca um
botão "preso".

**BOOT deixou de alimentar o HID.** O antigo `BOOT -> bit 0 (Botão 1)` era
um valor simulado de bring-up (pré-hardware-real); foi removido e o bit 0
agora é o `INPUT_BUTTON_01` de verdade, vindo do MCP23017. O botão físico
BOOT da placa continua existindo só para o gesto de abrir o portal de WiFi
(segurar 5s) — isso é lógica de WiFi, não foi tocado.

**Heartbeat (bit 31 / Botão 32): mantido de propósito.** Só deve ser
removido do `main.cpp` quando os 31 controles reais acima estiverem
comprovadamente funcionando na bancada física (hardware ainda não montado
no momento desta integração) — decisão explícita, não um esquecimento.

### O que falta para fechar o critério de sucesso desta etapa

Testar no `joy.cpl`, com o hardware fisicamente montado:
- cada um dos 31 botões individualmente;
- múltiplos botões simultâneos (sem "ghosting" — não deveria haver, já que
  não há matriz, mas vale confirmar);
- os 4 encoders, sentido CW e CCW de cada um;
- as 4 chaves caça, a ignição (3 posições) e o freio de estacionamento;
- o botão Start Engine;
- confirmar que o HID continua respondendo normalmente com o computador
  sem o SimHub aberto (o firmware não depende dele — SimHub só consome o
  HID que o Windows já expõe).

---

## 11. Diagrama de soldagem — GND ou VCC?

**Regra geral: todo botão/chave tem um terminal no pino/canal de entrada e o
outro terminal no GND. Nunca no VCC.** A lógica é *active-low*: em repouso o
pino lê `HIGH` (puxado pra cima por um pull-up), e ao fechar o contato ele é
puxado pro GND e lê `LOW`. Tudo é 3,3 V. O que varia por subsistema é
*quem fornece o pull-up* — e, na maioria dos casos, **você não solda
resistor nenhum**, porque o firmware liga pull-ups internos.

> **Correção (2026-09-24).** Uma versão anterior deste documento (e um
> comentário em `lib/mux4067`) dizia que **cada** entrada do 74HC4067
> precisava de um resistor externo de 10 kΩ. Isso estava exagerado: o
> firmware já liga o pull-up interno da ESP32 na linha SIG
> (`lib/mux4067/mux4067.cpp`, `pinMode(g_sig, INPUT_PULLUP)`), e o mux
> conecta o canal selecionado ao SIG, então esse pull-up já serve todos os
> canais. Não é preciso um resistor por entrada.

### Ligações de alimentação e controle (o que costuma fazer "nada funcionar")

| Chip | Pino | Liga em | Observação |
|---|---|---|---|
| ESP32-S3 | 3V3 / GND | trilho 3,3 V / GND comum | tudo referenciado ao mesmo GND |
| MCP23017 | VDD / VSS | 3,3 V / GND | |
| MCP23017 | **RESET** | **3,3 V** | não deixar flutuando — o chip pode ficar preso em reset. Algumas placas breakout já trazem isso resolvido; confira a sua |
| MCP23017 | A0, A1, A2 | **GND** | endereço I2C `0x20` (é o que `board_config.h` espera) |
| MCP23017 | SDA / SCL | GPIO8 / GPIO9 | precisam de pull-up I2C (~4,7 kΩ → 3,3 V). Breakouts costumam já ter; chip solto, não |
| MCP23017 | INTA / INTB | não conectar | o firmware usa polling, não usa interrupção do chip |
| 74HC4067 | VCC / GND | 3,3 V / GND | |
| 74HC4067 | **EN (/E)** | **GND** | ativo em nível baixo: em `HIGH` (ou flutuando) desliga todos os canais. Algumas placas já aterram; confira a sua |
| 74HC4067 | S0–S3 / SIG | GPIO15–18 / GPIO21 | conforme `include/board_config.h` |
| KY-040 | `+` | **3,3 V (não 5 V)** | o pull-up da placa vai pra esse pino; em 5 V injetaria 5 V nas entradas da ESP32 |

### Push buttons 1–11, Start Engine, Ignição (MCP23017)

O MCP23017 tem **pull-up interno** (~100 kΩ, habilitado via `GPPU`).
Nenhum resistor externo.

```
Terminal 1 do botão/chave ──→  Pino do MCP23017 (GPA0–GPB5)
Terminal 2                ──→  GND
```

Lógica: pino lê `HIGH` em repouso → `LOW` ao pressionar.

### Chaves caça (C4–C7) e freio de estacionamento (C8) — 74HC4067

Chaves "nuas" (só dois terminais). O pull-up interno da ESP32 na linha SIG
já resolve — **nenhum resistor por entrada**.

```
Canal Cx do 74HC4067 ──→  Terminal 1 da chave
                          Terminal 2 da chave ──→  GND
```

**Opcional, só se o `mux-test` mostrar leitura instável** (mais provável
com fios longos): **um único** resistor de 10 kΩ entre a linha **SIG
(GPIO21)** e o 3,3 V. Como o SIG é comum a todos os canais, esse resistor
serve os nove de uma vez. *Isto é uma expectativa pela física do circuito;
ainda não foi medido no hardware montado — teste sem, e só adicione se
precisar.*

**Freio de estacionamento:** o firmware trata "contato fechado no GND" como
acionado. Qual posição da alavanca fecha o contato depende de você usar o
terminal NO ou NC do microswitch; se ficar invertido, troque o terminal.

### SW dos encoders (C0–C3) — KY-040 → 74HC4067

O SW **não** é uma chave nua: é o pino `SW` do módulo, e o KY-040 padrão
já traz pull-up de 10 kΩ na placa em CLK, DT **e** SW. Então nada a soldar:

```
KY-040  SW  ──→  Canal C0–C3 do 74HC4067
        GND ──→  GND
        +   ──→  3,3 V
```

Confira no seu módulo com o multímetro (medir entre `SW` e `+` deve dar
~10 kΩ); alguns clones não têm o pull-up do SW — nesse caso o pull-up
interno da linha SIG cobre.

### CLK / DT dos encoders KY-040 (GPIO direto da ESP32-S3)

O módulo já tem pull-up na própria PCB (e o firmware liga o interno também,
o que é inofensivo).

```
KY-040  CLK  ──→  GPIO4 / 6 / 10 / 12   (encoders 1–4)
        DT   ──→  GPIO5 / 7 / 11 / 13
        GND  ──→  GND
        +    ──→  3,3 V
```

### Ignição (3 posições, chave de scooter)

```
COM (comum)          ──→  GND
Contato ON           ──→  GPB4 do MCP23017
Contato IGN (partida)──→  GPB5 do MCP23017
```

Confirme com o multímetro, antes de fiar, que o contato ON continua
fechado na posição 3 (partida) — ver a seção 2.

### Botão Start Engine (3 terminais) e o LED dele

```
COM            ──→  GND               (serve pra chave E pro LED)
Chave (NO)     ──→  GPB3 do MCP23017
LED ânodo (+)  ──→  220 Ω ──→  GPIO2 da ESP32-S3
```

Resistor para 3,3 V: `R = (3,3 V − Vf) / I` com `Vf ≈ 2,0 V` (LED
vermelho) e `I ≈ 6–9 mA` → 220–150 Ω. Mínimo recomendado: 100 Ω.

> **Atenção:** o firmware **ainda não aciona o GPIO2**. Ligado assim, o LED
> não acende sozinho — a lógica (ex.: acender com a ignição em ON) ainda
> não foi implementada.

### Matriz + fita WS2812

Alimentação **5 V externa** direto na matriz/fita (~4,4 A no pico com 74
LEDs em branco cheio — a USB da ESP32 não aguenta), **GND comum** com a
ESP32, dado `GPIO1 → DIN da matriz → DOUT da matriz → DIN da fita`. Recomendado:
capacitor ~1000 µF entre +5 V e GND perto do primeiro LED e resistor
~330–470 Ω em série no fio de dado.

### Tabela-resumo

| Componente | Terminal 1 vai para | Terminal 2 vai para | Pull-up |
|---|---|---|---|
| Push buttons 1–11 | Pino MCP23017 (GPA0–GPA7, GPB0–GPB2) | **GND** | Interno do MCP (~100 kΩ) |
| Start Engine (chave) | MCP23017 GPB3 | **GND** (via COM) | Interno do MCP |
| Ignição — ON / IGN | MCP23017 GPB4 / GPB5 | **GND** (via COM) | Interno do MCP |
| SW dos encoders | Canal 74HC4067 C0–C3 | módulo KY-040 (GND/`+`) | Na PCB do KY-040 |
| Chaves caça 1–4 | Canal 74HC4067 C4–C7 | **GND** | Interno da ESP32 na linha SIG |
| Freio de estacionamento | Canal 74HC4067 C8 | **GND** | Interno da ESP32 na linha SIG |
| KY-040 CLK/DT | GPIO da ESP32 direto | — | Na PCB do KY-040 |
| Start Engine (LED) | GPIO2 via 220 Ω | **GND** (via COM) | — (não aplicável; GPIO2 ainda não é acionado) |
