# tests/test_rs_widgets.py
import sys
from PySide6.QtWidgets import QApplication, QLabel

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_widgets import RsPanel


def test_rspanel_title_and_body():
    p = RsPanel("处理工具箱", icon="cog",
                actions=[("search", "搜索"), ("bookmark", "收藏")])
    assert p.title_label.text() == "处理工具箱"
    assert len(p.action_buttons) == 2
    body = QLabel("hello")
    p.add_body_widget(body)
    assert body.parent() is not None
