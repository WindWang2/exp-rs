"""QGIS-style scale bar settings."""

from PySide6.QtGui import QFont, QColor
from PySide6.QtCore import Qt


class ScaleBarStyle:
    """Scale bar rendering styles."""
    SingleBox = 0
    DoubleBox = 1
    Ticks = 2
    SteppedLines = 3
    Hollow = 4
    Numeric = 5


class QgsScaleBarSettings:
    """Stores configuration for a scale bar, similar to QGIS QgsScaleBarSettings."""

    def __init__(self):
        self._num_segments = 2
        self._units_per_segment = 1.0
        self._unit_label = ""
        self._height = 5.0  # mm
        self._font = QFont()
        self._text_color = QColor(Qt.GlobalColor.black)
        self._fill_color = QColor(Qt.GlobalColor.black)
        self._line_color = QColor(Qt.GlobalColor.black)
        self._style = ScaleBarStyle.SingleBox

    # --- numberOfSegments ---

    def numberOfSegments(self) -> int:
        return self._num_segments

    def setNumberOfSegments(self, count: int):
        self._num_segments = count

    # --- unitsPerSegment ---

    def unitsPerSegment(self) -> float:
        return self._units_per_segment

    def setUnitsPerSegment(self, units: float):
        self._units_per_segment = units

    # --- unitLabel ---

    def unitLabel(self) -> str:
        return self._unit_label

    def setUnitLabel(self, label: str):
        self._unit_label = label

    # --- height ---

    def height(self) -> float:
        return self._height

    def setHeight(self, height: float):
        self._height = height

    # --- font ---

    def font(self) -> QFont:
        return self._font

    def setFont(self, font: QFont):
        self._font = font

    # --- textColor ---

    def textColor(self) -> QColor:
        return self._text_color

    def setTextColor(self, color: QColor):
        self._text_color = color

    # --- fillColor ---

    def fillColor(self) -> QColor:
        return self._fill_color

    def setFillColor(self, color: QColor):
        self._fill_color = color

    # --- lineColor ---

    def lineColor(self) -> QColor:
        return self._line_color

    def setLineColor(self, color: QColor):
        self._line_color = color

    # --- style ---

    def style(self) -> int:
        return self._style

    def setStyle(self, style: int):
        self._style = style
