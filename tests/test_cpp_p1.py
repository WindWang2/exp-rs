"""P1 验收测试：layer/project/expression pybind11 绑定。"""
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


# ── QgsRectangle ──────────────────────────────────────────────────────────────

def test_rectangle_attrs(core):
    layer = core.QgsRasterLayer(LE7_TIF)
    ext = layer.extent()
    assert ext.width() > 0
    assert ext.height() > 0
    assert not ext.isNull()
    assert "QgsRectangle" in repr(ext)


# ── QgsRasterLayer（正式绑定）────────────────────────────────────────────────

def test_raster_layer_binding(core):
    layer = core.QgsRasterLayer(LE7_TIF, "le7")
    assert layer.isValid()
    assert layer.width() > 7000
    assert layer.height() > 7000
    assert layer.bandCount() >= 1
    assert layer.crs().isValid()
    assert layer.crs().authid().startswith("EPSG:")


def test_raster_layer_name(core):
    layer = core.QgsRasterLayer(LE7_TIF, "my_band")
    assert layer.name() == "my_band"
    assert layer.type() == 1  # QgsMapLayerType::RasterLayer == 1


def test_raster_layer_extent_values(core):
    layer = core.QgsRasterLayer(LE7_TIF)
    ext = layer.extent()
    # LE7 UTM zone 50N — xMin 应在东经范围内
    assert ext.xMinimum() > 100000
    assert ext.yMinimum() > 0


# ── QgsCRS ────────────────────────────────────────────────────────────────────

def test_crs_binding(core):
    layer = core.QgsRasterLayer(LE7_TIF)
    crs = layer.crs()
    assert crs.isValid()
    assert "EPSG" in crs.authid()
    assert len(crs.description()) > 0


def test_crs_standalone(core):
    crs = core.QgsCRS()
    assert not crs.isValid()  # 默认构造无效


# ── QgsProject ────────────────────────────────────────────────────────────────

def test_project_create(core):
    proj = core.QgsProject()
    assert proj is not None


def test_project_add_raster(core):
    proj = core.QgsProject()
    layer = proj.addRasterLayer(LE7_TIF, "b4")
    assert layer is not None
    assert layer.isValid()
    ids = proj.mapLayerIds()
    assert len(ids) >= 1


def test_project_layer_tree(core):
    proj = core.QgsProject()
    proj.addRasterLayer(LE7_TIF, "b4")
    root = proj.layerTreeRoot()
    assert root is not None
    ids = root.layerIds()
    assert len(ids) >= 1


def test_project_add_invalid_returns_none(core):
    proj = core.QgsProject()
    layer = proj.addRasterLayer("/nonexistent/path.tif", "bad")
    assert layer is None


# ── QgsGeometry ───────────────────────────────────────────────────────────────

def test_geometry_null(core):
    geom = core.QgsGeometry()
    assert geom.isNull()


# ── QgsExpression ─────────────────────────────────────────────────────────────

def test_expression_valid(core):
    expr = core.QgsExpression("1 + 1")
    assert expr.isValid()
    assert not expr.hasParserError()


def test_expression_invalid(core):
    expr = core.QgsExpression("((( invalid syntax")
    assert not expr.isValid()
    assert expr.hasParserError()
    assert len(expr.parserErrorString()) > 0


def test_expression_evaluate_scalar(core):
    expr = core.QgsExpression("2 * 21")
    ctx = core.QgsExpressionContext()
    result = expr.evaluate(ctx)
    assert result == 42


def test_expression_evaluate_string(core):
    expr = core.QgsExpression("'hello' || ' world'")
    ctx = core.QgsExpressionContext()
    result = expr.evaluate(ctx)
    assert result == "hello world"


def test_expression_evaluate_float(core):
    expr = core.QgsExpression("sqrt(4.0)")
    ctx = core.QgsExpressionContext()
    result = expr.evaluate(ctx)
    assert abs(result - 2.0) < 1e-9


# ── QgsVectorLayer（如有测试矢量数据则运行）─────────────────────────────────

def _find_test_shp():
    for root_dir, _dirs, files in os.walk(os.path.join(DATA_ROOT, "data")):
        for f in files:
            if f.endswith(".shp"):
                return os.path.join(root_dir, f)
    return None


_TEST_SHP = _find_test_shp()


@pytest.mark.skipif(_TEST_SHP is None, reason="data/ 目录下没有 .shp 文件")
def test_vector_layer_open(core):
    layer = core.QgsVectorLayer(_TEST_SHP, "vec")
    assert layer.isValid()
    assert layer.featureCount() >= 0
    fields = layer.fields()
    assert fields.count() >= 0
