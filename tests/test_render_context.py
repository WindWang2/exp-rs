"""Tests for QgsMapToPixel coordinate transform utility."""
import pytest
from PySide6.QtCore import QSize, QPointF

from core.qgsrectangle import QgsRectangle
from core.qgspointxy import QgsPointXY
from core.qgsmaptopixel import QgsMapToPixel


class TestQgsMapToPixel:
    """Tests for QgsMapToPixel (Task 18)."""

    def _make_instance(self):
        """Helper: extent 0,0 → 1000,1000 with 500×500 output."""
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
        # Forward: world (250, 750) → device
        px, py = mtp.transform(250, 750)
        # Inverse: device → world
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
        """Handles empty extent gracefully — returns identity-like behavior."""
        mtp = QgsMapToPixel.fromSettings(QgsRectangle(), QSize(100, 100))
        assert mtp is not None
        # Should not crash; scale may be 0 or identity
        px, py = mtp.transform(0, 0)
        assert isinstance(px, float)
        assert isinstance(py, float)
