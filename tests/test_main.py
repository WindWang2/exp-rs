import os
import pytest
from PySide6.QtWidgets import QApplication
import main
from main import load_stylesheet

def test_load_stylesheet_missing_file(qtbot):
    app = QApplication.instance() or QApplication([])
    # Temporarily rename file if it exists, using main.__file__
    style_path = os.path.join(os.path.dirname(main.__file__), "resources", "styles.qss")
    exists = os.path.exists(style_path)
    if exists:
        os.rename(style_path, style_path + ".bak")
    
    try:
        # Should not raise exception
        load_stylesheet(app)
    finally:
        if exists:
            os.rename(style_path + ".bak", style_path)
