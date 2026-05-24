"""Tests for QgsRasterPipe — 7-stage raster processing pipeline."""

import os
import pytest
import numpy as np
import rasterio
from rasterio.transform import from_origin

from core.raster.qgsrasterpipe import QgsRasterPipe
from core.raster.qgsrasterinterface import QgsRasterInterface
from core.raster.qgsrasterdataprovider import QgsRasterDataProvider
from core.raster.qgsrasterblock import QgsRasterBlock
from core.qgsrectangle import QgsRectangle


# ---------------------------------------------------------------------------
# Fixture: a small 3-band uint8 GeoTIFF
# ---------------------------------------------------------------------------

@pytest.fixture
def temp_raster(tmp_path):
    """Create a small 3-band uint8 GeoTIFF for testing."""
    file_path = str(tmp_path / "pipe_test.tif")
    h, w = 32, 32
    transform = from_origin(0, 32, 1, 1)

    b1 = np.full((h, w), 50, dtype=np.uint8)
    b2 = np.full((h, w), 70, dtype=np.uint8)
    b3 = np.full((h, w), 120, dtype=np.uint8)

    profile = {
        'driver': 'GTiff',
        'dtype': 'uint8',
        'width': w,
        'height': h,
        'count': 3,
        'crs': 'EPSG:3857',
        'transform': transform,
        'nodata': 0,
    }

    with rasterio.open(file_path, 'w', **profile) as dst:
        dst.write(b1, 1)
        dst.write(b2, 2)
        dst.write(b3, 3)

    return file_path


@pytest.fixture
def single_band_raster(tmp_path):
    """Create a single-band float32 GeoTIFF."""
    file_path = str(tmp_path / "single_band.tif")
    h, w = 16, 16
    transform = from_origin(0, 16, 1, 1)
    data = np.random.default_rng(42).uniform(0, 255, (h, w)).astype(np.float32)

    profile = {
        'driver': 'GTiff',
        'dtype': 'float32',
        'width': w,
        'height': h,
        'count': 1,
        'crs': 'EPSG:4326',
        'transform': transform,
        'nodata': -9999.0,
    }

    with rasterio.open(file_path, 'w', **profile) as dst:
        dst.write(data, 1)

    return file_path


# ---------------------------------------------------------------------------
# Pipe creation
# ---------------------------------------------------------------------------

class TestPipeCreation:
    def test_create_empty_pipe(self):
        pipe = QgsRasterPipe()
        assert pipe is not None

    def test_empty_pipe_provider_is_none(self):
        pipe = QgsRasterPipe()
        assert pipe.provider() is None

    def test_empty_pipe_renderer_is_none(self):
        pipe = QgsRasterPipe()
        assert pipe.renderer() is None


# ---------------------------------------------------------------------------
# Setting interfaces
# ---------------------------------------------------------------------------

