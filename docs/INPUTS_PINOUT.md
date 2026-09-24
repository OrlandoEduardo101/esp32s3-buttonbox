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

> **Revisão 2 (set/2026) — o pinout mudou.** A revisão 1 punha os 8 sinais
> de quadratura em GPIO direto e as 5 linhas do 74HC4067 nos GPIO 15, 16,
> 17, 18 e 21. Na **ESP32-S3 SuperMini** esses cinco não existem como pino
> de header: saem em pads na face inferior do módulo, sob o corpo da placa,
> o que torna a solda manual impraticável. Tudo do GPIO15 pra cima é assim.
>
> **Decisão (opção B, escolhida pelo usuário):** os encoders migram para o
> **banco A do MCP23017** e o 74HC4067 assume as linhas de botão, ocupando
> os pinos de header que os encoders liberaram. Resultado: **nenhum sinal
> acima do GPIO14**, e ainda sobram GPIO 11, 12 e 13 livres. Os dois CIs são
> os mesmos de antes — nenhuma peça nova.

- **MCP23017 (I2C)**: leva os 8 sinais **CLK/DT dos 4 encoders** (banco A) e,
  no banco B, o **SW de cada encoder** + as **4 chaves tipo caça**. Os SW
  ficam aqui por fiação, não por eletrônica: saem do mesmo conector do
  KY-040, então acompanham o CLK/DT do próprio encoder em vez de atravessar
  a caixa até o mux.
- **74HC4067 (mux de 16 canais)**: leva os **11 push buttons**, o **Start
  Engine**, os **2 contatos da ignição** e o **microswitch do freio de
  estacionamento** — 15 dos 16 canais. Todas entradas de contato mecânico,
  que o scan por polling atende de sobra.
- **GPIO direto do ESP32-S3**: sobra só o que *precisa* de pino — I2C (8/9),
  as 5 linhas de controle do mux (4-7 e 10), WS2812 (1), LED do Start
  Engine (2) e BOOT (0).

### O preço dessa troca (leia antes de mexer na amostragem)

Quadratura em GPIO tinha interrupção por borda: **nenhuma transição se
perde**, o hardware acorda a MCU em cada uma. Por I2C não existe isso — as
transições só aparecem se a amostragem cair entre elas.

Um KY-040 tem 20 detents por volta e 4 transições de Gray por detent. Num
giro rápido de mão (~1,5 volta/s = 30 detents/s), um detent dura ~33 ms e
suas 4 transições ficam ~5-10 ms uma da outra. Amostrando a **1 ms**, todas
são vistas com folga. Amostrando a 5 ms, duas transições vizinhas caem na
mesma amostra, a máquina de quadratura vê um salto de 2 bits (impossível num
contato real), trata como ruído e **perde o detent** — ela nunca inventa um
evento no sentido errado, mas o clique simplesmente não sai.

O `loop()` principal termina com `delay(5)`, ou seja, amostraria a 200 Hz.
Por isso a leitura do MCP23017 **saiu do loop** e virou uma **task dedicada**
(`lib/inputs/inputs.cpp`, core 0, prioridade acima do loop), com período em
`BOARD_MCP_SAMPLE_PERIOD_MS`. Uma leitura de 2 bytes a 400 kHz custa ~150 µs
— ~15% de um barramento que não tem mais nenhum outro dispositivo.

Depois que essa task sobe, **ela é a única dona do I2C**: `inputs_update()`,
no loop, só lê o cache que ela mantém. Não chame `input_expander_update()`
de nenhum outro lugar.

O pino **INT do MCP23017 está reservado no GPIO14** e vale a pena fiar desde
já, mesmo sem uso: se um dia a amostragem virar orientada a evento, é só
ligar `GPINTEN` no chip e um `attachInterrupt` — sem refazer a placa.

- Ambos os expansores ficam com folga (nenhum canal livre no MCP, 1 canal
  livre no mux, e 3 GPIO de header livres) para expansão futura.

---

## 1. Tabela de entradas

