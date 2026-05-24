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


# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsTabBar


def test_rstabbar_active_and_signal():
    bar = RsTabBar([("info", "信息", None, None),
                    ("symbol", "符号化", None, None)], active="info")
    seen = []
    bar.tab_changed.connect(seen.append)
    assert bar.active_id == "info"
    bar.set_active("symbol")
    assert bar.active_id == "symbol"
    assert seen == ["symbol"]


# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsToolBar


def test_rstoolbar_groups_and_trigger():
    tb = RsToolBar()
    tb.add_group([{"id": "open", "icon": "folder", "tip": "打开"},
                  {"id": "save", "icon": "save", "tip": "保存"}])
    tb.add_group([{"id": "pan", "icon": "pan", "checkable": True, "checked": True},
                  {"id": "zin", "icon": "zoomIn", "checkable": True}],
                 exclusive=True)
    fired = []
    tb.triggered.connect(fired.append)
    tb.button("open").click()
    assert fired == ["open"]
    # exclusive: clicking zin unchecks pan
    tb.button("zin").click()
    assert tb.button("zin").isChecked() and not tb.button("pan").isChecked()


# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsStatusBar


def test_rsstatusbar_setters():
    sb = RsStatusBar()
    sb.set_coord("116.4074° E, 39.9042° N")
    sb.set_scale("1 : 50,000")
    sb.set_crs("EPSG:4326 — WGS 84")
    assert "116.4074" in sb.coord_label.text()
    assert "50,000" in sb.scale_label.text()
    assert "EPSG:4326" in sb.crs_label.text()


# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsRowDelegate, RsSearchInput, ROLE_SWATCH, ROLE_META, ROLE_ICON
from PySide6.QtGui import QStandardItemModel, QStandardItem
from PySide6.QtWidgets import QStyleOptionViewItem


def test_rowdelegate_sizehint_height():
    d = RsRowDelegate()
    model = QStandardItemModel()
    it = QStandardItem("NDVI_2025.tif")
    it.setData("#7dd35c", ROLE_SWATCH)
    it.setData("raster", ROLE_META)
    it.setData("raster", ROLE_ICON)
    model.appendRow(it)
    sz = d.sizeHint(QStyleOptionViewItem(), model.index(0, 0))
    assert sz.height() == 22


def test_search_input_text():
    s = RsSearchInput("搜索 1,247 个算法…")
    s.line.setText("ndvi")
    assert s.text() == "ndvi"
