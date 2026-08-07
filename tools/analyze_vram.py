import subprocess, os
from pathlib import Path

# Projektwurzel aus dem Skriptort ableiten — die Arbeitskopie darf überall liegen.
ROOT = Path(__file__).resolve().parents[1]


for f in ['core/cards/k7024/k7024.cpp', 'core/cards/k7024/k7024.h']:
    path = str(ROOT / f)
    if os.path.exists(path):
        with open(path) as fp:
            content = fp.read()
        print(f"=== {f} ===")
        print(content[:3000])
        print()
