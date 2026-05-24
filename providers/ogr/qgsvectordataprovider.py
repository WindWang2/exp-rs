from core.qgsdataprovider import QgsDataProvider
from core.qgsreader import GeospatialReader

class OGRDataProvider(QgsDataProvider):
    """
    Data provider for OGR-compatible vector formats.
    """
    def __init__(self, uri: str):
        self.reader = GeospatialReader(uri)
        if self.reader.is_raster:
            raise ValueError(f"URI {uri} points to a raster dataset, expected vector.")

    def extent(self):
        """
        Returns the geographic extent of the vector dataset.
        """
        return self.reader.metadata["bounds"]

    def get_features(self, extent=None):
        """
        Fetches features from the data source, optionally filtered by extent.
        """
        all_features = self.reader.read_vector_features()
        
        if extent is None:
            return all_features
            
        # Basic spatial filtering
        filtered = []
        for feat in all_features:
            shape = feat.get("shape")
            if shape:
                # fb: (minx, miny, maxx, maxy)
                minx, miny, maxx, maxy = shape.bounds
                if (maxx >= extent['left'] and minx <= extent['right'] and
                     maxy >= extent['bottom'] and miny <= extent['top']):
                    filtered.append(feat)
        return filtered


VectorDataProvider = OGRDataProvider
