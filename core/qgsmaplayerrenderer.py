from abc import ABC, abstractmethod

class QgsMapLayerRenderer(ABC):
    """
    Abstract Base Class for QGIS-identical decoupled background thread map layer renderers.
    Isolates styling parameters from main-thread QObject-based MapLayer objects for thread-safety,
    mirroring the design of QgsMapLayerRenderer in QGIS.
    """
    def __init__(self, layer_id: str):
        self.layer_id = layer_id

    @abstractmethod
    def render(self, painter, settings, renderContext=None):
        """
        Renders the layer onto the canvas inside the background thread.

        Args:
            painter: QPainter drawing buffer
            settings: MapSettings instance containing projection and viewport limits
            renderContext: Optional QgsRenderContext. If None, the renderer should
                create one from settings for backward compatibility.
        """
        pass


MapLayerRenderer = QgsMapLayerRenderer
