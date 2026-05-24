# gui/rs_fonts.py
import os
from PySide6.QtGui import QFontDatabase

_FONT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "resources", "fonts")


def load_fonts():
    """Register bundled IBM Plex ttf files. Returns the list of loaded family names."""
    families = []
    if not os.path.isdir(_FONT_DIR):
        return families
    for name in sorted(os.listdir(_FONT_DIR)):
        if not name.lower().endswith((".ttf", ".otf")):
            continue
        fid = QFontDatabase.addApplicationFont(os.path.join(_FONT_DIR, name))
        if fid != -1:
            families.extend(QFontDatabase.applicationFontFamilies(fid))
    return families
