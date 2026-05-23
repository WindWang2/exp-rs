from abc import ABC, abstractmethod

class FeatureRenderer(ABC):
    """
    Base class for feature renderers.
    Defines the strategy for how vector features are rendered.
    """
    @abstractmethod
    def render_feature(self, feature, painter, settings):
        """
        Renders a single feature using the provided painter and settings.
        
        Args:
            feature: The feature to render (dict with 'shape', 'properties', etc.)
            painter: QPainter instance
            settings: MapSettings instance
        """
        pass
