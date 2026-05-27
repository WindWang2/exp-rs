"""P3 验收测试：QgsMapCanvas / MapTool / LayerTreeView (无头模式)。"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest

BUILD     = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
BUILD_PY  = os.path.join(BUILD, "src", "python")
DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LE7_TIF   = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")


@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD_PY)
    sys.path.insert(0, BUILD)
    os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
    import _antigravity_core as c
    c.init(BUILD)
    return c


def test_canvas_create(core):
    canvas = core.QgsMapCanvas()
    assert canvas is not None


def test_canvas_scale(core):
    canvas = core.QgsMapCanvas()
    assert canvas.scale() >= 0


def test_canvas_render_layer(core, tmp_path):
    canvas = core.QgsMapCanvas()
    layer = core.QgsRasterLayer(LE7_TIF, "le7")
    assert layer.isValid()
    canvas.setLayers([layer])
    canvas.setExtent(layer.extent())
    canvas.refresh()
    out = str(tmp_path / "canvas.png")
    result = canvas.saveAsImage(out)
    assert result and os.path.exists(out)


def test_maptool_pan(core):
    canvas = core.QgsMapCanvas()
    tool = core.QgsMapToolPan(canvas)
    assert tool is not None


def test_maptool_zoom(core):
    canvas = core.QgsMapCanvas()
    tool = core.QgsMapToolZoom(canvas, False)
    assert tool is not None


def test_layertreeview_create(core):
    view = core.QgsLayerTreeView()
    assert view is not None


def test_p3_exports(core):
    for name in ["QgsMapCanvas", "QgsMapTool", "QgsMapToolPan",
                 "QgsMapToolZoom", "QgsLayerTreeView"]:
        assert hasattr(core, name), f"缺少 {name}"
