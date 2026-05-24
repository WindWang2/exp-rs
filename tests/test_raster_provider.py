"""Tests for QgsRasterDataProvider (rasterio backend)."""

import os
import pytest
import numpy as np
import rasterio
from rasterio.transform import from_origin

from core.raster.qgsrasterdataprovider import QgsRasterDataProvider
from core.raster.qgsrasterinterface import QgsRasterInterface
from core.raster.qgsrasterblock import QgsRasterBlock
from core.qgsrectangle import QgsRectangle
from core.qgis import Qgis


# ---------------------------------------------------------------------------
# Fixture: a small 3-band uint8 GeoTIFF (32x32, EPSG:3857)
# ---------------------------------------------------------------------------

@pytest.fixture
def temp_raster(tmp_path):
    """Create a small 3-band uint8 GeoTIFF for testing."""
    file_path = str(tmp_path / "test_raster.tif")
    h, w = 32, 32
    transform = from_origin(0, 32, 1, 1)  # 1-degree pixels

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
def float_raster(tmp_path):
    """Create a single-band float32 GeoTIFF with explicit nodata."""
    file_path = str(tmp_path / "float_raster.tif")
    h, w = 64, 64
    transform = from_origin(100, 200, 0.5, 0.5)

    data = np.random.default_rng(42).uniform(0, 1000, (h, w)).astype(np.float32)

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
# Construction / metadata
# ---------------------------------------------------------------------------

