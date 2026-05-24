from PySide6.QtCore import QObject, Signal

class QgsLayerTreeNode(QObject):
    """
    Abstract Base Node for the GIS Layer Tree.
    Decouples structural organization from specific layer operations,
    mirroring the design of QgsLayerTreeNode in QGIS.
    """
    visibilityChanged = Signal(bool)
    
    def __init__(self, name: str, node_type: str):
        super().__init__()
        self._name = name
        self._node_type = node_type
        self._visible = True
        self._parent = None
        
    def nodeType(self) -> str:
        """Returns the type of node: 'group' or 'layer'."""
        return self._node_type
        
    def parent(self):
        """Returns parent group node."""
        return self._parent
        
    def setParent(self, parent_node):
        """Sets parent group node reference."""
        self._parent = parent_node
        
    @property
    def name(self) -> str:
        return self._name
        
    @name.setter
    def name(self, value: str):
        self._name = value
        
    @property
    def visible(self) -> bool:
        return self._visible
        
    @visible.setter
    def visible(self, state: bool):
        if self._visible != state:
            self._visible = state
            self.visibilityChanged.emit(state)

LayerTreeNode = QgsLayerTreeNode
