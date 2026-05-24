from abc import ABC, abstractmethod

class QgsMapLayer(ABC):
    def __init__(self, layer_id: str, name: str):
        self.id = layer_id
        self.name = name
        self.visible = True
        self.opacity = 1.0
        self.crs = None

    @property
    @abstractmethod
    def extent(self):
        pass

    @abstractmethod
    def createMapRenderer(self, settings):
        """Creates a decoupled MapLayerRenderer instance for thread-safe drawing."""
        pass

    def draw(self, painter, settings):
        """Default draw implementation calling the decoupled renderer for backward-compatibility."""
        renderer = self.createMapRenderer(settings)
        if renderer:
            renderer.render(painter, settings)


MapLayer = QgsMapLayer
