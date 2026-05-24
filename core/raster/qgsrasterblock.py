import numpy as np


class QgsRasterBlock:
    """Holds a rectangular block of raster data for one band."""

    __slots__ = ('_data', '_no_data_value', '_width', '_height', '_data_type')

    def __init__(self, data_type=None, width=0, height=0, no_data_value=None):
        self._data_type = data_type
        self._width = width
        self._height = height
        self._no_data_value = no_data_value
        self._data = None

        if width > 0 and height > 0:
            dtype = self._numpy_dtype(data_type)
            if dtype is not None:
                self._data = np.full(
                    (height, width),
                    no_data_value if no_data_value is not None else 0,
                    dtype=dtype,
                )

    @staticmethod
    def from_numpy(array: np.ndarray, no_data_value=None) -> 'QgsRasterBlock':
        """Create a QgsRasterBlock from an existing numpy array."""
        block = QgsRasterBlock()
        block._data = array
        block._height, block._width = array.shape[:2]
        block._no_data_value = no_data_value
        return block

    def data(self) -> np.ndarray:
        """Returns the underlying numpy array."""
        return self._data

    def setData(self, array: np.ndarray):
        """Replace the underlying data."""
        self._data = array
        if array is not None:
            self._height, self._width = array.shape[:2]

    def width(self) -> int:
        return self._width

    def height(self) -> int:
        return self._height

    def noDataValue(self):
        return self._no_data_value

    def setNoDataValue(self, value):
        self._no_data_value = value

    def isEmpty(self) -> bool:
        return self._data is None or self._width == 0 or self._height == 0

    def value(self, row: int, col: int):
        """Get value at (row, col). Returns noDataValue if nodata."""
        if self._data is None:
            return self._no_data_value
        return self._data[row, col]

    def setValue(self, row: int, col: int, value):
        """Set value at (row, col)."""
        if self._data is not None:
            self._data[row, col] = value

    def setMatrix(self, array: np.ndarray):
        """Set data from numpy array."""
        self._data = array
        if array is not None:
            self._height, self._width = array.shape[:2]

    @staticmethod
    def _numpy_dtype(data_type):
        """Map Qgis.DataType to numpy dtype."""
        from core.qgis import Qgis
        if data_type is None:
            return np.float32
        mapping = {
            Qgis.DataType.Byte: np.uint8,
            Qgis.DataType.UInt16: np.uint16,
            Qgis.DataType.Int16: np.int16,
            Qgis.DataType.UInt32: np.uint32,
            Qgis.DataType.Int32: np.int32,
            Qgis.DataType.Float32: np.float32,
            Qgis.DataType.Float64: np.float64,
        }
        return mapping.get(data_type, np.float32)
