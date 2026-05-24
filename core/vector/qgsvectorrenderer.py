from abc import ABC, abstractmethod
from PySide6.QtGui import QPen, QBrush, QColor, QPainterPath
from PySide6.QtCore import Qt, QPointF

from core.symbology.qgssymbol import QgsSymbol
from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer
from core.qgsrendercontext import QgsRenderContext


class QgsFeatureRenderer(ABC):
    """
    Base class for feature renderers.
    Defines the strategy for how vector features are rendered, matching QgsFeatureRenderer.
    """
    @abstractmethod
    def render_feature(self, feature, painter, settings, renderContext=None):
        """
        Renders a single feature using the provided painter and settings.

        Args:
            feature: dict or QgsFeature to render
            painter: QPainter drawing buffer
            settings: QgsMapSettings for the render pass
            renderContext: Optional QgsRenderContext. If None, the renderer
                should create one from settings for backward compatibility.
        """
        pass


class QgsRendererCategory:
    """A mapping from an attribute value to a label + symbol.

    Used by QgsCategorizedSymbolRenderer to assign symbols to features
    based on the value of a categorical attribute.
    """

    def __init__(self, value=None, label=None, symbol=None):
        self._value = value
        self._label = label
        self._symbol = symbol

    def value(self):
        """Return the attribute value this category matches."""
        return self._value

    def label(self) -> str:
        """Return the display label for this category."""
        return self._label

    def symbol(self) -> QgsSymbol:
        """Return the symbol used to render features in this category."""
        return self._symbol

    def setValue(self, value) -> None:
        """Set the attribute value this category matches."""
        self._value = value

    def setLabel(self, label: str) -> None:
        """Set the display label for this category."""
        self._label = label

    def setSymbol(self, symbol: QgsSymbol) -> None:
        """Set the symbol used to render features in this category."""
        self._symbol = symbol