| ENTRADA | HARDWARE | PINO | TIPO | OBSERVAÇÃO |
|---|---|---|---|---|
| Encoder 1 — CLK | MCP23017 | GPA0 | Digital, amostrado 1 kHz | Quadratura; decodificar CLK+DT juntos, sem debounce por software (debounce é inerente à máquina de estados de quadratura). **Lido pelo caminho CRU** do `input_expander`, fora do debounce de 15 ms. Saída mapeada como botão virtual +/- fixo — ver seção 8 |
| Encoder 1 — DT | MCP23017 | GPA1 | Digital, amostrado 1 kHz | Ver observação do CLK |
| Encoder 1 — SW | MCP23017 | GPB0 | Digital, polling | Clique do encoder; debounce por firmware (15 ms) |
| Encoder 2 — CLK | MCP23017 | GPA2 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 2 — DT | MCP23017 | GPA3 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 2 — SW | MCP23017 | GPB1 | Digital, polling | Debounce por firmware |
| Encoder 3 — CLK | MCP23017 | GPA4 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 3 — DT | MCP23017 | GPA5 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 3 — SW | MCP23017 | GPB2 | Digital, polling | Debounce por firmware |
| Encoder 4 — CLK | MCP23017 | GPA6 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 4 — DT | MCP23017 | GPA7 | Digital, amostrado 1 kHz | Quadratura |
| Encoder 4 — SW | MCP23017 | GPB3 | Digital, polling | Debounce por firmware |
| Push button 1 | 74HC4067 | C0 | Digital, polling | Pull-up interno da ESP32 na linha SIG |
| Push button 2 | 74HC4067 | C1 | Digital, polling | idem |
| Push button 3 | 74HC4067 | C2 | Digital, polling | idem |
| Push button 4 | 74HC4067 | C3 | Digital, polling | idem |
| Push button 5 | 74HC4067 | C4 | Digital, polling | idem |
| Push button 6 | 74HC4067 | C5 | Digital, polling | idem |
| Push button 7 | 74HC4067 | C6 | Digital, polling | idem |
| Push button 8 | 74HC4067 | C7 | Digital, polling | idem |
| Push button 9 | 74HC4067 | C8 | Digital, polling | idem |
| Push button 10 | 74HC4067 | C9 | Digital, polling | idem |
| Push button 11 | 74HC4067 | C10 | Digital, polling | idem |
| Start Engine (push) | 74HC4067 | C11 | Digital, polling | Um ciclo completo de scan dos 15 canais leva ~450 µs, repartido em várias chamadas de `mux4067_scan()` — imperceptível para um botão |
| Ignição — posição ON | 74HC4067 | C12 | Digital, polling | Confirmado pelo usuário: chave de scooter, 3 posições físicas (1=OFF, 2=ON, 3=IGN/partida). Contato ON fecha na posição 2 e **continua fechado** na posição 3 (não abre durante a partida) |
| Ignição — posição IGN | 74HC4067 | C13 | Digital, polling | **Momentâneo**: só fecha enquanto a chave é segurada na posição 3; a chave tem retorno por mola e volta sozinha para a posição 2 ao ser solta — este contato não é biestável, o firmware deve tratá-lo como pulso, não como estado |
| Microswitch freio de estacionamento | 74HC4067 | C14 | Digital, polling | Estado (não pulso) — down=freio acionado, up=freio liberado. Ver lógica de mapeamento na seção 7 |
| Chave caça 1 (ON/OFF) | MCP23017 | GPB4 | Digital, polling | Estado, sem urgência |
| Chave caça 2 (ON/OFF) | MCP23017 | GPB5 | Digital, polling | Estado, sem urgência |
| Chave caça 3 (ON/OFF) | MCP23017 | GPB6 | Digital, polling | Estado, sem urgência |
| Chave caça 4 (ON/OFF) | MCP23017 | GPB7 | Digital, polling | Estado, sem urgência |

Total: 31 sinais físicos (12 dos encoders + 11 push buttons + 1 microswitch +
2 da ignição + 1 start + 4 chaves caça). 16 no MCP23017 (lotado) e 15 no
74HC4067 (1 canal livre).

