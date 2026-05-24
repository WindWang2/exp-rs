"""QgsSymbol — ordered container of QgsSymbolLayer objects.

Mirrors the QGIS QgsSymbol class: holds a list of symbol layers and
delegates rendering to each enabled layer in order.
"""

from __future__ import annotations

from core.symbology.qgssymbollayer import QgsSymbolLayer


class QgsSymbol:
    """Container that owns an ordered list of QgsSymbolLayer objects."""

    class SymbolType(int):
        Fill = 0
        Line = 1
        Marker = 2

    def __init__(self, symbol_type: int, layers: list[QgsSymbolLayer] | None = None):
        self._type = int(symbol_type)
        self._layers: list[QgsSymbolLayer] = list(layers) if layers else []

    # -- accessors ----------------------------------------------------------

    def type(self) -> int:
        return self._type

    def symbolLayerCount(self) -> int:
        return len(self._layers)

    def symbolLayer(self, index: int) -> QgsSymbolLayer:
        return self._layers[index]

    # -- mutation -----------------------------------------------------------

    def addSymbolLayer(self, layer: QgsSymbolLayer) -> bool:
        """Append a symbol layer. Returns True."""
        self._layers.append(layer)
        return True

    def removeSymbolLayer(self, index: int) -> bool:
        """Remove the layer at *index*. Returns False if index is out of range."""
        if 0 <= index < len(self._layers):
            self._layers.pop(index)
            return True
        return False

    def removeLayer(self, index: int) -> bool:
        """Alias for removeSymbolLayer (convenience)."""
        return self.removeSymbolLayer(index)

    # -- render -------------------------------------------------------------

    def renderFeature(self, feature: dict, painter, renderContext) -> None:
        """Delegate rendering to each enabled symbol layer."""
        for layer in self._layers:
            if layer.isEnabled():
                layer.renderFeature(feature, painter, renderContext)

    # -- clone --------------------------------------------------------------

    def clone(self) -> 'QgsSymbol':
        """Return an independent deep copy."""
        cloned_layers = [layer.clone() for layer in self._layers]
        return QgsSymbol(self._type, cloned_layers)

    # -- dunder -------------------------------------------------------------

    def __repr__(self) -> str:
        type_names = {0: 'Fill', 1: 'Line', 2: 'Marker'}
        name = type_names.get(self._type, '?')
        return f"QgsSymbol({name}, layers={len(self._layers)})"
