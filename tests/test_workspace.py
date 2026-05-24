# tests/test_workspace.py
import sys
from PySide6.QtWidgets import QApplication, QSplitter

_app = QApplication.instance() or QApplication(sys.argv)

from gui.workspace import RsWorkspace
from gui.rs_widgets import RsToolBar, RsStatusBar, RsConsole, RsPropertyPanel
from gui.map_overlay import MapOverlayContainer


def test_workspace_exposes_parts():
    w = RsWorkspace()
    assert isinstance(w.toolbar, RsToolBar)
    assert isinstance(w.status, RsStatusBar)
    assert isinstance(w.console, RsConsole)
    assert isinstance(w.property_panel, RsPropertyPanel)
    assert isinstance(w.map_container, MapOverlayContainer)
    assert w.canvas is w.map_container.canvas
    # three top-level columns in the horizontal splitter
    assert w.main_splitter.count() == 3


def test_workspace_default_sizes():
    w = RsWorkspace()
    w.resize(1440, 900)
    w.show()
    _app.processEvents()
    sizes = w.main_splitter.sizes()
    assert sizes[0] > 0 and sizes[2] > 0  # left & right docks visible