class TestPipeSet:
    def test_set_provider(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        result = pipe.set(provider)
        assert result is True
        assert pipe.provider() is provider

    def test_set_provider_type_slot(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)
        iface = pipe.at(QgsRasterInterface.InterfaceType.Provider)
        assert iface is provider

    def test_set_rejects_non_interface(self):
        pipe = QgsRasterPipe()
        result = pipe.set("not an interface")
        assert result is False

    def test_set_none_rejects(self):
        pipe = QgsRasterPipe()
        result = pipe.set(None)
        assert result is False


# ---------------------------------------------------------------------------
# Renderer slot
# ---------------------------------------------------------------------------

class TestPipeRenderer:
    def test_renderer_slot_accepts_anything(self, temp_raster):
        """Renderer doesn't extend QgsRasterInterface, so we use setRenderer."""
        pipe = QgsRasterPipe()
        mock_renderer = object()  # Stand-in for a renderer
        pipe.setRenderer(mock_renderer)
        assert pipe.renderer() is mock_renderer

    def test_renderer_type_slot(self, temp_raster):
        pipe = QgsRasterPipe()
        mock_renderer = object()
        pipe.setRenderer(mock_renderer)
        iface = pipe.at(QgsRasterInterface.InterfaceType.Renderer)
        assert iface is mock_renderer

    def test_set_renderer_none(self):
        pipe = QgsRasterPipe()
        pipe.setRenderer(None)
        assert pipe.renderer() is None


# ---------------------------------------------------------------------------
# On/Off control
# ---------------------------------------------------------------------------

class TestPipeOnOff:
    def test_provider_starts_on(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)
        assert pipe.on(QgsRasterInterface.InterfaceType.Provider) is True

    def test_renderer_starts_on(self):
        pipe = QgsRasterPipe()
        pipe.setRenderer(object())
        assert pipe.on(QgsRasterInterface.InterfaceType.Renderer) is True

    def test_middle_stages_start_off(self):
        pipe = QgsRasterPipe()
        assert pipe.on(QgsRasterInterface.InterfaceType.Nuller) is False
        assert pipe.on(QgsRasterInterface.InterfaceType.Resampler) is False
        assert pipe.on(QgsRasterInterface.InterfaceType.Brightness) is False
        assert pipe.on(QgsRasterInterface.InterfaceType.HueSaturation) is False

    def test_set_on_off(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        pipe.setOff(QgsRasterInterface.InterfaceType.Provider)
        assert pipe.on(QgsRasterInterface.InterfaceType.Provider) is False

        pipe.setOn(QgsRasterInterface.InterfaceType.Provider, True)
        assert pipe.on(QgsRasterInterface.InterfaceType.Provider) is True

    def test_set_on_with_false(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        pipe.setOn(QgsRasterInterface.InterfaceType.Provider, False)
        assert pipe.on(QgsRasterInterface.InterfaceType.Provider) is False


# ---------------------------------------------------------------------------
# block() passthrough
# ---------------------------------------------------------------------------

class TestPipeBlock:
    def test_block_through_provider(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        ext = provider.extent()
        block = pipe.block(1, ext, 32, 32)
        assert isinstance(block, QgsRasterBlock)
        assert block.width() == 32
        assert block.height() == 32
        assert not block.isEmpty()
        assert np.all(block.data() == 50)

    def test_block_disabled_provider_returns_empty(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)
        pipe.setOff(QgsRasterInterface.InterfaceType.Provider)

        ext = provider.extent()
        block = pipe.block(1, ext, 32, 32)
        assert block.isEmpty()

    def test_block_no_provider_returns_empty(self):
        pipe = QgsRasterPipe()
        ext = QgsRectangle(0, 0, 32, 32)
        block = pipe.block(1, ext, 32, 32)
        assert block.isEmpty()

    def test_block_second_band(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        ext = provider.extent()
        block = pipe.block(2, ext, 32, 32)
        assert np.all(block.data() == 70)


# ---------------------------------------------------------------------------
# clone()
# ---------------------------------------------------------------------------

class TestPipeClone:
    def test_clone_has_provider(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        cloned = pipe.clone()
        assert cloned.provider() is not None
        assert cloned.provider() is not pipe.provider()
        assert cloned.provider().bandCount() == provider.bandCount()

    def test_clone_has_renderer(self):
        pipe = QgsRasterPipe()
        mock_renderer = {"type": "test"}
        pipe.setRenderer(mock_renderer)

        cloned = pipe.clone()
        # Renderer is shallow-copied (not a QgsRasterInterface)
        assert cloned.renderer() is mock_renderer

    def test_clone_preserves_on_off_state(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)
        pipe.setOff(QgsRasterInterface.InterfaceType.Provider)

        cloned = pipe.clone()
        assert cloned.on(QgsRasterInterface.InterfaceType.Provider) is False

    def test_clone_independent_block(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        cloned = pipe.clone()
        ext = cloned.provider().extent()
        block = cloned.block(1, ext, 32, 32)
        assert np.all(block.data() == 50)


# ---------------------------------------------------------------------------
# Integration: full pipeline with real provider
# ---------------------------------------------------------------------------

class TestPipeIntegration:
    def test_full_pipe_with_provider_and_renderer(self, temp_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(temp_raster)
        pipe.set(provider)

        # Simulate a renderer (not a QgsRasterInterface)
        pipe.setRenderer({"band_count": 3})

        assert pipe.provider() is provider
        assert pipe.renderer() is not None

        ext = provider.extent()
        # block() should still work through the provider
        block = pipe.block(1, ext, 32, 32)
        assert not block.isEmpty()

    def test_pipe_with_single_band(self, single_band_raster):
        pipe = QgsRasterPipe()
        provider = QgsRasterDataProvider(single_band_raster)
        pipe.set(provider)

        assert provider.bandCount() == 1
        ext = provider.extent()
        block = pipe.block(1, ext, 16, 16)
        assert not block.isEmpty()
        assert block.width() == 16
