"""QgsPalLayerSettings -- configuration for PAL-based text labeling.

Stores label display parameters: which attribute field to label,
font, color, placement mode, priority, and scale-range restrictions.
"""

from __future__ import annotations

from enum import IntEnum

from PySide6.QtGui import QColor, QFont


class Placement(IntEnum):
    """Label placement relative to feature geometry."""
    Point = 0
    Line = 1
    Polygon = 2  # centroid placement (default)


class QgsPalLayerSettings:
    """Configuration object for PAL-based layer labeling.

    Mirrors the QGIS ``QgsPalLayerSettings`` class at a minimal level:
    public attributes with defaults, plus getter helpers for QGIS API
    compatibility.
    """

    __slots__ = (
        '_enabled',
        '_fieldName',
        '_font',
        '_color',
        '_placement',
        '_priority',
        '_scaleMin',
        '_scaleMax',
    )

    def __init__(self) -> None:
        self._enabled: bool = False
        self._fieldName: str = ""
        self._font: QFont = QFont()
        self._color: QColor = QColor(0, 0, 0)
        self._placement: Placement = Placement.Polygon
        self._priority: float = 5.0
        self._scaleMin: float = 0.0
        self._scaleMax: float = 0.0

    # ------------------------------------------------------------------
    # enabled
    # ------------------------------------------------------------------

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool) -> None:
        self._enabled = bool(value)

    # ------------------------------------------------------------------
    # fieldName
    # ------------------------------------------------------------------

    @property
    def fieldName(self) -> str:
        return self._fieldName

    @fieldName.setter
    def fieldName(self, value: str) -> None:
        self._fieldName = str(value)

    # ------------------------------------------------------------------
    # font
    # ------------------------------------------------------------------

    @property
    def font(self) -> QFont:
        return self._font

    @font.setter
    def font(self, value: QFont) -> None:
        self._font = QFont(value)

    # ------------------------------------------------------------------
    # color
    # ------------------------------------------------------------------

    @property
    def color(self) -> QColor:
        return self._color

    @color.setter
    def color(self, value: QColor) -> None:
        self._color = QColor(value)

    # ------------------------------------------------------------------
    # placement
    # ------------------------------------------------------------------

    @property
    def placement(self) -> Placement:
        return self._placement

    @placement.setter
    def placement(self, value: Placement) -> None:
        self._placement = Placement(value)

    # ------------------------------------------------------------------
    # priority (0 = highest, 10 = lowest)
    # ------------------------------------------------------------------

    @property
    def priority(self) -> float:
        return self._priority

    @priority.setter
    def priority(self, value: float) -> None:
        self._priority = max(0.0, min(10.0, float(value)))

    # ------------------------------------------------------------------
    # scaleMin / scaleMax  (0 means no limit)
    # ------------------------------------------------------------------

    @property
    def scaleMin(self) -> float:
        return self._scaleMin

    @scaleMin.setter
    def scaleMin(self, value: float) -> None:
        self._scaleMin = float(value)

    @property
    def scaleMax(self) -> float:
        return self._scaleMax

    @scaleMax.setter
    def scaleMax(self, value: float) -> None:
        self._scaleMax = float(value)

    # ------------------------------------------------------------------
    # repr
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        return (
            f"QgsPalLayerSettings(enabled={self._enabled}, "
            f"field={self._fieldName!r}, priority={self._priority})"
        )
