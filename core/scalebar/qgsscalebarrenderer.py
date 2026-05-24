"""QGIS-style scale bar renderer."""

from PySide6.QtGui import QPainter, QColor, QPen, QBrush, QFont, QFontMetrics
from PySide6.QtCore import Qt, QPointF, QRectF

from core.scalebar.qgsscalebarsettings import QgsScaleBarSettings, ScaleBarStyle


# Pixels per mm at 96 DPI
PX_PER_MM = 96.0 / 25.4


class QgsScaleBarRenderer:
    """Renders a scale bar using QgsScaleBarSettings."""

    def __init__(self, settings: QgsScaleBarSettings):
        self._settings = settings

    def settings(self) -> QgsScaleBarSettings:
        return self._settings

    def calculateWidth(self, scale_denominator: float) -> float:
        """Calculate total pixel width of the scale bar.

        Width = (num_segments * units_per_segment (m) * 1000 (mm/m))
                / scale_denominator * PX_PER_MM
        """
        total_meters = self._settings.numberOfSegments() * self._settings.unitsPerSegment()
        total_mm = total_meters * 1000.0
        return total_mm / scale_denominator * PX_PER_MM

    def render(self, painter: QPainter, scale_denominator: float, position: QPointF):
        """Render the scale bar at the given position.

        The position is the bottom-left corner of the bar.
        """
        s = self._settings
        num_segments = s.numberOfSegments()
        units_per_segment = s.unitsPerSegment()

        if num_segments <= 0 or scale_denominator <= 0:
            return

        # Bar dimensions
        total_width = self.calculateWidth(scale_denominator)
        seg_width = total_width / num_segments
        bar_height_px = s.height() * PX_PER_MM

        # Font setup
        painter.setFont(s.font())
        fm = QFontMetrics(s.font())
        text_height = fm.height()

        # Bar top-left (position is bottom-left, so bar sits above position.y())
        bar_top = position.y() - bar_height_px - text_height - 4
        bar_left = position.x()

        # --- Draw segment bars ---
        fill = s.fillColor()
        fill_alt = QColor(Qt.GlobalColor.white)
        line_pen = QPen(s.lineColor())
        line_pen.setWidthF(1.0)
        painter.setPen(line_pen)

        for i in range(num_segments):
            x = bar_left + i * seg_width
            rect = QRectF(x, bar_top, seg_width, bar_height_px)

            if s.style() == ScaleBarStyle.SingleBox:
                color = fill if i % 2 == 0 else fill_alt
                painter.setBrush(QBrush(color))
                painter.drawRect(rect)
            elif s.style() == ScaleBarStyle.DoubleBox:
                color = fill if i % 2 == 0 else fill_alt
                painter.setBrush(QBrush(color))
                painter.drawRect(rect)
            elif s.style() == ScaleBarStyle.Hollow:
                painter.setBrush(QBrush(fill_alt))
                painter.drawRect(rect)
                painter.setBrush(Qt.BrushStyle.NoBrush)
            elif s.style() == ScaleBarStyle.Ticks:
                painter.setBrush(Qt.BrushStyle.NoBrush)
                painter.drawLine(int(x), int(bar_top), int(x), int(bar_top + bar_height_px))
                painter.drawLine(int(x), int(bar_top), int(x + seg_width), int(bar_top))
            else:
                color = fill if i % 2 == 0 else fill_alt
                painter.setBrush(QBrush(color))
                painter.drawRect(rect)

        # Draw the final right edge for Ticks/Hollow
        if s.style() in (ScaleBarStyle.Ticks, ScaleBarStyle.Hollow):
            painter.drawLine(int(bar_left + total_width), int(bar_top),
                             int(bar_left + total_width), int(bar_top + bar_height_px))
            painter.setPen(Qt.PenStyle.NoPen)

        # --- Draw segment labels ---
        painter.setPen(QPen(s.textColor()))
        painter.setFont(s.font())
        label_y = bar_top + bar_height_px + fm.ascent() + 4

        for i in range(num_segments + 1):
            x = bar_left + i * seg_width
            value = int(units_per_segment * i)
            label = str(value)
            label_width = fm.horizontalAdvance(label)
            painter.drawText(QPointF(x - label_width / 2.0, label_y), label)

        # --- Draw unit label ---
        if s.unitLabel():
            unit_x = bar_left + total_width + 6
            painter.drawText(QPointF(unit_x, label_y), s.unitLabel())
