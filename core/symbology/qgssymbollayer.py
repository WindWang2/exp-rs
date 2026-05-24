"""QgsSymbolLayer hierarchy — abstract base and three concrete symbol layers.

Mirrors the QGIS QgsSymbolLayer / QgsSimpleFillSymbolLayer /
QgsSimpleLineSymbolLayer / QgsSimpleMarkerSymbolLayer classes.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QBrush, QColor, QPen, QPainterPath


# ---------------------------------------------------------------------------
# Abstract base
# ---------------------------------------------------------------------------

class QgsSymbolLayer(ABC):
    """Abstract base class for all symbol layers."""

    class SymbolType(int):
        Fill = 0
        Line = 1
        Marker = 2

    def __init__(self):
        self._enabled: bool = True
        self._color: QColor = QColor(255, 0, 0, 255)

    # -- abstract -----------------------------------------------------------

    @abstractmethod
    def type(self) -> int:
        """Return the SymbolType this layer handles."""
        ...

    @abstractmethod
    def renderFeature(self, feature: dict, painter, renderContext) -> None:
        """Render a single feature using this symbol layer."""
        ...

    @abstractmethod
    def clone(self) -> 'QgsSymbolLayer':
        """Return an independent deep copy."""
        ...

    # -- concrete -----------------------------------------------------------

    def color(self) -> QColor:
        return QColor(self._color)

    def setColor(self, color: QColor) -> None:
        self._color = QColor(color)

    def isEnabled(self) -> bool:
        return self._enabled

    def setEnabled(self, enabled: bool) -> None:
        self._enabled = bool(enabled)


# ---------------------------------------------------------------------------
# Helpers (shared path-building logic)
# ---------------------------------------------------------------------------

def _to_qpainter_path(shape, transform) -> QPainterPath | None:
    """Convert a shapely geometry to QPainterPath using *transform* (QTransform).

    Handles Point, LineString, Polygon (with holes), and Multi* types.
    """
    path = QPainterPath()

    if shape.geom_type == 'Point':
        p = transform.map(QPointF(shape.x, shape.y))
        path.addEllipse(p.x() - 3, p.y() - 3, 6, 6)

    elif shape.geom_type == 'LineString':
        coords = list(shape.coords)
        if not coords:
            return None
        p0 = transform.map(QPointF(coords[0][0], coords[0][1]))
        path.moveTo(p0)
        for i in range(1, len(coords)):
            pi = transform.map(QPointF(coords[i][0], coords[i][1]))
            path.lineTo(pi)

    elif shape.geom_type == 'Polygon':
        exterior = list(shape.exterior.coords)
        if not exterior:
            return None
        p0 = transform.map(QPointF(exterior[0][0], exterior[0][1]))
        path.moveTo(p0)
        for i in range(1, len(exterior)):
            pi = transform.map(QPointF(exterior[i][0], exterior[i][1]))
            path.lineTo(pi)
        path.closeSubpath()

        for interior in shape.interiors:
            coords = list(interior.coords)
            if not coords:
                continue
            p0 = transform.map(QPointF(coords[0][0], coords[0][1]))
            path.moveTo(p0)
            for i in range(1, len(coords)):
                pi = transform.map(QPointF(coords[i][0], coords[i][1]))
                path.lineTo(pi)
            path.closeSubpath()

    elif shape.geom_type.startswith('Multi'):
        for part in shape.geoms:
            part_path = _to_qpainter_path(part, transform)
            if part_path:
                path.addPath(part_path)

    return path


def _get_shape(feature: dict):
    """Extract shapely geometry from a feature dict or QgsFeature."""
    if isinstance(feature, dict):
        return feature.get("shape")
    # QgsFeature — try geometry().asShapely()
    geom = feature.geometry()
    if geom is not None and hasattr(geom, 'asShapely'):
        return geom.asShapely()
    return None


# ---------------------------------------------------------------------------
# QgsSimpleFillSymbolLayer
# ---------------------------------------------------------------------------

class QgsSimpleFillSymbolLayer(QgsSymbolLayer):
    """Fill symbol layer — renders polygons with fill + stroke."""

    def __init__(
        self,
        fill_color: QColor = QColor(255, 0, 0, 100),
        stroke_color: QColor = QColor(255, 0, 0),
        stroke_width: int = 1,
        brush_style: Qt.BrushStyle = Qt.SolidPattern,
    ):
        super().__init__()
        self._color = QColor(fill_color)
        self._stroke_color = QColor(stroke_color)
        self._stroke_width = stroke_width
        self._brush_style = brush_style

    def type(self) -> int:
        return QgsSymbolLayer.SymbolType.Fill

    # -- properties ---------------------------------------------------------

    def strokeColor(self) -> QColor:
        return QColor(self._stroke_color)

    def setStrokeColor(self, color: QColor) -> None:
        self._stroke_color = QColor(color)

    def strokeWidth(self) -> int:
        return self._stroke_width

    def setStrokeWidth(self, width: int) -> None:
        self._stroke_width = int(width)

    def brushStyle(self) -> Qt.BrushStyle:
        return self._brush_style

    def setBrushStyle(self, style: Qt.BrushStyle) -> None:
        self._brush_style = style

    # -- render -------------------------------------------------------------

    def renderFeature(self, feature: dict, painter, renderContext) -> None:
        shape = _get_shape(feature)
        if shape is None or shape.is_empty:
            return

        transform = renderContext.mapToPixel()._transform
        path = _to_qpainter_path(shape, transform)
        if path is None:
            return

        pen = QPen(self._stroke_color)
        pen.setWidth(self._stroke_width)
        brush = QBrush(self._color, self._brush_style)

        painter.setPen(pen)
        painter.setBrush(brush)
        painter.drawPath(path)

    # -- clone --------------------------------------------------------------

    def clone(self) -> 'QgsSimpleFillSymbolLayer':
        copy = QgsSimpleFillSymbolLayer(
            fill_color=QColor(self._color),
            stroke_color=QColor(self._stroke_color),
            stroke_width=self._stroke_width,
            brush_style=self._brush_style,
        )
        copy._enabled = self._enabled
        return copy


# ---------------------------------------------------------------------------
# QgsSimpleLineSymbolLayer
# ---------------------------------------------------------------------------

class QgsSimpleLineSymbolLayer(QgsSymbolLayer):
    """Line symbol layer — renders line geometries with a pen."""

    def __init__(
        self,
        stroke_color: QColor = QColor(255, 0, 0),
        stroke_width: int = 1,
        pen_style: Qt.PenStyle = Qt.SolidLine,
    ):
        super().__init__()
        self._color = QColor(stroke_color)
        self._stroke_width = stroke_width
        self._pen_style = pen_style

    def type(self) -> int:
        return QgsSymbolLayer.SymbolType.Line

    # -- properties ---------------------------------------------------------

    def strokeWidth(self) -> int:
        return self._stroke_width

    def setStrokeWidth(self, width: int) -> None:
        self._stroke_width = int(width)

    def penStyle(self) -> Qt.PenStyle:
        return self._pen_style

    def setPenStyle(self, style: Qt.PenStyle) -> None:
        self._pen_style = style

    # -- render -------------------------------------------------------------

    def renderFeature(self, feature: dict, painter, renderContext) -> None:
        shape = _get_shape(feature)
        if shape is None or shape.is_empty:
            return

        transform = renderContext.mapToPixel()._transform
        path = _to_qpainter_path(shape, transform)
        if path is None:
            return

        pen = QPen(self._color)
        pen.setWidth(self._stroke_width)
        pen.setStyle(self._pen_style)

        painter.setPen(pen)
        painter.setBrush(Qt.NoBrush)
        painter.drawPath(path)

    # -- clone --------------------------------------------------------------

    def clone(self) -> 'QgsSimpleLineSymbolLayer':
        copy = QgsSimpleLineSymbolLayer(
            stroke_color=QColor(self._color),
            stroke_width=self._stroke_width,
            pen_style=self._pen_style,
        )
        copy._enabled = self._enabled
        return copy


# ---------------------------------------------------------------------------
# QgsSimpleMarkerSymbolLayer
# ---------------------------------------------------------------------------

class QgsSimpleMarkerSymbolLayer(QgsSymbolLayer):
    """Marker symbol layer — renders point geometries as simple shapes."""

    VALID_SHAPES = ('circle', 'square', 'triangle')

    def __init__(
        self,
        color: QColor = QColor(255, 0, 0),
        size: float = 6.0,
        shape: str = 'circle',
    ):
        super().__init__()
        self._color = QColor(color)
        self._size = float(size)
        self._shape = shape if shape in self.VALID_SHAPES else 'circle'

    def type(self) -> int:
        return QgsSymbolLayer.SymbolType.Marker

    # -- properties ---------------------------------------------------------

    def size(self) -> float:
        return self._size

    def setSize(self, size: float) -> None:
        self._size = float(size)

    def shape(self) -> str:
        return self._shape

    def setShape(self, shape: str) -> None:
        if shape in self.VALID_SHAPES:
            self._shape = shape

    # -- render -------------------------------------------------------------

    def renderFeature(self, feature: dict, painter, renderContext) -> None:
        shape = _get_shape(feature)
        if shape is None or shape.is_empty:
            return

        transform = renderContext.mapToPixel()._transform

        # Collect point coordinates (handle MultiPoint)
        if shape.geom_type == 'Point':
            points = [shape]
        elif shape.geom_type.startswith('Multi'):
            points = list(shape.geoms)
        else:
            # For non-point geometries, draw nothing
            return

        brush = QBrush(self._color)
        pen = QPen(self._color)
        painter.setPen(pen)
        painter.setBrush(brush)

        half = self._size / 2.0
        for pt in points:
            p = transform.map(QPointF(pt.x, pt.y))
            px, py = p.x(), p.y()

            if self._shape == 'circle':
                painter.drawEllipse(QPointF(px, py), half, half)
            elif self._shape == 'square':
                painter.drawRect(px - half, py - half, self._size, self._size)
            elif self._shape == 'triangle':
                path = QPainterPath()
                path.moveTo(px, py - half)
                path.lineTo(px - half, py + half)
                path.lineTo(px + half, py + half)
                path.closeSubpath()
                painter.drawPath(path)

    # -- clone --------------------------------------------------------------

    def clone(self) -> 'QgsSimpleMarkerSymbolLayer':
        copy = QgsSimpleMarkerSymbolLayer(
            color=QColor(self._color),
            size=self._size,
            shape=self._shape,
        )
        copy._enabled = self._enabled
        return copy
