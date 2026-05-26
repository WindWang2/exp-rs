"""P2 验收测试：symbology/labeling/render-context pybind11 绑定。"""
import os
import sys
import pytest

BUILD = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
BUILD_PY = os.path.join(BUILD, "src", "python")
DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LE7_TIF = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")


@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD_PY)
    sys.path.insert(0, BUILD)
    os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
    import _antigravity_core as c
    c.init(BUILD)
    return c


# ── QgsRenderContext ──────────────────────────────────────────────────────────

def test_render_context_default(core):
    rc = core.QgsRenderContext()
    assert rc.scaleFactor() >= 0
    assert rc.rendererScale() >= 0
    assert not rc.isValid()   # 默认构造没有 painter


# ── QgsSymbol ─────────────────────────────────────────────────────────────────

def test_symbol_classes_exist(core):
    assert hasattr(core, "QgsSymbol")
    assert hasattr(core, "QgsLineSymbol")
    assert hasattr(core, "QgsFillSymbol")
    assert hasattr(core, "QgsMarkerSymbol")


# ── QgsFeatureRenderer ────────────────────────────────────────────────────────

def test_renderer_class_exists(core):
    assert hasattr(core, "QgsFeatureRenderer")
    assert hasattr(core, "QgsSingleSymbolRenderer")


# ── QgsPalLayerSettings ───────────────────────────────────────────────────────

def test_pal_settings_default(core):
    settings = core.QgsPalLayerSettings()
    assert settings.drawLabels is True  # 默认启用
    assert isinstance(settings.fieldName, str)


def test_pal_settings_mutate(core):
    settings = core.QgsPalLayerSettings()
    settings.fieldName = "name"
    assert settings.fieldName == "name"
    settings.drawLabels = False
    assert settings.drawLabels is False


# ── QgsVectorLayer renderer ───────────────────────────────────────────────────

def _find_test_shp():
    for root_dir, _dirs, files in os.walk(os.path.join(DATA_ROOT, "data")):
        for f in files:
            if f.endswith(".shp"):
                return os.path.join(root_dir, f)
    return None


_TEST_SHP = _find_test_shp()


@pytest.mark.skipif(_TEST_SHP is None, reason="data/ 目录下没有 .shp 文件")
def test_vector_layer_renderer(core):
    layer = core.QgsVectorLayer(_TEST_SHP, "vec")
    assert layer.isValid()
    renderer = layer.renderer()
    if renderer is not None:
        assert len(renderer.type()) > 0


@pytest.mark.skipif(_TEST_SHP is None, reason="data/ 目录下没有 .shp 文件")
def test_vector_layer_labeling_none_by_default(core):
    layer = core.QgsVectorLayer(_TEST_SHP, "vec")
    assert layer.isValid()
    # 未配置标注时返回 None
    lab = layer.labeling()
    assert lab is None or hasattr(lab, "drawLabels")


# ── 模块导出完整性 ─────────────────────────────────────────────────────────────

def test_p2_exports(core):
    expected = [
        "QgsRenderContext", "QgsSymbol", "QgsLineSymbol", "QgsFillSymbol",
        "QgsMarkerSymbol", "QgsFeatureRenderer", "QgsSingleSymbolRenderer",
        "QgsPalLayerSettings",
    ]
    for name in expected:
        assert hasattr(core, name), f"缺少 {name}"
