# tests/test_rs_fonts.py
import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QFontDatabase

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_fonts import load_fonts


def test_load_fonts_registers_plex_families():
    families = load_fonts()
    joined = " ".join(QFontDatabase.families())
    assert any("Plex Sans" in f for f in families), families
    assert any("Plex Mono" in f for f in families), families
    assert "IBM Plex Sans" in joined
