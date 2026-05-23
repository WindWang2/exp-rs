from engine.core.display.renderers.base import FeatureRenderer
from PySide6.QtGui import QPen, QBrush, QColor, QPainterPath
from PySide6.QtCore import Qt, QPointF

class SingleSymbolRenderer(FeatureRenderer):
    """
    Renders all features using the same symbol (pen and brush).
    """
    def __init__(self, color=QColor(255, 0, 0, 100), stroke_color=QColor(255, 0, 0), stroke_width=1):
        self.pen = QPen(stroke_color)
        self.pen.setWidth(stroke_width)
        self.brush = QBrush(color)

    def render_feature(self, feature, painter, settings):
        """
        Renders a feature geometry.
        """
        shape = feature.get("shape")
        if not shape or shape.is_empty:
            return

        painter.setPen(self.pen)
        painter.setBrush(self.brush)

        path = self._to_qpainter_path(shape, settings)
        if path:
            painter.drawPath(path)

    def _to_qpainter_path(self, shape, settings):
        """
        Converts a shapely geometry to a QPainterPath using world-to-device transforms.
        """
        path = QPainterPath()
        transform = settings.worldToDevice()

        if shape.geom_type == 'Point':
            # Draw a small circle for points
            p = transform.map(QPointF(shape.x, shape.y))
            path.addEllipse(p.x() - 3, p.y() - 3, 6, 6)

        elif shape.geom_type == 'LineString':
            coords = list(shape.coords)
            if not coords: return None
            p0 = transform.map(QPointF(coords[0][0], coords[0][1]))
            path.moveTo(p0)
            for i in range(1, len(coords)):
                pi = transform.map(QPointF(coords[i][0], coords[i][1]))
                path.lineTo(pi)

        elif shape.geom_type == 'Polygon':
            # Outer ring
            exterior = list(shape.exterior.coords)
            if not exterior: return None
            p0 = transform.map(QPointF(exterior[0][0], exterior[0][1]))
            path.moveTo(p0)
            for i in range(1, len(exterior)):
                pi = transform.map(QPointF(exterior[i][0], exterior[i][1]))
                path.lineTo(pi)
            path.closeSubpath()

            # Inner rings (holes)
            for interior in shape.interiors:
                coords = list(interior.coords)
                if not coords: continue
                p0 = transform.map(QPointF(coords[0][0], coords[0][1]))
                path.moveTo(p0)
                for i in range(1, len(coords)):
                    pi = transform.map(QPointF(coords[i][0], coords[i][1]))
                    path.lineTo(pi)
                path.closeSubpath()

        elif shape.geom_type.startswith('Multi'):
            for part in shape.geoms:
                part_path = self._to_qpainter_path(part, settings)
                if part_path:
                    path.addPath(part_path)

        return path