class TestProviderCreation:
    def test_create_from_raster(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert provider is not None

    def test_rejects_non_raster(self, tmp_path):
        """Provider must raise for files that are not valid rasters.

        GeospatialReader may raise its own exception (e.g. fiona DriverError)
        before we reach the is_raster check, so accept any exception.
        """
        shp = tmp_path / "not_a_raster.shp"
        shp.write_text("dummy")
        with pytest.raises(Exception):
            QgsRasterDataProvider(str(shp))

    def test_rejects_vector_file(self, tmp_path):
        """Provider must raise ValueError for a valid vector file."""
        geojson = tmp_path / "test.geojson"
        geojson.write_text(
            '{"type": "FeatureCollection", "features": ['
            '{"type": "Feature", "geometry": {"type": "Point", "coordinates": [0, 0]},'
            '"properties": {"id": 1}}]}'
        )
        with pytest.raises(ValueError, match="Not a raster"):
            QgsRasterDataProvider(str(geojson))

    def test_is_raster_interface(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert isinstance(provider, QgsRasterInterface)

    def test_type_is_provider(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert provider.type() == QgsRasterInterface.InterfaceType.Provider


# ---------------------------------------------------------------------------
# bandCount / dataType
# ---------------------------------------------------------------------------

class TestBandMetadata:
    def test_band_count(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert provider.bandCount() == 3

    def test_band_count_single(self, float_raster):
        provider = QgsRasterDataProvider(float_raster)
        assert provider.bandCount() == 1

    def test_data_type_uint8(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert provider.dataType(1) == Qgis.DataType.Byte
        assert provider.dataType(2) == Qgis.DataType.Byte
        assert provider.dataType(3) == Qgis.DataType.Byte

    def test_data_type_float32(self, float_raster):
        provider = QgsRasterDataProvider(float_raster)
        assert provider.dataType(1) == Qgis.DataType.Float32

    def test_data_type_invalid_band(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        assert provider.dataType(99) == Qgis.DataType.Float32


# ---------------------------------------------------------------------------
# extent
# ---------------------------------------------------------------------------

class TestExtent:
    def test_extent_returns_qgs_rectangle(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        assert isinstance(ext, QgsRectangle)

    def test_extent_bounds_correct(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        # from_origin(0, 32, 1, 1) => left=0, top=32, pixel=1 degree
        assert ext.xMinimum() == pytest.approx(0.0)
        assert ext.xMaximum() == pytest.approx(32.0)
        assert ext.yMinimum() == pytest.approx(0.0)
        assert ext.yMaximum() == pytest.approx(32.0)

    def test_extent_float_raster(self, float_raster):
        provider = QgsRasterDataProvider(float_raster)
        ext = provider.extent()
        assert ext.xMinimum() == pytest.approx(100.0)
        assert ext.yMinimum() == pytest.approx(200.0 - 64 * 0.5)
        assert ext.xMaximum() == pytest.approx(100.0 + 64 * 0.5)
        assert ext.yMaximum() == pytest.approx(200.0)


# ---------------------------------------------------------------------------
# block()
# ---------------------------------------------------------------------------

class TestBlock:
    def test_block_full_extent(self, temp_raster):
        """Read the full raster at native resolution."""
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        block = provider.block(1, ext, 32, 32)

        assert isinstance(block, QgsRasterBlock)
        assert block.width() == 32
        assert block.height() == 32
        assert not block.isEmpty()
        assert block.data() is not None
        assert np.all(block.data() == 50)

    def test_block_second_band(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        block = provider.block(2, ext, 32, 32)

        assert block.width() == 32
        assert block.height() == 32
        assert np.all(block.data() == 70)

    def test_block_downsampled(self, temp_raster):
        """Request smaller output dimensions -- provider must downsample."""
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        block = provider.block(1, ext, 16, 16)

        assert block.width() == 16
        assert block.height() == 16
        # All values should still be 50 (constant band)
        assert np.all(block.data() == 50)

    def test_block_sub_extent(self, temp_raster):
        """Read a geographic subset."""
        provider = QgsRasterDataProvider(temp_raster)
        # Request a 10x10 degree window in the middle of the raster
        sub_ext = QgsRectangle(5.0, 5.0, 15.0, 15.0)
        block = provider.block(1, sub_ext, 10, 10)

        assert block.width() == 10
        assert block.height() == 10
        assert np.all(block.data() == 50)

    def test_block_invalid_band_returns_empty(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        block = provider.block(99, ext, 32, 32)
        assert block.isEmpty()

    def test_block_zero_dimensions_returns_empty(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        ext = provider.extent()
        block = provider.block(1, ext, 0, 0)
        assert block.isEmpty()

    def test_block_nodata_preserved(self, float_raster):
        """Nodata value from source should be propagated."""
        provider = QgsRasterDataProvider(float_raster)
        ext = provider.extent()
        block = provider.block(1, ext, 64, 64)

        assert block.noDataValue() == -9999.0

    def test_block_float_data_integrity(self, float_raster):
        """Float32 data should survive the round-trip."""
        provider = QgsRasterDataProvider(float_raster)
        ext = provider.extent()
        block = provider.block(1, ext, 64, 64)

        assert block.data().dtype == np.float32
        assert block.data().shape == (64, 64)

    def test_block_extent_outside_raster_returns_empty(self, temp_raster):
        """An extent completely outside the raster should yield an empty block."""
        provider = QgsRasterDataProvider(temp_raster)
        outside = QgsRectangle(1000.0, 1000.0, 2000.0, 2000.0)
        block = provider.block(1, outside, 32, 32)
        assert block.isEmpty()


# ---------------------------------------------------------------------------
# clone()
# ---------------------------------------------------------------------------

class TestClone:
    def test_clone_returns_same_type(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        cloned = provider.clone()
        assert isinstance(cloned, QgsRasterDataProvider)

    def test_clone_has_same_metadata(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        cloned = provider.clone()
        assert cloned.bandCount() == provider.bandCount()
        assert cloned.extent() == provider.extent()

    def test_clone_is_independent(self, temp_raster):
        """Clone must open its own GeospatialReader (thread safety)."""
        provider = QgsRasterDataProvider(temp_raster)
        cloned = provider.clone()
        assert cloned._reader is not provider._reader

    def test_clone_block_reads_correctly(self, temp_raster):
        provider = QgsRasterDataProvider(temp_raster)
        cloned = provider.clone()
        ext = cloned.extent()
        block = cloned.block(1, ext, 32, 32)
        assert block.width() == 32
        assert np.all(block.data() == 50)


# ---------------------------------------------------------------------------
# Integration with real project data (if available)
# ---------------------------------------------------------------------------

class TestWithRealData:
    @pytest.mark.skipif(
        not os.path.exists("data/sample_crops.tif"),
        reason="Sample data not found"
    )
    def test_real_raster_metadata(self):
        provider = QgsRasterDataProvider("data/sample_crops.tif")
        assert provider.bandCount() > 0
        ext = provider.extent()
        assert isinstance(ext, QgsRectangle)
        assert not ext.isEmpty()

    @pytest.mark.skipif(
        not os.path.exists("data/sample_crops.tif"),
        reason="Sample data not found"
    )
    def test_real_raster_block(self):
        provider = QgsRasterDataProvider("data/sample_crops.tif")
        ext = provider.extent()
        block = provider.block(1, ext, 64, 64)
        assert block.width() == 64
        assert block.height() == 64
        assert not block.isEmpty()
