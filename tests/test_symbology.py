"""Tests for QgsSymbolLayer hierarchy (Task 20)."""
import pytest
from unittest.mock import MagicMock

from PySide6.QtGui import QColor, QPainter, QImage, QPainterPath
from PySide6.QtCore import Qt, QSize, QPointF

from core.symbology.qgssymbollayer import (
    QgsSymbolLayer,
    QgsSimpleFillSymbolLayer,
    QgsSimpleLineSymbolLayer,
    QgsSimpleMarkerSymbolLayer,
)
from core.symbology.qgssymbol import QgsSymbol
from core.qgsmaptopixel import QgsMapToPixel
from core.qgsrectangle import QgsRectangle


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_mtp():
    """Create a QgsMapToPixel: extent 0,0->1000,1000 with 500x500 output."""
    extent = QgsRectangle(0, 0, 1000, 1000)
    size = QSize(500, 500)
    return QgsMapToPixel.fromSettings(extent, size)


def _make_render_context():
    """Create a minimal render context mock with mapToPixel()."""
    ctx = MagicMock()
    ctx.mapToPixel.return_value = _make_mtp()
    return ctx


def _make_polygon_feature():
    """Dict-style feature with a simple triangle polygon."""
    from shapely.geometry import Polygon
    return {
        "shape": Polygon([(10, 10), (50, 10), (30, 40), (10, 10)]),
        "id": 1,
    }


def _make_line_feature():
    """Dict-style feature with a simple line."""
    from shapely.geometry import LineString
    return {
        "shape": LineString([(10, 10), (50, 50), (100, 25)]),
        "id": 2,
    }


def _make_point_feature():
    """Dict-style feature with a simple point."""
    from shapely.geometry import Point
    return {
        "shape": Point(50, 50),
        "id": 3,
    }


# ---------------------------------------------------------------------------
# QgsSimpleFillSymbolLayer
# ---------------------------------------------------------------------------

class TestSimpleFillSymbolLayer:

    def test_simple_fill_symbol_layer(self):
        """type is Fill, color defaults are set."""
        layer = QgsSimpleFillSymbolLayer()
        assert layer.type() == QgsSymbolLayer.SymbolType.Fill
        assert layer.color() is not None
        assert layer.color().isValid()

    def test_fill_color_property(self):
        """fill_color can be get/set."""
        layer = QgsSimpleFillSymbolLayer()
        c = QColor(0, 255, 0, 128)
        layer.setColor(c)
        assert layer.color() == c

    def test_stroke_color_property(self):
        """stroke_color can be get/set."""
        layer = QgsSimpleFillSymbolLayer()
        c = QColor(0, 0, 255)
        layer.setStrokeColor(c)
        assert layer.strokeColor() == c

    def test_stroke_width_property(self):
        """stroke_width can be get/set."""
        layer = QgsSimpleFillSymbolLayer()
        layer.setStrokeWidth(3)
        assert layer.strokeWidth() == 3

    def test_brush_style_property(self):
        """brush_style can be get/set."""
        layer = QgsSimpleFillSymbolLayer()
        layer.setBrushStyle(Qt.Dense3Pattern)
        assert layer.brushStyle() == Qt.Dense3Pattern


# ---------------------------------------------------------------------------
# QgsSimpleLineSymbolLayer
# ---------------------------------------------------------------------------

class TestSimpleLineSymbolLayer:

    def test_simple_line_symbol_layer(self):
        """type is Line, color defaults are set."""
        layer = QgsSimpleLineSymbolLayer()
        assert layer.type() == QgsSymbolLayer.SymbolType.Line
        assert layer.color() is not None
        assert layer.color().isValid()

    def test_stroke_color_property(self):
        """stroke_color can be get/set."""
        layer = QgsSimpleLineSymbolLayer()
        c = QColor(0, 128, 255)
        layer.setColor(c)
        assert layer.color() == c

    def test_stroke_width_property(self):
        """stroke_width can be get/set."""
        layer = QgsSimpleLineSymbolLayer()
        layer.setStrokeWidth(4)
        assert layer.strokeWidth() == 4

    def test_pen_style_property(self):
        """pen_style can be get/set."""
        layer = QgsSimpleLineSymbolLayer()
        layer.setPenStyle(Qt.DashLine)
        assert layer.penStyle() == Qt.DashLine


