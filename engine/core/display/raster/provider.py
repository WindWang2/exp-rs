from engine.core.display.base.data_provider import DataProvider
from engine.core.reader import GeospatialReader

class GDALDataProvider(DataProvider):
    """
    GDAL Data Provider using GeospatialReader for raster data access.
    """
    def __init__(self, uri: str):
        self.reader = GeospatialReader(uri)
    
    def extent(self):
        """
        Returns the geographic extent of the raster dataset.
        """
        return self.reader.metadata["bounds"]
