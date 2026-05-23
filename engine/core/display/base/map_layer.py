from abc import ABC, abstractmethod

class MapLayer(ABC):
    def __init__(self, layer_id: str, name: str):
        self.id = layer_id
        self.name = name
        self.visible = True
        self.opacity = 1.0
        self.crs = None
        self.extent = None

    @abstractmethod
    def draw(self, painter, settings):
        pass
