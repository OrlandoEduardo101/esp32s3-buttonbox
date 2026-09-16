# Descobre o IP da placa pelo MAC antes de um upload OTA.
#
# Por que: o espota resolve o destino por socket comum, que NAO consulta mDNS,
# entao ".local" nao funciona; e o IP muda quando o DHCP renova. Procurar pelo
# MAC na tabela ARP resolve os dois problemas.
Import("env")
import subprocess, re

MAC = env.GetProjectOption("custom_board_mac", "").lower()

def find_ip_by_mac(mac):
    try:
        out = subprocess.run(["arp", "-a"], capture_output=True, text=True).stdout
    except Exception:
        return None
    for line in out.splitlines():
        if mac in line.lower():
            m = re.search(r"\((\d+\.\d+\.\d+\.\d+)\)", line)
            if m:
                return m.group(1)
    return None

def before_upload(source, target, env):
    if not MAC:
        return
    # popula a tabela ARP antes de consultar
    subprocess.run(["ping", "-c", "2", "-W", "500", "255.255.255.255"],
                   capture_output=True)
    ip = find_ip_by_mac(MAC)
    if ip:
        print(f"[OTA] placa {MAC} encontrada em {ip}")
        env.Replace(UPLOAD_PORT=ip)
    else:
        print(f"[OTA] MAC {MAC} nao encontrado na rede — usando upload_port do ini")

env.AddPreAction("upload", before_upload)
