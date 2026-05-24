"""Tests for Feature Renderer Hierarchy (Task 21).

QgsRendererCategory, QgsCategorizedSymbolRenderer, and refactored QgsSingleSymbolRenderer.
"""
import pytest
from unittest.mock import MagicMock, patch

from PySide6.QtGui import QColor, QPainter, QImage
from PySide6.QtCore import Qt, QSize

from core.vector.qgsvectorrenderer import (
    QgsFeatureRenderer,
    QgsSingleSymbolRenderer,
    QgsRendererCategory,
    QgsCategorizedSymbolRenderer,
)
from core.symbology.qgssymbol import QgsSymbol
from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer
from core.qgsmapsettings import QgsMapSettings
from core.qgsrectangle import QgsRectangle


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_settings():
    """Create a minimal QgsMapSettings for testing."""
    settings = QgsMapSettings()
    settings.extent = QgsRectangle(0, 0, 1000, 1000)
    settings.output_size = QSize(500, 500)
    return settings


def _make_polygon_feature(**extra_attrs):
    """Dict-style feature with a simple triangle polygon."""
    from shapely.geometry import Polygon
    feat = {
        "shape": Polygon([(10, 10), (50, 10), (30, 40), (10, 10)]),
        "id": 1,
        "properties": {},
    }
    feat["properties"].update(extra_attrs)
    return feat


def _make_point_feature(**extra_attrs):
    """Dict-style feature with a simple point."""
    from shapely.geometry import Point
    feat = {
        "shape": Point(50, 50),
        "id": 2,
        "properties": {},
    }
    feat["properties"].update(extra_attrs)
    return feat


def _make_symbol():
    """Create a basic fill symbol for testing."""
    fill_layer = QgsSimpleFillSymbolLayer()
    return QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])


# ---------------------------------------------------------------------------
# QgsRendererCategory
# ---------------------------------------------------------------------------

class TestRendererCategory:

    def test_renderer_category(self):
        """Value, label, and symbol accessors work correctly."""
        sym = _make_symbol()
        cat = QgsRendererCategory("highway", "Highways", sym)

        assert cat.value() == "highway"
        assert cat.label() == "Highways"
        assert cat.symbol() is sym

    def test_renderer_category_setters(self):
        """setValue, setLabel, setSymbol work correctly."""
        sym1 = _make_symbol()
        sym2 = _make_symbol()
        cat = QgsRendererCategory("a", "A", sym1)

        cat.setValue("b")
        assert cat.value() == "b"

        cat.setLabel("B")
        assert cat.label() == "B"

        cat.setSymbol(sym2)
        assert cat.symbol() is sym2


# ---------------------------------------------------------------------------
# QgsCategorizedSymbolRenderer
# ---------------------------------------------------------------------------

