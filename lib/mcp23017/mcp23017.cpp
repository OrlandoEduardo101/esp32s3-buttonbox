#include "mcp23017.h"
#include <Wire.h>

// Mapa de registradores do MCP23017 em modo IOCON.BANK=0 (default de
// fábrica), onde os registradores de A e B ficam intercalados e o
// ponteiro de endereço auto-incrementa por padrão (SEQOP=0) — é o que
// permite ler GPIOA+GPIOB numa tacada só em mcp23017_read().
namespace {
constexpr uint8_t REG_IODIRA   = 0x00;
constexpr uint8_t REG_IODIRB   = 0x01;
constexpr uint8_t REG_IPOLA    = 0x02;
constexpr uint8_t REG_IPOLB    = 0x03;
constexpr uint8_t REG_IOCON    = 0x0A; // duplicado em 0x0B; escrevemos os dois
constexpr uint8_t REG_IOCON_B  = 0x0B;
constexpr uint8_t REG_GPPUA    = 0x0C;
constexpr uint8_t REG_GPPUB    = 0x0D;
constexpr uint8_t REG_GPIOA    = 0x12;
constexpr uint8_t REG_GPIOB    = 0x13;

// IOCON bit 6 = MIRROR (INTA/INTB espelhados). Os demais bits ficam em 0:
// BANK=0, SEQOP=0 (auto-incremento LIGADO, necessário para o read duplo),
// DISSLW=0, HAEN=0 (não se aplica ao MCP23017 I2C), ODR=0, INTPOL=0.
constexpr uint8_t IOCON_VALUE = 0x40;

uint8_t g_addr = MCP23017_DEFAULT_I2C_ADDR;
bool g_ready = false;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Lê 'count' bytes a partir de 'reg' (usa o auto-incremento de endereço).
// Devolve false se o número de bytes recebidos não bater com o esperado.
bool readRegisters(uint8_t reg, uint8_t *dst, uint8_t count) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) { // repeated start, sem soltar o barramento
    return false;
  }
  const uint8_t got = Wire.requestFrom(g_addr, count);
  if (got != count) {
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    dst[i] = (uint8_t)Wire.read();
  }
  return true;
}
} // namespace

bool mcp23017_init(uint8_t i2c_addr, uint8_t sda_pin, uint8_t scl_pin) {
  g_addr  = i2c_addr;
  g_ready = false;

  Wire.begin(sda_pin, scl_pin);
  Wire.setClock(400000); // Fast Mode — MCP23017 suporta ate 1.7 MHz (datasheet)

  bool ok = true;
  ok &= writeRegister(REG_IOCON,   IOCON_VALUE);
  ok &= writeRegister(REG_IOCON_B, IOCON_VALUE);
  ok &= writeRegister(REG_IODIRA, 0xFF); // todos entrada
  ok &= writeRegister(REG_IODIRB, 0xFF); // todos entrada
  ok &= writeRegister(REG_IPOLA,  0x00); // sem inversao: aberto=HIGH, fechado=LOW
  ok &= writeRegister(REG_IPOLB,  0x00);
  ok &= writeRegister(REG_GPPUA,  0xFF); // pull-up interno em todos os 8 pinos
  ok &= writeRegister(REG_GPPUB,  0xFF); // idem banco B

  g_ready = ok;
  return ok;
}

uint16_t mcp23017_read() {
  uint8_t buf[2];
  if (!readRegisters(REG_GPIOA, buf, 2)) {
    return 0xFFFF; // falha de I2C -> assume tudo aberto (seguro)
  }
  // buf[0] = GPIOA, buf[1] = GPIOB (auto-incremento do ponteiro no MCP)
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

uint8_t mcp23017_read_gpioa() {
  uint8_t value;
  if (!readRegisters(REG_GPIOA, &value, 1)) {
    return 0xFF;
  }
  return value;
}

uint8_t mcp23017_read_gpiob() {
  uint8_t value;
  if (!readRegisters(REG_GPIOB, &value, 1)) {
    return 0xFF;
  }
  return value;
}
