from core.qgsdataprovider import QgsDataProvider
from core.qgsreader import GeospatialReader

class GDALDataProvider(QgsDataProvider):
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


RasterDataProvider = GDALDataProvider
