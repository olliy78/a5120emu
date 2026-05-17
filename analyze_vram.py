import subprocess, os

for f in ['core/cards/k7024/k7024.cpp', 'core/cards/k7024/k7024.h']:
    path = f'/home/olliy/projects/a5120emu/{f}'
    if os.path.exists(path):
        with open(path) as fp:
            content = fp.read()
        print(f"=== {f} ===")
        print(content[:3000])
        print()
