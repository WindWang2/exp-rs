"""QgsRasterDataProvider -- QGIS-style raster data provider backed by rasterio/GeospatialReader."""

import math
import numpy as np
import rasterio
import rasterio.windows
from affine import Affine

from core.raster.qgsrasterinterface import QgsRasterInterface
from core.raster.qgsrasterblock import QgsRasterBlock
from core.qgsreader import GeospatialReader
from core.qgsrectangle import QgsRectangle
from core.qgis import Qgis


class QgsRasterDataProvider(QgsRasterInterface):
    """Raster data provider that wraps GeospatialReader and implements
    the QgsRasterInterface.block() API.

    This is the bridge between raw rasterio data access (GeospatialReader)
    and the QGIS-style raster rendering pipeline.

    Thread safety: each clone() opens its own GeospatialReader, so
    provider instances are safe to use from separate threads.
    """

    # Map rasterio/numpy dtype strings to Qgis.DataType
    _DTYPE_MAP = {
        'uint8': Qgis.DataType.Byte,
        'uint16': Qgis.DataType.UInt16,
        'int16': Qgis.DataType.Int16,
        'uint32': Qgis.DataType.UInt32,
        'int32': Qgis.DataType.Int32,
        'float32': Qgis.DataType.Float32,
        'float64': Qgis.DataType.Float64,
    }

    def __init__(self, uri: str):
        super().__init__()
        self._type = QgsRasterInterface.InterfaceType.Provider
        self._uri = uri
        self._reader = GeospatialReader(uri)

        if not self._reader.is_raster:
            raise ValueError(f"Not a raster dataset: {uri}")

        meta = self._reader.metadata
        self._transform = Affine(*meta["transform"])
        self._inv_transform = ~self._transform

        # Cache nodata values per band (read once, lightweight)
        self._nodata_values = self._read_nodata_values()

    @property
    def reader(self) -> 'GeospatialReader':
        """Public accessor for the underlying GeospatialReader.

        Backward-compatible with code that accesses ``provider.reader``.
        """
        return self._reader

    # ------------------------------------------------------------------
    # Metadata accessors
    # ------------------------------------------------------------------

    def bandCount(self) -> int:
        return self._reader.metadata["count"]

    def dataType(self, band_no: int) -> int:
        dtypes = self._reader.metadata["dtypes"]
        if band_no < 1 or band_no > len(dtypes):
            return Qgis.DataType.Float32
        return self._DTYPE_MAP.get(dtypes[band_no - 1], Qgis.DataType.Float32)

    def extent(self) -> QgsRectangle:
        b = self._reader.metadata["bounds"]
        return QgsRectangle(b["left"], b["bottom"], b["right"], b["top"])

    def sourceNoDataValue(self, band_no: int):
        """Return the source no-data value for *band_no* (1-based)."""
        if band_no < 1 or band_no > len(self._nodata_values):
            return None
        return self._nodata_values[band_no - 1]

    # ------------------------------------------------------------------
    # Core block() implementation
    # ------------------------------------------------------------------

    def block(self, band_no: int, extent, width: int, height: int,
              feedback=None) -> QgsRasterBlock:
        """Read a rectangular block of raster data.

        Parameters
        ----------
        band_no : int
            1-based band index.
        extent : QgsRectangle
            Geographic extent to read.
        width, height : int
            Desired output pixel dimensions.
        feedback : optional
            Ignored for now (progress callback placeholder).

        Returns
        -------
        QgsRasterBlock
            Block of shape (height, width), or an empty block on failure.
        """
        if band_no < 1 or band_no > self.bandCount():
            return QgsRasterBlock()
        if width <= 0 or height <= 0:
            return QgsRasterBlock()

        # --- Convert geographic extent to pixel window ---
        window = self._extent_to_window(extent)
        if window is None:
            return QgsRasterBlock()

        # --- Compute scale factor for the reader ---
        win_w = int(window.width)
        win_h = int(window.height)
        if win_w <= 0 or win_h <= 0:
            return QgsRasterBlock()

        scale_x = win_w / width
        scale_y = win_h / height
        scale_factor = max(1, int(math.ceil(max(scale_x, scale_y))))

        # --- Read band data via GeospatialReader ---
        try:
            data = self._reader.read_raster_band(
                band_no, scale_factor=scale_factor, window=window
            )
        except (IndexError, ValueError):
            return QgsRasterBlock()

        # --- Resize to exact requested dimensions ---
        if data.shape[0] != height or data.shape[1] != width:
            data = self._resize_array(data, height, width)

        no_data = self.sourceNoDataValue(band_no)
        return QgsRasterBlock.from_numpy(data, no_data_value=no_data, data_type=self.dataType(band_no))

    # ------------------------------------------------------------------
    # Thread-safe cloning
    # ------------------------------------------------------------------

    def clone(self) -> 'QgsRasterDataProvider':
        """Return an independent copy with its own GeospatialReader handle."""
        return QgsRasterDataProvider(self._uri)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _extent_to_window(self, extent) -> 'rasterio.windows.Window | None':
        """Convert a QgsRectangle to a rasterio Window clamped to raster bounds."""
        # Top-left and bottom-right in pixel coords
        col_tl, row_tl = self._inv_transform * (
            extent.xMinimum(), extent.yMaximum()
        )
        col_br, row_br = self._inv_transform * (
            extent.xMaximum(), extent.yMinimum()
        )

        col_off = int(math.floor(min(col_tl, col_br)))
        row_off = int(math.floor(min(row_tl, row_br)))
        col_end = int(math.ceil(max(col_tl, col_br)))
        row_end = int(math.ceil(max(row_tl, row_br)))

        # Clamp to raster pixel bounds
        meta = self._reader.metadata
        col_off = max(0, col_off)
        row_off = max(0, row_off)
        col_end = min(meta["width"], col_end)
        row_end = min(meta["height"], row_end)

        w = col_end - col_off
        h = row_end - row_off
        if w <= 0 or h <= 0:
            return None
        return rasterio.windows.Window(col_off, row_off, w, h)

    @staticmethod
    def _resize_array(data: np.ndarray, target_h: int,
                      target_w: int) -> np.ndarray:
        """Nearest-neighbour resize of a 2-D array to (target_h, target_w)."""
        src_h, src_w = data.shape
        row_idx = np.clip(
            (np.arange(target_h) * src_h / target_h).astype(int), 0, src_h - 1
        )
        col_idx = np.clip(
            (np.arange(target_w) * src_w / target_w).astype(int), 0, src_w - 1
        )
        return data[np.ix_(row_idx, col_idx)]

    def _read_nodata_values(self) -> list:
        """Read per-band nodata values from the source raster."""
        with rasterio.open(self._uri) as src:
            nodata = src.nodata
        return [nodata] * self._reader.metadata["count"]

    def crs(self) -> str:
        """Returns the CRS string of the source raster."""
        return self._reader.metadata.get("crs")
