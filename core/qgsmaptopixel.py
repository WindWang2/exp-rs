"""QgsMapToPixel — standalone coordinate transform utility.

Converts between map (world/GIS) coordinates and device (pixel/Qt) coordinates.
Wraps QTransform with the same math as QgsMapSettings.worldToDevice() but
packaged as a reusable, standalone class.
"""

from __future__ import annotations

from PySide6.QtCore import QPointF, QSize
from PySide6.QtGui import QTransform

from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle


class QgsMapToPixel:
    """Converts map (world) coordinates to device (pixel) coordinates and back.

    GIS convention: Y increases upward.
    Qt convention:  Y increases downward.
    This class handles the Y-axis flip transparently.
    """

    __slots__ = ('_transform', '_inv_transform', '_extent', '_output_size', '_valid')

    def __init__(self, transform: QTransform, extent: QgsRectangle, output_size: QSize):
        self._transform = transform
        inv, ok = transform.inverted()
        self._inv_transform = inv if ok else QTransform()
        self._extent = extent
        self._output_size = output_size
        self._valid = ok

    # ------------------------------------------------------------------
    # Factory
    # ------------------------------------------------------------------

    @staticmethod
    def fromSettings(extent: QgsRectangle, output_size: QSize) -> 'QgsMapToPixel':
        """Build a QgsMapToPixel from an extent and output device size.

        Parameters
        ----------
        extent : QgsRectangle
            The map extent in world coordinates (GIS Y-up).
        output_size : QSize
            The target device dimensions in pixels.

        Returns
        -------
        QgsMapToPixel
        """
        if extent.isEmpty() or not output_size.isValid():
            return QgsMapToPixel(QTransform(), extent, output_size)

        world_w = extent.width()
        world_h = extent.height()
        dev_w = output_size.width()
        dev_h = output_size.height()

        s_x = dev_w / world_w
        s_y = dev_h / world_h

        t = QTransform()
        t.scale(s_x, -s_y)
        t.translate(-extent.xMinimum(), -extent.yMaximum())
        return QgsMapToPixel(t, extent, output_size)

    # ------------------------------------------------------------------
    # Forward transform  (world → device)
    # ------------------------------------------------------------------

    def transform(self, *args):
        """Transform map coordinates to device (pixel) coordinates.

        Overloads
        ---------
        transform(x: float, y: float) -> tuple[float, float]
        transform(point: QgsPointXY)  -> QPointF
        """
        if len(args) == 2 and isinstance(args[0], (int, float)):
            x, y = float(args[0]), float(args[1])
            pt = self._transform.map(QPointF(x, y))
            return (pt.x(), pt.y())
        elif len(args) == 1 and isinstance(args[0], QgsPointXY):
            pt = args[0]
            return self._transform.map(QPointF(pt.x(), pt.y()))
        else:
            raise TypeError(f"Unsupported arguments: {args}")

    # ------------------------------------------------------------------
    # Inverse transform  (device → world)
    # ------------------------------------------------------------------

    def toMapCoordinates(self, *args):
        """Transform device (pixel) coordinates back to map coordinates.

        Overloads
        ---------
        toMapCoordinates(px: float, py: float) -> tuple[float, float]
        toMapCoordinates(point: QPointF)       -> QgsPointXY
        """
        if len(args) == 2 and isinstance(args[0], (int, float)):
            px, py = float(args[0]), float(args[1])
            pt = self._inv_transform.map(QPointF(px, py))
            return (pt.x(), pt.y())
        elif len(args) == 1 and isinstance(args[0], QPointF):
            pt = self._inv_transform.map(args[0])
            return QgsPointXY(pt.x(), pt.y())
        else:
            raise TypeError(f"Unsupported arguments: {args}")

    # ------------------------------------------------------------------
    # Accessors
    # ------------------------------------------------------------------

    def scale(self) -> float:
        """Map units per pixel (world_width / device_width)."""
        if self._output_size is None or self._output_size.width() == 0:
            return 0.0
        return self._extent.width() / self._output_size.width()

    def mapWidth(self) -> int:
        """Output device width in pixels."""
        return self._output_size.width()

    def mapHeight(self) -> int:
        """Output device height in pixels."""
        return self._output_size.height()

    def mapExtent(self) -> QgsRectangle:
        """The map extent in world coordinates."""
        return self._extent

    # ------------------------------------------------------------------
    # Copy
    # ------------------------------------------------------------------

    def clone(self) -> 'QgsMapToPixel':
        """Return an independent copy of this instance."""
        return QgsMapToPixel(QTransform(self._transform), self._extent, self._output_size)

    # ------------------------------------------------------------------
    # Dunder
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        return (
            f"QgsMapToPixel(extent={self._extent!r}, "
            f"output={self._output_size.width()}×{self._output_size.height()}, "
            f"scale={self.scale():.6f})"
        )
