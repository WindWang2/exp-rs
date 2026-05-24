from abc import ABC, abstractmethod


class QgsRasterInterface(ABC):
    """Abstract base for all raster data pipeline stages."""

    class InterfaceType(int):
        Unknown = 0
        Provider = 1
        Reprojector = 2
        Nuller = 3
        Resampler = 4
        Brightness = 5
        HueSaturation = 6
        Renderer = 7

    def __init__(self):
        self._type = QgsRasterInterface.InterfaceType.Unknown

    def clone(self) -> 'QgsRasterInterface':
        """Override in subclasses."""
        return None

    def type(self) -> int:
        return self._type

    @abstractmethod
    def block(self, band_no: int, extent, width: int, height: int, feedback=None):
        """Read a block of raster data. Returns QgsRasterBlock."""
        pass

    def bandCount(self) -> int:
        return 0

    def dataType(self, band_no: int) -> int:
        """Returns Qgis.DataType for the given band."""
        from core.qgis import Qgis
        return Qgis.DataType.Float32

    def sourceDataType(self, band_no: int) -> int:
        return self.dataType(band_no)

    def extent(self):
        return None

    def capabilities(self) -> int:
        return 0