---

## 2. Mapa completo do MCP23017 (I2C)

| Pino | Uso |
|---|---|
| GPA0 | Encoder 1 — CLK |
| GPA1 | Encoder 1 — DT |
| GPA2 | Encoder 2 — CLK |
| GPA3 | Encoder 2 — DT |
| GPA4 | Encoder 3 — CLK |
| GPA5 | Encoder 3 — DT |
| GPA6 | Encoder 4 — CLK |
| GPA7 | Encoder 4 — DT |
| GPB0 | Encoder 1 — SW |
| GPB1 | Encoder 2 — SW |
| GPB2 | Encoder 3 — SW |
| GPB3 | Encoder 4 — SW |
| GPB4 | Chave caça 1 |
| GPB5 | Chave caça 2 |
| GPB6 | Chave caça 3 |
| GPB7 | Chave caça 4 |

Os 16 pinos estão ocupados. Expansão futura entra pelo canal C15 livre do
mux, pelos GPIO 11/12/13 livres, ou por um segundo MCP23017 no mesmo
barramento (`0x21`, com A0 em 3V3) — que não custa nenhum pino novo.

**Banco A é caminho de quadratura, não de botão.** O firmware lê GPA0-GPA7
pelo acessor **cru** (`input_expander_get_raw()`), fora da janela de debounce
de 15 ms da camada — aplicar debounce ali apagaria justamente as transições
que formam o detent. O banco B passa normalmente pelo debounce.

Configuração aplicada por `mcp23017_init()`: os 16 pinos como entrada, com
pull-up interno (~100 kΩ) ligado em todos, sem inversão de polaridade
(`IPOL = 0`, mantendo aberto=HIGH), `SEQOP` habilitado para ler GPIOA+GPIOB
numa única transação I2C e `IOCON.MIRROR = 1` (INTA/INTB espelhados).

O `MIRROR` já está ligado, mas o **INT não é usado** hoje: a amostragem é
periódica (task dedicada, ver a seção "O preço dessa troca" no topo). Para
migrar para leitura orientada a evento no futuro, o que falta é habilitar
`GPINTEN` nos pinos desejados com `INTCON = 0` (interrupção por mudança de
estado, não por comparação com `DEFVAL`) e um `attachInterrupt` no GPIO14 —
sem mudança de hardware, desde que o INT já esteja fiado.

> Cuidado ao fazer isso: se uma mudança acontecer entre a leitura de GPIO e
> o rearme, o INT pode ficar travado em nível ativo e nenhuma borda nova
> aparece. Quem for implementar precisa manter um polling de segurança em
> paralelo. É justamente por essa corrida que a amostragem periódica foi
> escolhida primeiro.

Endereço I2C: 7 bits, `0x20`–`0x27` conforme os pinos de endereço A0/A1/A2 do
chip (a fiar conforme a placa; com um único MCP basta amarrar A0/A1/A2 no GND
→ endereço `0x20`).

---

## 3. Mapa completo do 74HC4067 (mux digital)

| Canal | Uso |
|---|---|
| C0 | Push button 1 |
| C1 | Push button 2 |
| C2 | Push button 3 |
| C3 | Push button 4 |
| C4 | Push button 5 |
| C5 | Push button 6 |
| C6 | Push button 7 |
| C7 | Push button 8 |
| C8 | Push button 9 |
| C9 | Push button 10 |
| C10 | Push button 11 |
| C11 | Start Engine |
| C12 | Ignição — ON |
| C13 | Ignição — IGN |
| C14 | Microswitch freio de estacionamento |
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

### Lógica da chave de ignição (confirmada com o usuário)

Chave de scooter, 3 posições físicas, **não** é uma chave rotativa comum de
3 estados estáveis:

