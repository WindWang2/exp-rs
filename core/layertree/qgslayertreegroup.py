from PySide6.QtCore import Signal
from core.layertree.qgslayertreenode import QgsLayerTreeNode

class QgsLayerTreeGroup(QgsLayerTreeNode):
    """
    Folder / Structural Node Group holding child nodes (other groups or layers).
    Allows building hierarchical layer groupings like QgsLayerTreeGroup in QGIS.
    """
    childAdded = Signal(object)
    childRemoved = Signal(object)
    
    def __init__(self, name: str):
        super().__init__(name, "group")
        self._children = []
        
    def children(self) -> list:
        """Returns a copy of the children list."""
        return self._children
        
    def addChildNode(self, node: QgsLayerTreeNode):
        """Appends a child node to this group."""
        self.insertChildNode(len(self._children), node)
        
    def insertChildNode(self, index: int, node: QgsLayerTreeNode):
        """Inserts a child node at the specified index."""
        node.setParent(self)
        self._children.insert(index, node)
        self.childAdded.emit(node)
        
    def removeChildNode(self, node: QgsLayerTreeNode):
        """Removes a child node from this group."""
        if node in self._children:
            self._children.remove(node)
            node.setParent(None)
            self.childRemoved.emit(node)
            
    def addLayer(self, layer):
        """Helper to create and add a QgsLayerTreeLayer wrap node for a layer."""
        from core.layertree.qgslayertreelayer import QgsLayerTreeLayer
        child = QgsLayerTreeLayer(layer.id, layer.name)
        self.addChildNode(child)
        return child
        
    def clear(self):
        """Clears all child nodes."""
        for c in list(self._children):
            self.removeChildNode(c)

LayerTreeGroup = QgsLayerTreeGroup
