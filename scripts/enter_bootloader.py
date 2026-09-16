# Coloca a placa em modo download antes do upload por USB, sem botao fisico.
#
# Com ARDUINO_USB_MODE=0 (necessario para HID) o USB-Serial-JTAG nao existe, e
# o esptool nao consegue resetar a placa pelo CDC do TinyUSB — o upload falha
# com "No serial data received". A saida e o proprio firmware entrar em modo
# download: mandamos "BOOTLOADER" pela serial e esperamos a porta de download
# aparecer.
Import("env")
import glob, time, sys

def porta_app():
    # porta do CDC do TinyUSB (nome contem o MAC da placa)
    mac = env.GetProjectOption("custom_expected_mac", "").upper()
    for p in glob.glob("/dev/cu.usbmodem*"):
        if mac and mac in p.upper():
            return p
    return None

def porta_download():
    # em modo download aparece a porta do USB-Serial-JTAG (sem o MAC no nome)
    mac = env.GetProjectOption("custom_expected_mac", "").upper()
    for p in glob.glob("/dev/cu.usbmodem*"):
        if not mac or mac not in p.upper():
            return p
    return None

def before_upload(source, target, env):
    if porta_download():
        print("[BOOTLOADER] ja esta em modo download")
        return

    p = porta_app()
    if not p:
        print("[BOOTLOADER] porta do app nao encontrada; seguindo assim mesmo")
        return

    try:
        import serial
    except ImportError:
        print("[BOOTLOADER] pyserial ausente; seguindo sem comandar reboot")
        return

    print(f"[BOOTLOADER] mandando comando em {p}...")
    try:
        # Abrir sem tocar em DTR/RTS: o CDC do Arduino usa essas linhas no
        # protocolo de reset, e um toggle na abertura reinicia a placa antes
        # de ela ler o comando.
        s = serial.Serial()
        s.port = p; s.baudrate = 115200; s.timeout = 1
        s.dtr = False; s.rts = False
        s.open()
        time.sleep(0.5)
        s.write(b"BOOTLOADER\n")
        s.flush()
        time.sleep(0.5)
        s.close()
    except Exception as e:
        print(f"[BOOTLOADER] falha ao enviar: {e}")
        return

    # espera a porta de download aparecer
    for _ in range(40):
        time.sleep(0.25)
        d = porta_download()
        if d:
            print(f"[BOOTLOADER] modo download em {d}")
            env.Replace(UPLOAD_PORT=d)
            return
    print("[BOOTLOADER] a porta de download nao apareceu")

env.AddPreAction("upload", before_upload)