class TestCategorizedSymbolRenderer:

    def test_categorized_renderer_add_category(self):
        """addCategory appends to the categories list."""
        renderer = QgsCategorizedSymbolRenderer("type")
        sym = _make_symbol()
        cat = QgsRendererCategory("road", "Road", sym)

        renderer.addCategory(cat)
        assert len(renderer.categories()) == 1
        assert renderer.categories()[0] is cat

    def test_categorized_renderer_remove_category(self):
        """removeCategory removes by index."""
        renderer = QgsCategorizedSymbolRenderer("type")
        cat1 = QgsRendererCategory("a", "A", _make_symbol())
        cat2 = QgsRendererCategory("b", "B", _make_symbol())
        renderer.addCategory(cat1)
        renderer.addCategory(cat2)

        assert len(renderer.categories()) == 2
        renderer.removeCategory(0)
        assert len(renderer.categories()) == 1
        assert renderer.categories()[0].value() == "b"

    def test_categorized_renderer_set_categories(self):
        """setCategories replaces all categories."""
        renderer = QgsCategorizedSymbolRenderer("type")
        cat1 = QgsRendererCategory("a", "A", _make_symbol())
        cat2 = QgsRendererCategory("b", "B", _make_symbol())

        renderer.setCategories([cat1, cat2])
        assert len(renderer.categories()) == 2

    def test_categorized_renderer_match(self):
        """categoryForFeature finds the correct matching category."""
        renderer = QgsCategorizedSymbolRenderer("road_type")
        sym_highway = _make_symbol()
        sym_local = _make_symbol()
        cat_hw = QgsRendererCategory("highway", "Highway", sym_highway)
        cat_local = QgsRendererCategory("local", "Local", sym_local)
        renderer.addCategory(cat_hw)
        renderer.addCategory(cat_local)

        feature = _make_polygon_feature(road_type="highway")
        matched = renderer.categoryForFeature(feature)
        assert matched is cat_hw
        assert matched.symbol() is sym_highway

    def test_categorized_renderer_no_match(self):
        """categoryForFeature returns None when no category matches."""
        renderer = QgsCategorizedSymbolRenderer("road_type")
        renderer.addCategory(QgsRendererCategory("highway", "Highway", _make_symbol()))

        feature = _make_polygon_feature(road_type="residential")
        matched = renderer.categoryForFeature(feature)
        assert matched is None

    def test_categorized_renderer_attr_name(self):
        """attrName / setAttrName work correctly."""
        renderer = QgsCategorizedSymbolRenderer("type")
        assert renderer.attrName() == "type"
        renderer.setAttrName("category")
        assert renderer.attrName() == "category"

    def test_categorized_renderer_render(self):
        """Render using matching category's symbol."""
        renderer = QgsCategorizedSymbolRenderer("road_type")
        sym = _make_symbol()
        cat = QgsRendererCategory("highway", "Highway", sym)
        renderer.addCategory(cat)

        feature = _make_polygon_feature(road_type="highway")
        painter = MagicMock(spec=QPainter)
        settings = _make_settings()

        # Should not raise
        renderer.render_feature(feature, painter, settings)

    def test_categorized_renderer_default_symbol(self):
        """Uses default symbol for unmatched features."""
        default_sym = _make_symbol()
        renderer = QgsCategorizedSymbolRenderer("road_type", default_symbol=default_sym)
        renderer.addCategory(QgsRendererCategory("highway", "Highway", _make_symbol()))

        feature = _make_polygon_feature(road_type="unknown_value")
        painter = MagicMock(spec=QPainter)
        settings = _make_settings()

        # Should not raise -- uses default symbol
        renderer.render_feature(feature, painter, settings)

    def test_categorized_renderer_no_match_no_default(self):
        """No category match and no default symbol -- no crash."""
        renderer = QgsCategorizedSymbolRenderer("road_type")
        renderer.addCategory(QgsRendererCategory("highway", "Highway", _make_symbol()))

        feature = _make_polygon_feature(road_type="unknown")
        painter = MagicMock(spec=QPainter)
        settings = _make_settings()

        # Should not raise -- nothing to render, just returns
        renderer.render_feature(feature, painter, settings)


# ---------------------------------------------------------------------------
# QgsSingleSymbolRenderer (refactored)
# ---------------------------------------------------------------------------

class TestSingleSymbolRenderer:

    def test_single_symbol_renderer_default(self):
        """Creates with a default QgsSymbol when no args given."""
        renderer = QgsSingleSymbolRenderer()
        assert renderer.symbol() is not None
        assert isinstance(renderer.symbol(), QgsSymbol)
        assert renderer.symbol().symbolLayerCount() >= 1

    def test_single_symbol_renderer_custom(self):
        """Accepts a custom QgsSymbol."""
        sym = _make_symbol()
        renderer = QgsSingleSymbolRenderer(symbol=sym)
        assert renderer.symbol() is sym

    def test_single_symbol_renderer_set_symbol(self):
        """setSymbol replaces the internal symbol."""
        renderer = QgsSingleSymbolRenderer()
        new_sym = _make_symbol()
        renderer.setSymbol(new_sym)
        assert renderer.symbol() is new_sym

    def test_single_symbol_renderer_backward_compat(self):
        """Old color kwargs still work for backward compatibility."""
        renderer = QgsSingleSymbolRenderer(
            color=QColor(0, 255, 0, 100),
            stroke_color=QColor(0, 255, 0),
            stroke_width=2,
        )
        sym = renderer.symbol()
        assert sym is not None
        assert isinstance(sym, QgsSymbol)
        # The symbol should have a fill layer with the given colors
        layer = sym.symbolLayer(0)
        assert isinstance(layer, QgsSimpleFillSymbolLayer)
        assert layer.color() == QColor(0, 255, 0, 100)
        assert layer.strokeColor() == QColor(0, 255, 0)
        assert layer.strokeWidth() == 2

    def test_single_symbol_renderer_render(self):
        """Renders feature through the internal symbol."""
        sym = _make_symbol()
        renderer = QgsSingleSymbolRenderer(symbol=sym)

        feature = _make_polygon_feature()
        painter = MagicMock(spec=QPainter)
        settings = _make_settings()

        # Should not raise
        renderer.render_feature(feature, painter, settings)

    def test_single_symbol_renderer_is_abstract_subclass(self):
        """QgsSingleSymbolRenderer is a QgsFeatureRenderer."""
        renderer = QgsSingleSymbolRenderer()
        assert isinstance(renderer, QgsFeatureRenderer)

    def test_categorized_renderer_is_abstract_subclass(self):
        """QgsCategorizedSymbolRenderer is a QgsFeatureRenderer."""
        renderer = QgsCategorizedSymbolRenderer("type")
        assert isinstance(renderer, QgsFeatureRenderer)
