Import("env")

import sys

from serial.tools import list_ports

VID_PID = (0x239A, 0x00B3)  # Adafruit nRF52 bootloader w trybie DFU (nice!nano v2)
NAME_FILTER = "nRF52"


def find_port():
    ports = list_ports.comports()
    if VID_PID:
        for p in ports:
            if (p.vid, p.pid) == VID_PID:
                return p
    for p in ports:
        if p.vid is None:
            continue
        if NAME_FILTER.lower() in p.description.lower():
            return p
    return None


port = find_port()
if port:
    env.Replace(UPLOAD_PORT=port.device)
    print(f"Wykryto port DFU: {port.device} ({port.description})")
elif "upload" in COMMAND_LINE_TARGETS:
    print("Wykryte porty:")
    for p in list_ports.comports():
        print(" ", p.device, "-", p.description, f"(VID:PID {p.vid}:{p.pid})")
    sys.exit("Blad: nie znaleziono portu DFU. Podlacz plytke (tryb bootloadera) i sprobuj ponownie.")
else:
    print("Ostrzezenie: nie znaleziono portu DFU - pominięto (upload nie zadziala).")