# ---------------------------------------------------------------------------
# QgsSimpleMarkerSymbolLayer
# ---------------------------------------------------------------------------

class TestSimpleMarkerSymbolLayer:

    def test_simple_marker_symbol_layer(self):
        """type is Marker, color defaults are set."""
        layer = QgsSimpleMarkerSymbolLayer()
        assert layer.type() == QgsSymbolLayer.SymbolType.Marker
        assert layer.color() is not None
        assert layer.color().isValid()

    def test_size_property(self):
        """size can be get/set."""
        layer = QgsSimpleMarkerSymbolLayer()
        layer.setSize(10.0)
        assert layer.size() == 10.0

    def test_shape_property(self):
        """shape can be get/set."""
        layer = QgsSimpleMarkerSymbolLayer()
        layer.setShape("square")
        assert layer.shape() == "square"


# ---------------------------------------------------------------------------
# Enable/disable and clone
# ---------------------------------------------------------------------------

class TestSymbolLayerEnableDisable:

    def test_symbol_layer_enable_disable(self):
        """isEnabled/setEnabled toggles correctly."""
        layer = QgsSimpleFillSymbolLayer()
        assert layer.isEnabled() is True  # default
        layer.setEnabled(False)
        assert layer.isEnabled() is False
        layer.setEnabled(True)
        assert layer.isEnabled() is True

    def test_symbol_layer_clone(self):
        """clone creates an independent copy."""
        layer = QgsSimpleFillSymbolLayer()
        layer.setColor(QColor(10, 20, 30))
        layer.setStrokeWidth(7)

        clone = layer.clone()
        assert clone is not layer
        assert clone.color() == layer.color()
        assert clone.strokeWidth() == layer.strokeWidth()

        # Mutating clone does not affect original
        clone.setColor(QColor(255, 255, 255))
        assert clone.color() != layer.color()


# ---------------------------------------------------------------------------
# QgsSymbol (container)
# ---------------------------------------------------------------------------

class TestQgsSymbol:

    def test_symbol_from_layers(self):
        """Constructor with layers."""
        layer1 = QgsSimpleFillSymbolLayer()
        layer2 = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [layer1, layer2])
        assert sym.type() == QgsSymbol.SymbolType.Fill
        assert sym.symbolLayerCount() == 2

    def test_symbol_add_layer(self):
        """addSymbolLayer increases count and returns True."""
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill)
        assert sym.symbolLayerCount() == 0
        result = sym.addSymbolLayer(QgsSimpleFillSymbolLayer())
        assert result is True
        assert sym.symbolLayerCount() == 1

    def test_symbol_remove_layer(self):
        """removeSymbolLayer decreases count and returns True."""
        layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [layer])
        assert sym.symbolLayerCount() == 1
        result = sym.removeSymbolLayer(0)
        assert result is True
        assert sym.symbolLayerCount() == 0

    def test_symbol_remove_layer_invalid_index(self):
        """removeSymbolLayer with invalid index returns False."""
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill)
        assert sym.removeSymbolLayer(0) is False
        assert sym.removeLayer(99) is False

    def test_symbol_render_feature(self):
        """Renders without error using mock painter."""
        layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [layer])

        painter = MagicMock(spec=QPainter)
        render_ctx = _make_render_context()
        feature = _make_polygon_feature()

        # Should not raise
        sym.renderFeature(feature, painter, render_ctx)

    def test_symbol_render_feature_skips_disabled(self):
        """Disabled layers are not called during render."""
        layer = QgsSimpleFillSymbolLayer()
        layer.setEnabled(False)
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [layer])

        painter = MagicMock(spec=QPainter)
        render_ctx = _make_render_context()
        feature = _make_polygon_feature()

        sym.renderFeature(feature, painter, render_ctx)
        # The painter drawPath should NOT have been called
        painter.drawPath.assert_not_called()

    def test_symbol_clone(self):
        """clone is independent."""
        layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [layer])

        clone = sym.clone()
        assert clone is not sym
        assert clone.symbolLayerCount() == sym.symbolLayerCount()
        assert clone.type() == sym.type()

        # Mutating clone does not affect original
        clone.addSymbolLayer(QgsSimpleLineSymbolLayer())
        assert clone.symbolLayerCount() != sym.symbolLayerCount()