| Posição física | C12 (ON) | C13 (IGN) | Comportamento mecânico |
|---|---|---|---|
| 1 — OFF | inativo | inativo | Estável — permanece até o usuário mover |
| 2 — ON | **ativo** | inativo | Estável — permanece até o usuário mover |
| 3 — IGN/partida | **ativo** | **ativo** | **Momentâneo** — retorno por mola; some sozinho e a chave volta para a posição 2 assim que o usuário solta |

Consequências para o firmware (ainda não implementado, só registrado aqui
para quando for portar a lógica):
- C13 (IGN) deve ser tratado como **pulso de partida**, não como estado —
  nunca fica "travado" ativo sozinho, sempre retorna a 0 quando a mão do
  usuário sai da chave.
- Se o usuário mover a chave direto da posição 3 para a posição 1 (pulando a
  posição 2), o firmware vê C12 e C13 caindo praticamente juntos — o
  veículo deve ser tratado como desligado (OFF) nesse caso, exatamente como
  se tivesse passado por ON primeiro.
- Não existe uma combinação válida de "IGN ativo com ON inativo" nesta
  chave — se isso for lido, é transição elétrica passageira (debounce), não
  um estado real a ser reportado.

---

## 4. GPIOs diretos do ESP32-S3 usados

| GPIO | Função |
|---|---|
| GPIO0 | Botão BOOT da placa — só o gesto de abrir o portal de WiFi (segurar 5 s). Não alimenta HID |
| GPIO1 | WS2812 — dado da cadeia (matriz 8x8 + fita), ver `docs/SYSTEM_INTEGRATION.md` |
| GPIO2 | LED do botão Start Engine (saída, via 220 Ω) — reservado, firmware ainda não aciona |
| GPIO4 | 74HC4067 — S0 |
| GPIO5 | 74HC4067 — S1 |
| GPIO6 | 74HC4067 — S2 |
| GPIO7 | 74HC4067 — S3 |
| GPIO8 | I2C SDA (MCP23017) — pino default do core Arduino-ESP32 (`pins_arduino.h`) |
| GPIO9 | I2C SCL (MCP23017) — pino default do core |
| GPIO10 | 74HC4067 — SIG |
| GPIO14 | MCP23017 — INT (espelhado A+B). **Reservado, não usado** pelo firmware atual (a amostragem é periódica); fiar mesmo assim |

11 GPIOs usados, **todos na faixa GPIO0-14** — exatamente a faixa que a
ESP32-S3 SuperMini expõe em header. Nenhum pad da face inferior é
necessário.

Pinos deliberadamente **evitados** nesta alocação:
- **GPIO3, GPIO45, GPIO46** — strapping pins do ESP32-S3 (afetam modo de
  boot/tensão da flash); GPIO46 além disso é *input-only*.
- **GPIO19, GPIO20** — USB nativo (D-/D+), em uso pelo HID/CDC. Nunca usar
  como GPIO neste projeto.
- **GPIO15-18, GPIO21 e tudo de GPIO33 pra cima** — existem no chip, mas na
  SuperMini saem em pads na face inferior. É a restrição que motivou esta
  revisão; não voltar a usá-los sem trocar de placa.
- **GPIO26-GPIO32** — barramento interno para a flash/PSRAM em pacote
  (SPI0/1). Não existem como GPIO utilizável nesta placa.
- **GPIO43, GPIO44** — UART0 TX/RX (default). Livres no uso atual (o projeto
  usa USB CDC nativo, não UART0), mas reservados para depuração serial
  alternativa se um dia for necessário.
- **GPIO48** — LED RGB (NeoPixel) embutido da placa (`PIN_NEOPIXEL` no core).

---

## 5. GPIOs ainda livres

| GPIO | Observação |
|---|---|
| GPIO11 | **Livre em header** — primeiro candidato para qualquer entrada/saída nova |
| GPIO12 | **Livre em header** |
| GPIO13 | **Livre em header** |
| GPIO3 | Existe em header, mas é strapping — evitar até ter motivo |
| GPIO15-18, 21, 33-48 | Existem no chip; na SuperMini são pads da face inferior. Tratar como indisponíveis |

