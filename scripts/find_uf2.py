Import("env")

import os
import string
import sys


def find_uf2_drive():
    for letter in string.ascii_uppercase:
        root = f"{letter}:\\"
        if os.path.isdir(root) and os.path.isfile(os.path.join(root, "INFO_UF2.TXT")):
            return root
    return None


drive = find_uf2_drive()
if drive:
    env.Replace(UPLOAD_PORT=drive)
    print(f"Wykryto dysk UF2: {drive}")
elif "upload" in COMMAND_LINE_TARGETS:
    sys.exit(
        "Blad: nie znaleziono dysku UF2. Szybko nacisnij RESET dwa razy, "
        "aby wejsc w tryb bootloadera (pojawi sie dysk USB), a nastepnie ponow upload."
    )
else:
    print("Ostrzezenie: brak dysku UF2 - pominięto.")
