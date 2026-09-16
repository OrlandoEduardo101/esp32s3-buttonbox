# Fala o protocolo REAL do SimHub com a placa (duas camadas: transporte ARQ
# + comandos) e confere as respostas — verificacao de ponta a ponta sem
# precisar abrir o SimHub.
#
#   Host -> device:  01 01 <packetId> <len> <dados...> <crc8>
#   Device -> host:  ACK = 03 <packetId>, byte = 08 <b>, string = 06 <len> <bytes> 20
#   Dentro dos dados: 03 (MESSAGE_HEADER) + char de comando + payload
#
# Referencia: docs/SIMHUB_PROTOCOL.md
#
# Uso:
#   python scripts/simhub_test_send.py COM27
import sys
import time
import serial

CRC8_TABLE = [
    0,213,127,170,254,43,129,84,41,252,86,131,215,2,168,125,82,135,45,248,172,121,211,6,123,174,4,209,133,80,250,47,
    164,113,219,14,90,143,37,240,141,88,242,39,115,166,12,217,246,35,137,92,8,221,119,162,223,10,160,117,33,244,94,139,
    157,72,226,55,99,182,28,201,180,97,203,30,74,159,53,224,207,26,176,101,49,228,78,155,230,51,153,76,24,205,103,178,
    57,236,70,147,199,18,184,109,16,197,111,186,238,59,145,68,107,190,20,193,149,64,234,63,66,151,61,232,188,105,195,22,
    239,58,144,69,17,196,110,187,198,19,185,108,56,237,71,146,189,104,194,23,67,150,60,233,148,65,235,62,106,191,21,192,
    75,158,52,225,181,96,202,31,98,183,29,200,156,73,227,54,25,204,102,179,231,50,152,77,48,229,79,154,206,27,177,100,
    114,167,13,216,140,89,243,38,91,142,36,241,165,112,218,15,32,245,95,138,222,11,161,116,9,220,118,163,247,34,136,93,
    214,3,169,124,40,253,87,130,255,42,128,85,1,212,126,171,132,81,251,46,122,175,5,208,173,120,210,7,83,134,44,249,
]

BROADCAST_ID = 255
MATRIX_LEDS = 64


def arq_packet(data: bytes, packet_id: int = BROADCAST_ID) -> bytes:
    """Monta um pacote ARQ: 01 01 <id> <len> <dados> <crc8>."""
    assert 1 <= len(data) <= 32, "payload ARQ vai de 1 a 32 bytes"
    crc = 0
    crc = CRC8_TABLE[crc ^ packet_id]
    crc = CRC8_TABLE[crc ^ len(data)]
    for b in data:
        crc = CRC8_TABLE[crc ^ b]
    return bytes([0x01, 0x01, packet_id, len(data)]) + data + bytes([crc])


def send_command(ser, cmd: bytes, payload: bytes = b"", wait=0.3):
    """Manda 0x03 + comando (+ payload) dentro de pacotes ARQ de ate 32 bytes."""
    data = bytes([0x03]) + cmd + payload
    ser.reset_input_buffer()
    for i in range(0, len(data), 32):
        ser.write(arq_packet(data[i:i + 32]))
        ser.flush()
        time.sleep(0.02)
    time.sleep(wait)
    return ser.read(ser.in_waiting or 1)


def decode(reply: bytes):
    """Decodifica as respostas enquadradas do dispositivo."""
    out, i = [], 0
    while i < len(reply):
        tag = reply[i]
        if tag == 0x03 and i + 1 < len(reply):       # ACK
            out.append(("ack", reply[i + 1])); i += 2
        elif tag == 0x04 and i + 2 < len(reply):     # NACK
            out.append(("nack", reply[i + 1], reply[i + 2])); i += 3
        elif tag == 0x08 and i + 1 < len(reply):     # byte
            out.append(("byte", reply[i + 1])); i += 2
        elif tag == 0x06 and i + 1 < len(reply):     # string
            n = reply[i + 1]
            out.append(("str", reply[i + 2:i + 2 + n])); i += 2 + n + 1
        else:
            out.append(("raw", tag)); i += 1
    return out


def rgb_stream_mode1(colors):
    out = bytearray([1])
    for (r, g, b) in colors:
        out += bytes([r, g, b])
    out += bytes([0])
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        print(f"uso: {sys.argv[0]} <porta serial>")
        sys.exit(1)

    with serial.Serial(sys.argv[1], 115200, timeout=1) as ser:
        time.sleep(2)

        # Hello: dados = 03 '1' 0x10 (o 0x10 e' o trailer do exemplo oficial)
        reply = send_command(ser, b"1", b"\x10")
        decoded = decode(reply)
        print(f"[hello]    {reply!r} -> {decoded}")
        assert ("byte", ord("j")) in decoded, "Hello nao respondeu 0x08 'j'"

        reply = send_command(ser, b"0")
        decoded = decode(reply)
        print(f"[features] {reply!r} -> {decoded}")
        feats = b"".join(d[1] for d in decoded if d[0] == "str")
        assert b"R" in feats, "Features nao anuncia 'R' (RGB Matrix)"
        assert b"P" not in feats, "Features anuncia 'P' (SHCustomProtocol) — nao deveria"

        reply = send_command(ser, b"4")
        decoded = decode(reply)
        print(f"[fita]     {reply!r} -> {decoded}")
        counts = [d[1] for d in decoded if d[0] == "byte"]
        assert counts, "comando '4' nao respondeu a contagem"
        strip_count = counts[0]
        print(f"[fita]     {strip_count} LEDs")

        strip_colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        strip_colors += [((i * 8) % 256,) * 3 for i in range(3, strip_count)]
        reply = send_command(ser, b"6", rgb_stream_mode1(strip_colors[:strip_count]))
        print(f"[fita RGB] {reply!r} -> {decode(reply)}")

        matrix_colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)] + [(0, 0, 0)] * (MATRIX_LEDS - 3)
        reply = send_command(ser, b"R", rgb_stream_mode1(matrix_colors), wait=0.6)
        print(f"[matriz]   {reply!r} -> {decode(reply)}")

        ser.reset_input_buffer()
        ser.write(b"DUMPLEDS\r\n")
        ser.flush()
        time.sleep(0.4)
        dump = ser.read(ser.in_waiting or 1).decode(errors="replace")
        print("--- DUMPLEDS ---")
        print(dump)

        nospace = dump.replace(" ", "")
        assert "M0=(255,0,0)" in nospace, "matriz: M0 nao chegou como (255,0,0)"
        assert "LED0=(255,0,0)" in nospace, "fita: LED0 nao chegou como (255,0,0)"
        print("\nOK: ARQ, handshake, features, fita e matriz responderam corretamente.")


if __name__ == "__main__":
    main()
