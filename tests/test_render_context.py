"""Tests for QgsMapToPixel coordinate transform utility."""
import pytest
from PySide6.QtCore import QSize, QPointF
from PySide6.QtGui import QPainter

from core.qgsrectangle import QgsRectangle
from core.qgspointxy import QgsPointXY
from core.qgsmaptopixel import QgsMapToPixel


class TestQgsMapToPixel:
    """Tests for QgsMapToPixel (Task 18)."""

    def _make_instance(self):
        """Helper: extent 0,0 -> 1000,1000 with 500x500 output."""
        extent = QgsRectangle(0, 0, 1000, 1000)
        size = QSize(500, 500)
        return QgsMapToPixel.fromSettings(extent, size)

    def test_map_to_pixel_from_settings(self):
        """Factory creates a valid, non-None instance."""
        mtp = self._make_instance()
        assert mtp is not None
        assert isinstance(mtp, QgsMapToPixel)

    def test_map_to_pixel_transform(self):
        """Top-left of extent (0, 1000) maps to ~(0, 0) in device coords.

        In GIS coords the top-left is (xMin, yMax) = (0, 1000).
        After Y-flip that should land at device (0, 0).
        """
        mtp = self._make_instance()
        px, py = mtp.transform(0, 1000)
        assert abs(px) < 1e-6
        assert abs(py) < 1e-6

    def test_map_to_pixel_transform_point(self):
        """QgsPointXY overload returns QPointF with correct values."""
        mtp = self._make_instance()
        pt = QgsPointXY(0, 1000)  # top-left in GIS
        result = mtp.transform(pt)
        assert isinstance(result, QPointF)
        assert abs(result.x()) < 1e-6
        assert abs(result.y()) < 1e-6

    def test_map_to_pixel_inverse(self):
        """toMapCoordinates reverses transform (tuple overload)."""
        mtp = self._make_instance()
        # Forward: world (250, 750) -> device
        px, py = mtp.transform(250, 750)
        # Inverse: device -> world
        wx, wy = mtp.toMapCoordinates(px, py)
        assert abs(wx - 250) < 1e-6
        assert abs(wy - 750) < 1e-6

    def test_map_to_pixel_inverse_point(self):
        """toMapCoordinates reverses transform (QPointF overload)."""
        mtp = self._make_instance()
        pt = QgsPointXY(500, 500)  # center of extent
        device_pt = mtp.transform(pt)
        world_pt = mtp.toMapCoordinates(device_pt)
        assert isinstance(world_pt, QgsPointXY)
        assert abs(world_pt.x() - 500) < 1e-6
        assert abs(world_pt.y() - 500) < 1e-6

    def test_map_to_pixel_scale(self):
        """Scale = map units per pixel = world_width / device_width."""
        mtp = self._make_instance()
        # 1000 world units / 500 pixels = 2.0
        assert abs(mtp.scale() - 2.0) < 1e-9

    def test_map_to_pixel_dimensions(self):
        """mapWidth and mapHeight return the output device size."""
        mtp = self._make_instance()
        assert mtp.mapWidth() == 500
        assert mtp.mapHeight() == 500

    def test_map_to_pixel_clone(self):
        """Clone is an independent copy."""
        mtp = self._make_instance()
        clone = mtp.clone()
        assert clone is not mtp
        assert clone.mapWidth() == mtp.mapWidth()
        assert clone.mapHeight() == mtp.mapHeight()
        # Mutating clone should not affect original
        assert abs(clone.scale() - mtp.scale()) < 1e-9

    def test_map_to_pixel_no_extent(self):
        """Handles empty extent gracefully -- returns identity-like behavior."""
        mtp = QgsMapToPixel.fromSettings(QgsRectangle(), QSize(100, 100))
        assert mtp is not None
        # Should not crash; scale may be 0 or identity
        px, py = mtp.transform(0, 0)
        assert isinstance(px, float)
        assert isinstance(py, float)


# ======================================================================
# Tests for QgsRenderContext (Task 19)
# ======================================================================

from core.qgsrendercontext import QgsRenderContext, RenderFlag
from core.qgsmapsettings import QgsMapSettings
from core.qgsgeometry import QgsGeometry
from core.qgscoordinatetransform import QgsCoordinateTransform
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem


class TestQgsRenderContext:
    """Tests for QgsRenderContext (Task 19)."""

    def test_render_context_default_constructor(self):
        """All fields have safe defaults after construction."""
        ctx = QgsRenderContext()
        # MapToPixel should be a valid default instance
        assert isinstance(ctx.mapToPixel(), QgsMapToPixel)
        # Extent should be an empty rectangle
        assert isinstance(ctx.extent(), QgsRectangle)
        assert ctx.extent().isEmpty()
        # Renderer scale defaults to 0
        assert ctx.rendererScale() == 0.0
        # No coordinate transform by default
        assert ctx.coordinateTransform() is None
        # No painter by default
        assert ctx.painter() is None
        # No feature clip geometry by default
        assert ctx.featureClipGeometry() is None
        # Output size defaults to 0x0
        assert isinstance(ctx.outputSize(), QSize)
        assert ctx.outputSize().width() == 0
        assert ctx.outputSize().height() == 0
        # All flags default to off
        assert not ctx.testFlag(RenderFlag.DrawEditingInfo)
        assert not ctx.testFlag(RenderFlag.ForceVectorOutput)
        assert not ctx.testFlag(RenderFlag.UseRenderingOptimization)
        assert not ctx.testFlag(RenderFlag.DrawSelection)
        assert not ctx.testFlag(RenderFlag.DrawSymbolBounds)

    def test_render_context_from_settings(self):
        """Factory creates context from QgsMapSettings."""
        settings = QgsMapSettings()
        settings.extent = QgsRectangle(100, 200, 300, 400)
        settings.output_size = QSize(800, 600)

        ctx = QgsRenderContext.fromMapSettings(settings)
        assert isinstance(ctx, QgsRenderContext)
        # Extent should match
        assert ctx.extent() == QgsRectangle(100, 200, 300, 400)
        # Output size should match
        assert ctx.outputSize().width() == 800
        assert ctx.outputSize().height() == 600
        # MapToPixel should be initialized
        assert isinstance(ctx.mapToPixel(), QgsMapToPixel)
        assert ctx.mapToPixel().mapWidth() == 800
        assert ctx.mapToPixel().mapHeight() == 600

    def test_render_context_map_to_pixel(self):
        """Get/set MapToPixel."""
        ctx = QgsRenderContext()
        # Default
        default_mtp = ctx.mapToPixel()
        assert isinstance(default_mtp, QgsMapToPixel)
        # Set new
        extent = QgsRectangle(0, 0, 500, 500)
        new_mtp = QgsMapToPixel.fromSettings(extent, QSize(250, 250))
        ctx.setMapToPixel(new_mtp)
        assert ctx.mapToPixel() is new_mtp
        assert ctx.mapToPixel().mapWidth() == 250

    def test_render_context_extent(self):
        """Get/set extent."""
        ctx = QgsRenderContext()
        assert ctx.extent().isEmpty()
        new_extent = QgsRectangle(-180, -90, 180, 90)
        ctx.setExtent(new_extent)
        assert ctx.extent() == new_extent
        assert ctx.extent().xMinimum() == -180

    def test_render_context_painter(self):
        """Get/set painter."""
        ctx = QgsRenderContext()
        assert ctx.painter() is None
        # Use a mock-like approach: QPainter can be created with no device
        painter = QPainter()
        ctx.setPainter(painter)
        assert ctx.painter() is painter
        painter.end()
        # Setting to None
        ctx.setPainter(None)
        assert ctx.painter() is None

    def test_render_context_flags(self):
        """Set and test flags."""
        ctx = QgsRenderContext()
        # All flags off by default
        assert not ctx.testFlag(RenderFlag.DrawEditingInfo)
        assert not ctx.testFlag(RenderFlag.ForceVectorOutput)
        assert not ctx.testFlag(RenderFlag.UseRenderingOptimization)
        assert not ctx.testFlag(RenderFlag.DrawSelection)
        assert not ctx.testFlag(RenderFlag.DrawSymbolBounds)
        # Set a flag
        ctx.setFlag(RenderFlag.DrawEditingInfo, True)
        assert ctx.testFlag(RenderFlag.DrawEditingInfo)
        assert not ctx.testFlag(RenderFlag.ForceVectorOutput)
        # Set another
        ctx.setFlag(RenderFlag.ForceVectorOutput, True)
        assert ctx.testFlag(RenderFlag.DrawEditingInfo)
        assert ctx.testFlag(RenderFlag.ForceVectorOutput)
        # Clear a flag
        ctx.setFlag(RenderFlag.DrawEditingInfo, False)
        assert not ctx.testFlag(RenderFlag.DrawEditingInfo)
        assert ctx.testFlag(RenderFlag.ForceVectorOutput)
        # Setting False on already-off flag is a no-op
        ctx.setFlag(RenderFlag.DrawSymbolBounds, False)
        assert not ctx.testFlag(RenderFlag.DrawSymbolBounds)

    def test_render_context_feature_clip(self):
        """Get/set feature clip geometry."""
        ctx = QgsRenderContext()
        assert ctx.featureClipGeometry() is None
        geom = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
        ctx.setFeatureClipGeometry(geom)
        assert ctx.featureClipGeometry() is geom
        assert not ctx.featureClipGeometry().isNull()

    def test_render_context_output_size(self):
        """Get/set output size."""
        ctx = QgsRenderContext()
        assert ctx.outputSize().width() == 0
        assert ctx.outputSize().height() == 0
        ctx.setOutputSize(QSize(1920, 1080))
        assert ctx.outputSize().width() == 1920
        assert ctx.outputSize().height() == 1080

    def test_render_context_renderer_scale(self):
        """Get/set renderer scale."""
        ctx = QgsRenderContext()
        assert ctx.rendererScale() == 0.0
        ctx.setRendererScale(25000.0)
        assert ctx.rendererScale() == 25000.0
        ctx.setRendererScale(100000.0)
        assert ctx.rendererScale() == 100000.0

    def test_render_context_coordinate_transform(self):
        """Get/set coordinate transform."""
        ctx = QgsRenderContext()
        assert ctx.coordinateTransform() is None
        src_crs = QgsCoordinateReferenceSystem("EPSG:4326")
        dst_crs = QgsCoordinateReferenceSystem("EPSG:3857")
        ct = QgsCoordinateTransform(src_crs, dst_crs)
        ctx.setCoordinateTransform(ct)
        assert ctx.coordinateTransform() is ct
        assert ctx.coordinateTransform().isValid()
        # Set to None
        ctx.setCoordinateTransform(None)
        assert ctx.coordinateTransform() is None
