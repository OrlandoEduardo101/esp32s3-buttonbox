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
# A contagem de LEDs é descoberta em runtime via "ledsc" (não fica mais
# fixa no script) — reflete o que estiver gravado em NVS na placa,
# ajustável a qualquer momento com o comando serial "SETLEDS <n>" (ver
# docs/SIMHUB_PROTOCOL.md). Não precisa mais manter esse número
# sincronizado à mão entre o script e o firmware.
import re
import sys
import time
import serial

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
        match = re.search(rb"\d+", ledsc_reply or b"")
        assert match, f"resposta de 'ledsc' nao contem um numero: {ledsc_reply!r}"
        LED_COUNT = int(match.group())
        print(f"[ledsc] LED_COUNT descoberto em runtime: {LED_COUNT}")

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
        print("[sleds] frame enviado")
        time.sleep(0.3)

        # DUMPLEDS é um comando de texto NOSSO (não faz parte do protocolo
        # do SimHub) — pede pro firmware imprimir o framebuffer recebido,
        # útil tanto no firmware de produção quanto no simhub-test isolado
        # (que também imprime sozinho, mas responder ao DUMPLEDS não atrapalha).
        ser.reset_input_buffer()
        ser.write(b"DUMPLEDS\r\n")
        ser.flush()
        time.sleep(0.3)
        dump = ser.read(ser.in_waiting or 1).decode(errors="replace")
        print("--- resposta do DUMPLEDS ---")
        print(dump)

        assert "LED0=" in dump, \
            "firmware nao respondeu ao DUMPLEDS (build antiga sem esse comando?)"
        assert "LED0=(255,  0,  0)" in dump or "LED0=(255,0,0)" in dump.replace(" ", ""), \
            "LED0 nao chegou como (255,0,0) — RGB corrompido ou fora de ordem"

        print("\nOK: proto/ledsc responderam certo e o LED0 chegou como (255,0,0).")


if __name__ == "__main__":
    main()
