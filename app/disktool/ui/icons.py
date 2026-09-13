"""Die Symbole — sie liegen jetzt bei :mod:`app.ui_icons`.

Emulator und DiskTool teilen sich einen Satz Zeichnungen (``app/icons/``) und
einen Lader; das Modul wohnt deshalb neben beiden Oberflächen statt in einer von
ihnen.  Dieser Wiederausfuhr wegen bleiben ``from app.disktool.ui.icons import
icon`` und die eingeführten Namen gültig.
"""

from app.ui_icons import GROESSEN, ICON_DIR, icon

__all__ = ["GROESSEN", "ICON_DIR", "icon"]
