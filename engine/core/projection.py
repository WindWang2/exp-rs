from typing import Tuple, List, Dict, Any
from pyproj import Transformer, CRS
from shapely.ops import transform as shapely_transform

class CRSTransformer:
    """
    Handles coordinate reference system conversions for raster bounds,
    vector shapes, and canvas display coordinates.
    """
    def __init__(self, source_crs_str: str, target_crs_str: str = "EPSG:3857"):
        self.source_crs_str = source_crs_str
        self.target_crs_str = target_crs_str
        
        if source_crs_str and target_crs_str:
            try:
                self.src_crs = CRS.from_user_input(source_crs_str)
                self.tgt_crs = CRS.from_user_input(target_crs_str)
                # always_xy=True ensures output is (longitude, latitude) or (easting, northing)
                # rather than depending on strict geodetic standards where lat/lon ordering is flipped.
                self.transformer = Transformer.from_crs(self.src_crs, self.tgt_crs, always_xy=True)
                self.inverse_transformer = Transformer.from_crs(self.tgt_crs, self.src_crs, always_xy=True)
            except Exception as e:
                print(f"Error creating CRS transformer: {e}")
                self.transformer = None
                self.inverse_transformer = None
        else:
            self.transformer = None
            self.inverse_transformer = None

    def transform_point(self, x: float, y: float) -> Tuple[float, float]:
        """Transforms a single coordinate point (x, y) -> (x', y')"""
        if not self.transformer:
            return x, y
        return self.transformer.transform(x, y)

    def inverse_transform_point(self, x: float, y: float) -> Tuple[float, float]:
        """Transforms canvas coordinates back to source layers coordinates."""
        if not self.inverse_transformer:
            return x, y
        return self.inverse_transformer.transform(x, y)

    def transform_bounds(self, left: float, bottom: float, right: float, top: float) -> Tuple[float, float, float, float]:
        """Transforms a bounding box."""
        if not self.transformer:
            return left, bottom, right, top
        # Transform the four corner coordinates
        xs, ys = self.transformer.transform(
            [left, left, right, right],
            [bottom, top, bottom, top]
        )
        return min(xs), min(ys), max(xs), max(ys)

    def transform_geometry(self, geom_shape):
        """Transforms a Shapely geometry object in a thread-safe manner."""
        if not self.transformer or geom_shape is None:
            return geom_shape
        # shapely_transform applies a transformation function (in this case, transformer.transform)
        # to all coordinates of a shape.
        return shapely_transform(self.transformer.transform, geom_shape)
