from engine.core.display.base.map_layer import MapLayer
from engine.core.display.vector.provider import OGRDataProvider
from engine.core.display.renderers.vector.single_symbol import SingleSymbolRenderer
from PySide6.QtCore import QRectF

class VectorLayer(MapLayer):
    """
    Map layer for displaying vector data.
    """
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = OGRDataProvider(uri)
        self.renderer = SingleSymbolRenderer()
        ext = self.provider.extent()
        # Convert dict extent to QRectF
        self.extent = QRectF(ext["left"], ext["top"], ext["right"] - ext["left"], ext["top"] - ext["bottom"])
        self.crs = self.provider.reader.metadata.get("crs")

    def set_renderer(self, renderer):
        """
        Sets a new renderer for the layer.
        """
        self.renderer = renderer

    def draw(self, painter, settings):
        """
        Draws the vector layer.
        """
        if not self.visible or painter is None:
            return

        # 1. Determine extent to fetch
        view_extent = settings.extent if settings.extent else self.extent
        
        # 2. Fetch features (convert QRectF to dict for provider if needed, or update provider)
        # OGRDataProvider currently expects a dict-like or something with ['left'] etc.
        # Wait, OGRDataProvider.get_features(extent=None) does:
        # if (maxx >= extent['left'] and minx <= extent['right'] and ...)
        # So I should pass a dict.
        
        ext_dict = {
            "left": view_extent.left(),
            "right": view_extent.right(),
            "top": view_extent.top(), # max Y
            "bottom": view_extent.top() - view_extent.height() # min Y
        }
        
        features = self.provider.get_features(ext_dict)
        
        # 3. Apply opacity if needed
        old_opacity = painter.opacity()
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity * self.opacity)
            
        # 4. Render features
        for feature in features:
            self.renderer.render_feature(feature, painter, settings)
            
        # 5. Restore opacity
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity)
