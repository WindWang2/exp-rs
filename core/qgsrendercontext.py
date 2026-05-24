"""QgsRenderContext -- per-render state bundle for a single rendering pass.

Holds everything a renderer needs during one render cycle: coordinate
transforms, visible extent, painter, rendering flags, and clip geometry.
"""

from __future__ import annotations

from enum import IntEnum

from PySide6.QtCore import QSize

from core.qgsrectangle import QgsRectangle
from core.qgsmaptopixel import QgsMapToPixel


# ------------------------------------------------------------------
# Rendering flags
# ------------------------------------------------------------------

class RenderFlag(IntEnum):
    """Bit-flag enumeration for render context behaviour."""
    DrawEditingInfo = 1
    ForceVectorOutput = 2
    UseRenderingOptimization = 4
    DrawSelection = 8
    DrawSymbolBounds = 16


# ------------------------------------------------------------------
# QgsRenderContext
# ------------------------------------------------------------------

class QgsRenderContext:
    """Bundles per-render state for a single rendering pass.

    Mirrors the QGIS C++ ``QgsRenderContext`` class at a minimal level:
    coordinate transforms, visible extent, painter, rendering flags,
    clip geometry, output size, and renderer scale.
    """

    __slots__ = (
        '_map_to_pixel',
        '_extent',
        '_renderer_scale',
        '_coordinate_transform',
        '_painter',
        '_flags',
        '_feature_clip_geometry',
        '_output_size',
    )

    def __init__(self) -> None:
        self._map_to_pixel: QgsMapToPixel = QgsMapToPixel.fromSettings(
            QgsRectangle(), QSize(0, 0)
        )
        self._extent: QgsRectangle = QgsRectangle()
        self._renderer_scale: float = 0.0
        self._coordinate_transform = None  # QgsCoordinateTransform | None
        self._painter = None               # QPainter | None
        self._flags: int = 0
        self._feature_clip_geometry = None  # QgsGeometry | None
        self._output_size: QSize = QSize(0, 0)

    # ------------------------------------------------------------------
    # Factory
    # ------------------------------------------------------------------

    @staticmethod
    def fromMapSettings(settings) -> 'QgsRenderContext':
        """Create a QgsRenderContext initialised from a QgsMapSettings.

        Parameters
        ----------
        settings : QgsMapSettings
            The map settings whose extent, output size and CRS are used
            to populate the render context.

        Returns
        -------
        QgsRenderContext
        """
        ctx = QgsRenderContext()
        if settings.extent is not None:
            ctx._extent = settings.extent
        if settings.output_size is not None:
            ctx._output_size = settings.output_size
        ctx._map_to_pixel = QgsMapToPixel.fromSettings(
            ctx._extent, ctx._output_size
        )
        return ctx

    # ------------------------------------------------------------------
    # MapToPixel
    # ------------------------------------------------------------------

    def mapToPixel(self) -> QgsMapToPixel:
        """Read-only access to the map-to-pixel coordinate transform."""
        return self._map_to_pixel

    def setMapToPixel(self, mtp: QgsMapToPixel) -> None:
        """Set the map-to-pixel coordinate transform."""
        self._map_to_pixel = mtp

    # ------------------------------------------------------------------
    # Extent
    # ------------------------------------------------------------------

    def extent(self) -> QgsRectangle:
        """The visible map extent in world coordinates."""
        return self._extent

    def setExtent(self, extent: QgsRectangle) -> None:
        """Set the visible map extent."""
        self._extent = extent

    # ------------------------------------------------------------------
    # Renderer scale
    # ------------------------------------------------------------------

    def rendererScale(self) -> float:
        """Map scale denominator (e.g. 25000 means 1:25 000)."""
        return self._renderer_scale

    def setRendererScale(self, scale: float) -> None:
        """Set the map scale denominator."""
        self._renderer_scale = scale

    # ------------------------------------------------------------------
    # Coordinate transform
    # ------------------------------------------------------------------

    def coordinateTransform(self):
        """The active coordinate transform, or None if not set."""
        return self._coordinate_transform

    def setCoordinateTransform(self, ct) -> None:
        """Set (or clear with None) the active coordinate transform."""
        self._coordinate_transform = ct

    # ------------------------------------------------------------------
    # Painter
    # ------------------------------------------------------------------

    def painter(self):
        """The QPainter being rendered to, or None."""
        return self._painter

    def setPainter(self, painter) -> None:
        """Set (or clear with None) the target QPainter."""
        self._painter = painter

    # ------------------------------------------------------------------
    # Flags
    # ------------------------------------------------------------------

    def setFlag(self, flag: RenderFlag, on: bool = True) -> None:
        """Enable or disable a rendering flag.

        Parameters
        ----------
        flag : RenderFlag
            The flag to modify.
        on : bool
            True to set the flag, False to clear it.
        """
        if on:
            self._flags |= int(flag)
        else:
            self._flags &= ~int(flag)

    def testFlag(self, flag: RenderFlag) -> bool:
        """Return True if *flag* is set."""
        return (self._flags & int(flag)) != 0

    # ------------------------------------------------------------------
    # Feature clip geometry
    # ------------------------------------------------------------------

    def featureClipGeometry(self):
        """The feature clip geometry, or None if not set."""
        return self._feature_clip_geometry

    def setFeatureClipGeometry(self, geom) -> None:
        """Set (or clear with None) the feature clip geometry."""
        self._feature_clip_geometry = geom

    # ------------------------------------------------------------------
    # Output size
    # ------------------------------------------------------------------

    def outputSize(self) -> QSize:
        """The output device size in pixels."""
        return self._output_size

    def setOutputSize(self, size: QSize) -> None:
        """Set the output device size."""
        self._output_size = size

    # ------------------------------------------------------------------
    # Dunder
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        w = self._output_size.width()
        h = self._output_size.height()
        return (
            f"QgsRenderContext(extent={self._extent!r}, "
            f"output={w}x{h}, scale={self._renderer_scale})"
        )
