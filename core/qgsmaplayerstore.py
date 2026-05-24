"""QgsMapLayerStore - centralized, reusable layer registry with Qt signals.

Unlike QgsProject (which is a singleton managing the full project state),
QgsMapLayerStore is a lightweight container that any component can use to
hold a set of map layers.  QgsProject may delegate to it internally.
"""

from PySide6.QtCore import QObject, Signal


class QgsMapLayerStore(QObject):
    """A reusable container for map layers, keyed by layer ID.

    Signals
    -------
    layerWasAdded(layer_id: str)
        Emitted after a layer is successfully added.
    layerWillBeRemoved(layer_id: str)
        Emitted just before a layer is removed from the store.
    layerWasRemoved(layer_id: str)
        Emitted after a layer has been removed from the store.
    """

    layerWasAdded = Signal(str)
    layerWillBeRemoved = Signal(str)
    layerWasRemoved = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._layers: dict = {}

    # ------------------------------------------------------------------
    # Add
    # ------------------------------------------------------------------

    def addMapLayer(self, layer):
        """Register *layer* in the store.

        Returns the layer on success, or ``None`` if a layer with the same
        ID is already registered (duplicate).
        """
        if layer.id in self._layers:
            return None
        self._layers[layer.id] = layer
        self.layerWasAdded.emit(layer.id)
        return layer

    def addMapLayers(self, layers: list) -> list:
        """Register multiple layers.  Returns only the layers that were
        actually added (duplicates are skipped).
        """
        added = []
        for layer in layers:
            result = self.addMapLayer(layer)
            if result is not None:
                added.append(result)
        return added

    # ------------------------------------------------------------------
    # Remove
    # ------------------------------------------------------------------

    def removeMapLayer(self, layer_id: str) -> bool:
        """Remove a layer by ID.  Returns True if the layer was found and removed."""
        if layer_id not in self._layers:
            return False
        self.layerWillBeRemoved.emit(layer_id)
        del self._layers[layer_id]
        self.layerWasRemoved.emit(layer_id)
        return True

    def removeMapLayers(self, layer_ids: list) -> list:
        """Remove multiple layers by ID.  Returns the removed layer objects."""
        removed = []
        for lid in layer_ids:
            if lid in self._layers:
                self.layerWillBeRemoved.emit(lid)
                removed.append(self._layers.pop(lid))
                self.layerWasRemoved.emit(lid)
        return removed

    # ------------------------------------------------------------------
    # Query
    # ------------------------------------------------------------------

    def mapLayer(self, layer_id: str):
        """Return the layer with the given ID, or None if not found."""
        return self._layers.get(layer_id)

    def mapLayers(self) -> dict:
        """Return a dict of all registered layers keyed by ID."""
        return self._layers

    def count(self) -> int:
        """Return the number of registered layers."""
        return len(self._layers)

    def isLayerRegistered(self, layer_id: str) -> bool:
        """Return True if a layer with the given ID is registered."""
        return layer_id in self._layers

    def clear(self):
        """Remove all layers from the store, emitting removal signals."""
        for lid in list(self._layers.keys()):
            self.removeMapLayer(lid)
