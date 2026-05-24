# tests/test_rs_icons.py
import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QIcon

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_icons import rs_icon, rs_pixmap, ICON_PATHS


def test_known_icon_names_present():
    for name in ["folder", "save", "cursor", "pan", "zoomIn", "zoomOut", "zoomFit",
                 "cog", "wand", "workflow", "brain", "spark", "layers", "database",
                 "search", "filter", "refresh", "x", "chevD", "chevR", "raster",
                 "vector", "globe", "histogram", "palette", "crosshair", "bell", "user"]:
        assert name in ICON_PATHS, name


def test_rs_pixmap_is_non_null_and_sized():
    pm = rs_pixmap("folder", size=14, color="#1f6feb")
    assert not pm.isNull()
    # 2x DPR: logical size stays 14, physical pixels are doubled
    assert pm.deviceIndependentSize().width() == 14
    assert pm.width() == 28


def test_rs_icon_returns_icon():
    assert isinstance(rs_icon("cog", 16, "#2f3640"), QIcon)


def test_unknown_icon_is_blank_not_crash():
    # unknown name returns a valid (blank/transparent) pixmap without raising
    assert not rs_pixmap("does-not-exist", 14, "#000").isNull()
