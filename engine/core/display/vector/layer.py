from engine.core.display.base.map_layer import MapLayer
from engine.core.display.vector.provider import OGRDataProvider
from engine.core.display.renderers.vector.single_symbol import SingleSymbolRenderer
from PySide6.QtCore import QRectF

class VectorLayer(MapLayer):
    """
    Map layer for displaying vector data.
    Supports On-The-Fly (OTF) projection of geometry boundaries and coordinates.
    """
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = OGRDataProvider(uri)
        self.renderer = SingleSymbolRenderer()
        ext = self.provider.extent()
        self.crs = self.provider.reader.metadata.get("crs")
        
        # Calculate raw extent in native projection coordinates
        self.raw_extent = QRectF(ext["left"], ext["top"], ext["right"] - ext["left"], ext["top"] - ext["bottom"])
        
        # Project extent to Web Mercator (EPSG:3857) for consistent canvas viewport bounds
        if self.crs and self.crs != "EPSG:3857":
            from engine.core.projection import CRSTransformer
            try:
                self.otf_transformer = CRSTransformer(self.crs, "EPSG:3857")
                xmin, ymin, xmax, ymax = self.otf_transformer.transform_bounds(ext["left"], ext["bottom"], ext["right"], ext["top"])
                self.extent = QRectF(xmin, ymax, xmax - xmin, ymax - ymin)
            except Exception as e:
                print(f"Error reprojecting vector extent for {name}: {e}")
                self.extent = self.raw_extent
                self.otf_transformer = None
        else:
            self.extent = self.raw_extent
            self.otf_transformer = None

    def set_renderer(self, renderer):
        """
        Sets a new renderer for the layer.
        """
        self.renderer = renderer

    def draw(self, painter, settings):
        """
        Draws the vector layer, projecting bounds and geometries on-the-fly if needed.
        """
        if not self.visible or painter is None:
            return

        # 1. Determine extent to fetch
        view_extent = settings.extent if settings.extent else self.extent
        
        # 2. Fetch features: if OTF is active, inverse-transform view_extent to layer native CRS
        if self.otf_transformer:
            try:
                xmin, ymin, xmax, ymax = self.otf_transformer.inverse_transform_bounds(
                    view_extent.left(),
                    view_extent.top() - view_extent.height(),
                    view_extent.right(),
                    view_extent.top()
                )
                ext_dict = {
                    "left": xmin,
                    "right": xmax,
                    "top": ymax,
                    "bottom": ymin
                }
            except Exception as e:
                print(f"Error inverse projecting viewport bounds: {e}")
                ext_dict = {
                    "left": view_extent.left(),
                    "right": view_extent.right(),
                    "top": view_extent.top(),
                    "bottom": view_extent.top() - view_extent.height()
                }
        else:
            ext_dict = {
                "left": view_extent.left(),
                "right": view_extent.right(),
                "top": view_extent.top(),
                "bottom": view_extent.top() - view_extent.height()
            }
        
        features = self.provider.get_features(ext_dict)
        
        # 3. Apply opacity if needed
        old_opacity = painter.opacity()
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity * self.opacity)
            
        # 4. Render features
        for feature in features:
            if self.otf_transformer:
                try:
                    # Project geometry shape on-the-fly to match canvas coordinates
                    projected_shape = self.otf_transformer.transform_geometry(feature.get("shape"))
                    # Create a temporary projected feature so as not to modify the cached feature
                    projected_feature = {
                        "id": feature.get("id"),
                        "properties": feature.get("properties"),
                        "shape": projected_shape
                    }
                    self.renderer.render_feature(projected_feature, painter, settings)
                except Exception as e:
                    print(f"Error drawing projected vector feature: {e}")
                    self.renderer.render_feature(feature, painter, settings)
            else:
                self.renderer.render_feature(feature, painter, settings)
            
        # 5. Restore opacity
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity)
