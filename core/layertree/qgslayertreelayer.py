from core.layertree.qgslayertreenode import QgsLayerTreeNode

class QgsLayerTreeLayer(QgsLayerTreeNode):
    """
    Leaf Node wrapping a single MapLayer inside the layer tree hierarchy.
    Corresponds to QgsLayerTreeLayer in QGIS.
    """
    def __init__(self, layer_id: str, name: str = None):
        from core.qgsproject import QgsProject
        proj = QgsProject.instance()
        layer = proj.mapLayer(layer_id)
        layer_name = name or (layer.name if layer else layer_id)
        
        super().__init__(layer_name, "layer")
        self.layer_id = layer_id
        
    @property
    def name(self) -> str:
        """Dynamically retrieves the layer name from QgsProject if active."""
        l = self.layer()
        return l.name if l else self._name
        
    @name.setter
    def name(self, value: str):
        self._name = value
        l = self.layer()
        if l:
            l.name = value
        
    def layer(self):
        """Retrieves the underlying MapLayer object from QgsProject."""
        from core.qgsproject import QgsProject
        return QgsProject.instance().mapLayer(self.layer_id)

LayerTreeLayer = QgsLayerTreeLayer