Três pinos de header livres, mais o canal C15 do mux, mais a possibilidade
de um segundo MCP23017 no mesmo I2C (16 entradas a custo zero de pino). Isso
cobre com folga paddle shifters, freio de mão analógico ou pedaleira —
lembrando que, do lado do HID, o orçamento de bits é o limite real (seção 9).

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
| Chave (NO) | 74HC4067 C11 | Já alocado na seção 1/3 — pull-up interno da ESP32 na linha SIG, lê LOW ao pressionar |
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
| MCP23017 | SDA / SCL | GPIO8 / GPIO9 | precisam de pull-up I2C (~4,7 kΩ → 3,3 V). Breakouts costumam já ter; chip solto, não. **Com a quadratura no I2C, um barramento marginal deixa de ser "às vezes falha um botão" e vira detent perdido** — se tiver dúvida, ponha os 4,7 kΩ |
| MCP23017 | INT (ou INTA) | GPIO14 | **reservado**: o firmware atual não usa. Fiar mesmo assim, é de graça e evita refazer a placa se a amostragem virar orientada a evento |
| MCP23017 | INTB | não conectar | `IOCON.MIRROR = 1` espelha os dois bancos numa linha só |
| 74HC4067 | VCC / GND | 3,3 V / GND | |
| 74HC4067 | **EN (/E)** | **GND** | ativo em nível baixo: em `HIGH` (ou flutuando) desliga todos os canais. Algumas placas já aterram; confira a sua |
| 74HC4067 | S0 / S1 / S2 / S3 | GPIO4 / GPIO5 / GPIO6 / GPIO7 | conforme `include/board_config.h` |
| 74HC4067 | SIG | GPIO10 | idem |
| KY-040 | `+` | **3,3 V (não 5 V)** | o pull-up da placa vai pra esse pino; em 5 V injetaria 5 V nas entradas do MCP23017 |

> **Comprimento de fio importa mais que antes.** As 8 linhas CLK/DT agora
> chegam ao MCP23017, e o SDA/SCL passou a carregar a quadratura. Mantenha o
> MCP23017 perto da ESP32 (I2C curto) e leve os fios longos para os
> encoders, não para o barramento.

### Push buttons 1–11, Start Engine, Ignição, freio (74HC4067)

Chaves "nuas" (dois terminais). O firmware liga o pull-up interno da ESP32 na
linha SIG (`pinMode(SIG, INPUT_PULLUP)`), e o mux conecta o canal
selecionado ao SIG — **esse único pull-up serve todos os canais**. Nenhum
resistor por entrada.

```
Canal Cx do 74HC4067 ──→  Terminal 1 da chave
                          Terminal 2 da chave ──→  GND
```

| Canal | O que ligar |
|---|---|
| C0–C10 | Push buttons 1 a 11 |
| C11 | Start Engine (terminal NO da chave; o COM vai pro GND) |
| C12 | Ignição — contato ON |
| C13 | Ignição — contato IGN (partida) |
| C14 | Microswitch do freio de estacionamento |
| C15 | livre |

Lógica: canal lê `HIGH` em repouso → `LOW` ao fechar contra o GND.

**Opcional, só se o `mux-test` mostrar leitura instável** (mais provável com
fios longos): **um único** resistor de 10 kΩ entre a linha **SIG (GPIO10)** e
o 3,3 V. Como o SIG é comum a todos os canais, esse resistor serve os quinze
de uma vez. *Isto é uma expectativa pela física do circuito; ainda não foi
medido no hardware montado — teste sem, e só adicione se precisar.*

**Freio de estacionamento:** o firmware trata "contato fechado no GND" como
acionado. Qual posição da alavanca fecha o contato depende de você usar o
terminal NO ou NC do microswitch; se ficar invertido, troque o terminal.

### Ignição (3 posições, chave de scooter) — 74HC4067

```
COM (comum)          ──→  GND
Contato ON           ──→  C12 do 74HC4067
Contato IGN (partida)──→  C13 do 74HC4067
```

Confirme com o multímetro, antes de fiar, que o contato ON continua fechado
na posição 3 (partida) — ver a seção 3.