class QgsSingleSymbolRenderer(QgsFeatureRenderer):
    """
    Renders all features using the same symbol.

    Refactored to use QgsSymbol internally. Maintains backward compatibility
    with the old color/stroke_color/stroke_width kwargs interface.
    """

    def __init__(self, symbol=None, **kwargs):
        # Backward compatibility: old kwargs interface
        has_color_kwargs = any(
            k in kwargs for k in ('color', 'stroke_color', 'stroke_width')
        )

        if symbol is not None:
            self._symbol = symbol
        elif has_color_kwargs:
            # Build a QgsSymbol from the legacy color kwargs
            color = kwargs.get('color', QColor(255, 0, 0, 100))
            stroke_color = kwargs.get('stroke_color', QColor(255, 0, 0))
            stroke_width = kwargs.get('stroke_width', 1)
            fill_layer = QgsSimpleFillSymbolLayer(
                fill_color=color,
                stroke_color=stroke_color,
                stroke_width=stroke_width,
            )
            self._symbol = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        else:
            # Default: create a symbol with a fill layer
            fill_layer = QgsSimpleFillSymbolLayer()
            self._symbol = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])

    def symbol(self) -> QgsSymbol:
        """Return the internal QgsSymbol."""
        return self._symbol

    def setSymbol(self, symbol: QgsSymbol) -> None:
        """Replace the internal QgsSymbol."""
        self._symbol = symbol

    def render_feature(self, feature, painter, settings, renderContext=None):
        """Render a feature using the internal symbol.

        Parameters
        ----------
        feature : dict or QgsFeature
            The feature to render.
        painter : QPainter
            The painter to draw on.
        settings : QgsMapSettings
            Map settings (used to build a QgsRenderContext if none provided).
        renderContext : QgsRenderContext, optional
            Pre-built render context from the pipeline. If None, one is
            created from settings for backward compatibility.
        """
        # Use the provided render context, or create one from settings
        if renderContext is None:
            rc = QgsRenderContext.fromMapSettings(settings)
            rc.setPainter(painter)
        else:
            rc = renderContext
        self._symbol.renderFeature(feature, painter, rc)

    # -- Backward-compatible accessors (delegate to first symbol layer) ----

    def color(self) -> QColor:
        """Return fill color of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'color'):
                return layer.color()
        return QColor(255, 0, 0, 100)

    def set_color(self, color: QColor):
        """Set fill color of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'setColor'):
                layer.setColor(color)

    def stroke_color(self) -> QColor:
        """Return stroke color of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'strokeColor'):
                return layer.strokeColor()
        return QColor(255, 0, 0)

    def set_stroke_color(self, color: QColor):
        """Set stroke color of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'setStrokeColor'):
                layer.setStrokeColor(color)

    def stroke_width(self) -> int:
        """Return stroke width of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'strokeWidth'):
                return layer.strokeWidth()
        return 1

    def set_stroke_width(self, width: int):
        """Set stroke width of the first symbol layer (backward compat)."""
        if self._symbol.symbolLayerCount() > 0:
            layer = self._symbol.symbolLayer(0)
            if hasattr(layer, 'setStrokeWidth'):
                layer.setStrokeWidth(width)


class QgsCategorizedSymbolRenderer(QgsFeatureRenderer):
    """Renders features based on a categorical attribute.

    Each feature's attribute value is matched against a list of
    QgsRendererCategory objects. The matching category's symbol is used
    to render the feature. A default symbol is used for unmatched features.
    """

    def __init__(self, attr_name: str, default_symbol: QgsSymbol = None):
        self._attr_name = attr_name
        self._default_symbol = default_symbol
        self._categories: list[QgsRendererCategory] = []

    def categories(self) -> list:
        """Return the list of QgsRendererCategory objects."""
        return list(self._categories)

    def setCategories(self, categories: list) -> None:
        """Replace all categories."""
        self._categories = list(categories)

    def addCategory(self, category: QgsRendererCategory) -> None:
        """Append a category to the list."""
        self._categories.append(category)

    def removeCategory(self, index: int) -> None:
        """Remove the category at the given index."""
        self._categories.pop(index)

    def categoryForFeature(self, feature) -> QgsRendererCategory | None:
        """Find the category matching the feature's attribute value.

        Supports both dict-style features (with a "properties" sub-dict)
        and QgsFeature objects (via .attribute()).
        """
        # Extract attribute value from feature
        if isinstance(feature, dict):
            props = feature.get("properties", {})
            attr_val = props.get(self._attr_name)
        else:
            # QgsFeature object
            attr_val = feature.attribute(self._attr_name)

        # Match against categories
        for cat in self._categories:
            if attr_val == cat.value():
                return cat
        return None

    def attrName(self) -> str:
        """Return the attribute name used for categorization."""
        return self._attr_name

    def setAttrName(self, name: str) -> None:
        """Set the attribute name used for categorization."""
        self._attr_name = name

    def render_feature(self, feature, painter, settings, renderContext=None):
        """Render a feature using the matching category's symbol.

        Falls back to the default symbol if no category matches.
        Does nothing if there is no match and no default symbol.

        Parameters
        ----------
        feature : dict or QgsFeature
            The feature to render.
        painter : QPainter
            The painter to draw on.
        settings : QgsMapSettings
            Map settings (used to build a QgsRenderContext if none provided).
        renderContext : QgsRenderContext, optional
            Pre-built render context from the pipeline. If None, one is
            created from settings for backward compatibility.
        """
        cat = self.categoryForFeature(feature)

        # Use the provided render context, or create one from settings
        if renderContext is None:
            rc = QgsRenderContext.fromMapSettings(settings)
            rc.setPainter(painter)
        else:
            rc = renderContext

        if cat and cat.symbol():
            cat.symbol().renderFeature(feature, painter, rc)
        elif self._default_symbol:
            self._default_symbol.renderFeature(feature, painter, rc)


FeatureRenderer = QgsFeatureRenderer
SingleSymbolRenderer = QgsSingleSymbolRenderer
