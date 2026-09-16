# Envia o protocolo Standard Serial do SimHub pra placa (firmware
# esp32s3-supermini ou simhub-test) e confere as respostas — verificação
# manual de ponta a ponta pro criterio de sucesso desta integracao ("e
# possivel enviar dados Standard Protocol... e confirmar que os valores
# RGB foram recebidos corretamente"), sem precisar instalar o SimHub.
#
# Uso:
#   ~/.platformio/penv/bin/python scripts/simhub_test_send.py /dev/cu.usbmodemXXXX
#
# (usa o Python do PlatformIO, que ja tem pyserial; ou "pip install
# pyserial" num Python qualquer)
#
# IMPORTANTE: LED_COUNT abaixo tem que bater com a contagem configurada na
# placa (comando serial "SETLEDS <n>", persistido em NVS — ver
# docs/SIMHUB_PROTOCOL.md). Rode "SETLEDS 74" uma vez (ou o valor que for)
# antes de usar este script, ou ajuste LED_COUNT pra bater com o que já
# está gravado.
import sys
import time
import serial

LED_COUNT = 74  # matriz 8x8 (64) + fita ~10 LEDs — precisa bater com o SETLEDS já gravado na placa
HEADER = bytes([0xFF] * 6)
TERMINATOR = bytes([0xFF, 0xFE, 0xFD])


def send_command(ser, cmd: bytes, expect_reply: bool, label: str):
    ser.reset_input_buffer()
    ser.write(HEADER + cmd)
    ser.flush()
    if not expect_reply:
        print(f"[{label}] enviado, sem resposta esperada")
        return None
    time.sleep(0.2)
    reply = ser.read(ser.in_waiting or 1)
    print(f"[{label}] resposta crua: {reply!r}")
    return reply


def main():
    if len(sys.argv) != 2:
        print(f"uso: {sys.argv[0]} <porta serial>")
        sys.exit(1)

    port = sys.argv[1]
    with serial.Serial(port, 115200, timeout=1) as ser:
        time.sleep(2)  # tempo pro CDC nativo estabilizar depois de abrir a porta

        proto_reply = send_command(ser, b"proto", True, "proto")
        assert proto_reply and b"SIMHUB_1.0" in proto_reply, \
            "resposta de 'proto' nao contem SIMHUB_1.0 — protocolo nao reconhecido"

        ledsc_reply = send_command(ser, b"ledsc", True, "ledsc")
        assert ledsc_reply and str(LED_COUNT).encode() in ledsc_reply, \
            f"resposta de 'ledsc' nao contem {LED_COUNT} — LED_COUNT do script != SIMHUB_LED_COUNT do firmware?"

        # Payload de teste: LED 0 = vermelho puro, LED 1 = verde puro,
        # LED 2 = azul puro, demais = uma rampa crescente simples — dá pra
        # conferir visualmente no dump do firmware que R/G/B chegaram nos
        # bytes certos, na ordem certa, sem trocar canais.
        payload = bytearray(LED_COUNT * 3)
        if LED_COUNT > 0:
            payload[0:3] = bytes([255, 0, 0])
        if LED_COUNT > 1:
            payload[3:6] = bytes([0, 255, 0])
        if LED_COUNT > 2:
            payload[6:9] = bytes([0, 0, 255])
        for i in range(3, LED_COUNT):
            v = (i * 8) % 256
            payload[i * 3:i * 3 + 3] = bytes([v, v, v])

        ser.reset_input_buffer()
        ser.write(HEADER + b"sleds" + bytes(payload) + TERMINATOR)
        ser.flush()
        print("[sleds] frame enviado, aguardando confirmacao do firmware...")

        time.sleep(2.5)
        dump = ser.read(ser.in_waiting or 1).decode(errors="replace")
        print("--- saida do firmware apos o 'sleds' ---")
        print(dump)

        assert "CONECTADO" in dump or "LED0=" in dump, \
            "firmware nao confirmou recebimento do 'sleds' (sem 'CONECTADO' nem dump de framebuffer)"
        assert "LED0=(255,  0,  0)" in dump or "LED0=(255,0,0)" in dump.replace(" ", ""), \
            "LED0 nao chegou como (255,0,0) — RGB corrompido ou fora de ordem"

        print("\nOK: proto/ledsc responderam certo e o LED0 chegou como (255,0,0).")


if __name__ == "__main__":
    main()
