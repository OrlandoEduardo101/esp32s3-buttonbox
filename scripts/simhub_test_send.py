# Fala o protocolo REAL do SimHub (aba "Arduino") com a placa e confere as
# respostas — verificacao de ponta a ponta sem precisar abrir o SimHub.
#
# Protocolo: 0x03 (MESSAGE_HEADER) + 1 char de comando + payload.
# Referencia: docs/SIMHUB_PROTOCOL.md (extraido de ESP-SimHub, que
# comprovadamente funciona com o SimHub).
#
# Uso:
#   python scripts/simhub_test_send.py COM27
#   ~/.platformio/penv/bin/python scripts/simhub_test_send.py /dev/cu.usbmodemXXXX
import sys
import time
import serial

HEADER = b"\x03"
ACK = 0x15
MATRIX_LEDS = 64  # 8x8, fixo no protocolo do SimHub


def send(ser, cmd: bytes, payload: bytes = b""):
    ser.reset_input_buffer()
    ser.write(HEADER + cmd + payload)
    ser.flush()
    time.sleep(0.25)
    return ser.read(ser.in_waiting or 1)


def rgb_stream_mode1(colors):
    """modo 1 = todos os LEDs em sequencia, terminado por um byte 0."""
    out = bytearray([1])
    for (r, g, b) in colors:
        out += bytes([r, g, b])
    out += bytes([0])  # fim do stream
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        print(f"uso: {sys.argv[0]} <porta serial>")
        sys.exit(1)

    with serial.Serial(sys.argv[1], 115200, timeout=1) as ser:
        time.sleep(2)  # CDC nativo estabilizar

        # --- Hello: 0x03 '1' <trailer> -> responde o char de versao -----
        reply = send(ser, b"1", b"\x00")
        print(f"[hello]    {reply!r}")
        assert b"j" in reply, "Hello nao respondeu o char de versao 'j'"

        # --- Features: 0x03 '0' -> letras de capacidade + \n ------------
        reply = send(ser, b"0")
        print(f"[features] {reply!r}")
        assert b"R" in reply, "Features nao anuncia 'R' (RGB Matrix)"
        assert b"P" not in reply, "Features anuncia 'P' (SHCustomProtocol) — nao deveria"

        # --- Contagem de LEDs da fita: 0x03 '4' -> 1 byte ---------------
        reply = send(ser, b"4")
        assert len(reply) >= 1, "comando '4' nao respondeu a contagem da fita"
        strip_count = reply[0]
        print(f"[fita]     {strip_count} LEDs")

        # --- Dados RGB da fita: 0x03 '6' + stream -> ACK 0x15 -----------
        strip_colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        strip_colors += [((i * 8) % 256,) * 3 for i in range(3, strip_count)]
        reply = send(ser, b"6", rgb_stream_mode1(strip_colors[:strip_count]))
        print(f"[fita RGB] {reply!r}")
        assert ACK in reply, "comando '6' nao respondeu ACK 0x15"

        # --- Dados RGB da matriz: 0x03 'R' + stream -> ACK 0x15 ---------
        matrix_colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
        matrix_colors += [(0, 0, 0)] * (MATRIX_LEDS - 3)
        reply = send(ser, b"R", rgb_stream_mode1(matrix_colors))
        print(f"[matriz]   {reply!r}")
        assert ACK in reply, "comando 'R' nao respondeu ACK 0x15"

        # --- Confirma o que chegou nos framebuffers ---------------------
        ser.reset_input_buffer()
        ser.write(b"DUMPLEDS\r\n")
        ser.flush()
        time.sleep(0.4)
        dump = ser.read(ser.in_waiting or 1).decode(errors="replace")
        print("--- DUMPLEDS ---")
        print(dump)

        assert "M0=(255,  0,  0)" in dump or "M0=(255,0,0)" in dump.replace(" ", ""), \
            "matriz: M0 nao chegou como (255,0,0)"
        assert "LED0=(255,  0,  0)" in dump or "LED0=(255,0,0)" in dump.replace(" ", ""), \
            "fita: LED0 nao chegou como (255,0,0)"

        print("\nOK: handshake, features, fita e matriz responderam corretamente.")


if __name__ == "__main__":
    main()
