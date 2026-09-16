Import("env")
import subprocess
import json
import sys

def find_port_by_mac(expected_mac):
    try:
        result = subprocess.run(["pio", "device", "list", "--json-output"], capture_output=True, text=True)
        devices = json.loads(result.stdout)
        for dev in devices:
            hwid = dev.get("hwid", "").lower()
            # HWID usually looks like: USB VID:PID=303A:8172 SER=dc:b4:d9:0b:51:6c LOCATION=1-1.4.4
            if expected_mac.lower() in hwid:
                return dev.get("port")
    except Exception as e:
        print(f"Error checking devices: {e}")
    return None

def before_upload(source, target, env):
    expected_mac = env.GetProjectOption("custom_expected_mac", "")
    if not expected_mac:
        return

    print(f"\n[AUTO-PORT] Looking for device with MAC: {expected_mac}...")
    port = find_port_by_mac(expected_mac)
    
    if port:
        print(f"[AUTO-PORT] Found target board at {port}!")
        env.Replace(UPLOAD_PORT=port)
    else:
        sys.stderr.write(f"\n[FATAL ERROR] Could not find board with MAC {expected_mac}!\n")
        sys.stderr.write("Are you trying to flash the wrong board? Or is it disconnected?\n")
        sys.stderr.write("Upload ABORTED to prevent flashing wrong firmware.\n\n")
        env.Exit(1)

env.AddPreAction("upload", before_upload)