### Botão Start Engine (3 terminais) e o LED dele

```
COM            ──→  GND               (serve pra chave E pro LED)
Chave (NO)     ──→  C11 do 74HC4067
LED ânodo (+)  ──→  220 Ω ──→  GPIO2 da ESP32-S3
```

Resistor para 3,3 V: `R = (3,3 V − Vf) / I` com `Vf ≈ 2,0 V` (LED vermelho) e
`I ≈ 6–9 mA` → 220–150 Ω. Mínimo recomendado: 100 Ω.

> **Atenção:** o firmware **ainda não aciona o GPIO2**. Ligado assim, o LED
> não acende sozinho — a lógica (ex.: acender com a ignição em ON) ainda não
> foi implementada.

### Encoders KY-040 — CLK, DT e SW, todos no MCP23017

O MCP23017 tem pull-up interno (~100 kΩ) em todos os 16 pinos, e o módulo
KY-040 já traz os seus na própria PCB. Nada a soldar de resistor.

```
KY-040 #1   CLK ──→ GPA0    DT ──→ GPA1    SW ──→ GPB0
KY-040 #2   CLK ──→ GPA2    DT ──→ GPA3    SW ──→ GPB1
KY-040 #3   CLK ──→ GPA4    DT ──→ GPA5    SW ──→ GPB2
KY-040 #4   CLK ──→ GPA6    DT ──→ GPA7    SW ──→ GPB3
todos       GND ──→ GND     +  ──→ 3,3 V
```

Confira no seu módulo com o multímetro (medir entre `SW` e `+` deve dar
~10 kΩ); alguns clones não têm o pull-up do `SW` — nesse caso o pull-up
interno do MCP23017 cobre.

> **Se um detent físico não gerar exatamente um evento**, o suspeito número 1
> passou a ser o período de amostragem (`BOARD_MCP_SAMPLE_PERIOD_MS`) ou um
> I2C marginal — não o decoder. Rode o `encoder-test` isolado antes de mexer
> na máquina de quadratura, que não mudou nada nesta revisão.

### Chaves caça 1–4 — MCP23017

```
Chave caça 1 ──→ GPB4      Chave caça 3 ──→ GPB6
Chave caça 2 ──→ GPB5      Chave caça 4 ──→ GPB7
outro terminal de cada uma ──→ GND
```

### Matriz + fita WS2812

Alimentação **5 V externa** direto na matriz/fita (~4,4 A no pico com 74
LEDs em branco cheio — a USB da ESP32 não aguenta), **GND comum** com a
ESP32, dado `GPIO1 → DIN da matriz → DOUT da matriz → DIN da fita`.
Recomendado: capacitor ~1000 µF entre +5 V e GND perto do primeiro LED e
resistor ~330–470 Ω em série no fio de dado.

### Tabela-resumo

| Componente | Terminal 1 vai para | Terminal 2 vai para | Pull-up |
|---|---|---|---|
| Push buttons 1–11 | 74HC4067 C0–C10 | **GND** | Interno da ESP32 na linha SIG |
| Start Engine (chave) | 74HC4067 C11 | **GND** (via COM) | Interno da ESP32 na linha SIG |
| Ignição — ON / IGN | 74HC4067 C12 / C13 | **GND** (via COM) | Interno da ESP32 na linha SIG |
| Freio de estacionamento | 74HC4067 C14 | **GND** | Interno da ESP32 na linha SIG |
| KY-040 CLK / DT | MCP23017 GPA0–GPA7 | módulo KY-040 (GND / `+`) | Na PCB do KY-040 + interno do MCP |
| SW dos encoders | MCP23017 GPB0–GPB3 | módulo KY-040 (GND / `+`) | Na PCB do KY-040 + interno do MCP |
| Chaves caça 1–4 | MCP23017 GPB4–GPB7 | **GND** | Interno do MCP (~100 kΩ) |
| Start Engine (LED) | GPIO2 via 220 Ω | **GND** (via COM) | — (não aplicável; GPIO2 ainda não é acionado) |
