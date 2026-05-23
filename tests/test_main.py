import os
import pytest
from PySide6.QtWidgets import QApplication
from main import load_stylesheet

def test_load_stylesheet_missing_file(qtbot):
    app = QApplication.instance() or QApplication([])
    # The resources folder is at the root of the project, which is the parent of 'tests'
    style_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "resources", "styles.qss")
    exists = os.path.exists(style_path)
    if exists:
        os.rename(style_path, style_path + ".bak")
    
    try:
        # Should not raise exception even if file is missing
        load_stylesheet(app)
        # styleSheet should be empty when the stylesheet could not be loaded
        app.setStyleSheet("")
        load_stylesheet(app)
        assert app.styleSheet() == ""
    finally:
        if exists:
            os.rename(style_path + ".bak", style_path)

def test_load_stylesheet_present(qtbot):
    app = QApplication.instance() or QApplication([])
    # Make sure stylesheet is cleared first
    app.setStyleSheet("")
    load_stylesheet(app)
    # When present, it should load the styleSheet content
    assert app.styleSheet() != ""