# ---------------------------------------------------------------------------
# Render integration — actual QPainter (no mock)
# ---------------------------------------------------------------------------

class TestRenderIntegration:

    def test_fill_renders_to_image(self):
        """Fill symbol layer actually draws to a QImage without crashing."""
        layer = QgsSimpleFillSymbolLayer()
        layer.setColor(QColor(255, 0, 0, 100))
        layer.setStrokeColor(QColor(255, 0, 0))

        img = QImage(500, 500, QImage.Format_ARGB32)
        img.fill(0)
        painter = QPainter(img)
        try:
            render_ctx = _make_render_context()
            feature = _make_polygon_feature()
            layer.renderFeature(feature, painter, render_ctx)
        finally:
            painter.end()

    def test_line_renders_to_image(self):
        """Line symbol layer actually draws to a QImage without crashing."""
        layer = QgsSimpleLineSymbolLayer()
        layer.setColor(QColor(0, 0, 255))

        img = QImage(500, 500, QImage.Format_ARGB32)
        img.fill(0)
        painter = QPainter(img)
        try:
            render_ctx = _make_render_context()
            feature = _make_line_feature()
            layer.renderFeature(feature, painter, render_ctx)
        finally:
            painter.end()

    def test_marker_renders_to_image(self):
        """Marker symbol layer actually draws to a QImage without crashing."""
        layer = QgsSimpleMarkerSymbolLayer()
        layer.setColor(QColor(0, 255, 0))

        img = QImage(500, 500, QImage.Format_ARGB32)
        img.fill(0)
        painter = QPainter(img)
        try:
            render_ctx = _make_render_context()
            feature = _make_point_feature()
            layer.renderFeature(feature, painter, render_ctx)
        finally:
            painter.end()

    def test_multi_geometry_render(self):
        """MultiPolygon / MultiLineString / MultiPoint render without crash."""
        from shapely.geometry import MultiPolygon, Polygon, MultiLineString, LineString, MultiPoint, Point

        fill_layer = QgsSimpleFillSymbolLayer()
        line_layer = QgsSimpleLineSymbolLayer()
        marker_layer = QgsSimpleMarkerSymbolLayer()

        multi_poly = {"shape": MultiPolygon([
            Polygon([(0, 0), (10, 0), (10, 10), (0, 0)]),
            Polygon([(20, 20), (30, 20), (30, 30), (20, 20)]),
        ])}
        multi_line = {"shape": MultiLineString([
            LineString([(0, 0), (10, 10)]),
            LineString([(20, 20), (30, 30)]),
        ])}
        multi_point = {"shape": MultiPoint([(5, 5), (15, 15)])}

        img = QImage(500, 500, QImage.Format_ARGB32)
        img.fill(0)
        painter = QPainter(img)
        render_ctx = _make_render_context()
        try:
            fill_layer.renderFeature(multi_poly, painter, render_ctx)
            line_layer.renderFeature(multi_line, painter, render_ctx)
            marker_layer.renderFeature(multi_point, painter, render_ctx)
        finally:
            painter.end()

    def test_polygon_with_holes(self):
        """Polygon with interior holes renders without crash."""
        from shapely.geometry import Polygon
        outer = [(0, 0), (100, 0), (100, 100), (0, 100)]
        hole = [(25, 25), (75, 25), (75, 75), (25, 75)]
        feature = {"shape": Polygon(outer, [hole])}

        layer = QgsSimpleFillSymbolLayer()
        img = QImage(500, 500, QImage.Format_ARGB32)
        img.fill(0)
        painter = QPainter(img)
        render_ctx = _make_render_context()
        try:
            layer.renderFeature(feature, painter, render_ctx)
        finally:
            painter.end()
