from PySide6.QtCore import QObject

class QgsLayerTreeCanvasBridge(QObject):
    """
    Synchronization Bridge that automatically binds the hierarchical LayerTreeGroup structure
    to the MapCanvas rendering engine. Listens to tree-level changes and updates the canvas,
    matching QgsLayerTreeCanvasBridge in QGIS.
    """
    def __init__(self, root_node, canvas):
        super().__init__()
        self.root = root_node
        self.canvas = canvas
        
        # Track connected signal nodes to avoid duplicate connections
        self._connected_nodes = set()
        
        # Connect the entire tree structure recursively
        self._connect_group(self.root)
        self.synchronize()
        
    def _connect_group(self, group):
        if group in self._connected_nodes:
            return
        self._connected_nodes.add(group)
        
        group.childAdded.connect(self._handle_child_added)
        group.childRemoved.connect(self._handle_child_removed)
        group.visibilityChanged.connect(self._handle_visibility_changed)
        
        for child in group.children():
            if child.nodeType() == "group":
                self._connect_group(child)
            else:
                if child not in self._connected_nodes:
                    self._connected_nodes.add(child)
                    child.visibilityChanged.connect(self._handle_visibility_changed)
                    
    def _disconnect_node(self, node):
        if node in self._connected_nodes:
            self._connected_nodes.remove(node)
            try:
                node.visibilityChanged.disconnect(self._handle_visibility_changed)
            except Exception:
                pass
                
        if node.nodeType() == "group":
            try:
                node.childAdded.disconnect(self._handle_child_added)
                node.childRemoved.disconnect(self._handle_child_removed)
            except Exception:
                pass
            for child in list(node.children()):
                self._disconnect_node(child)
                
    def _handle_child_added(self, child):
        if child.nodeType() == "group":
            self._connect_group(child)
        else:
            if child not in self._connected_nodes:
                self._connected_nodes.add(child)
                child.visibilityChanged.connect(self._handle_visibility_changed)
        self.synchronize()
        
    def _handle_child_removed(self, child):
        self._disconnect_node(child)
        self.synchronize()
        
    def _handle_visibility_changed(self, visible):
        self.synchronize()
        
    def synchronize(self):
        """Re-traverses the tree to rebuild the flat visible drawing order of layers on MapCanvas."""
        layers = self._collect_layers(self.root, parent_visible=True)
        # Reverse list: top layers in QGIS tree are rendered last (on top) in MapCanvas draw loop
        layers.reverse()
        self.canvas.setLayers(layers)
        self.canvas.refresh()
        
    def _collect_layers(self, node, parent_visible=True) -> list:
        flat = []
        node_visible = parent_visible and node.visible
        if node.nodeType() == "layer":
            layer = node.layer()
            if layer:
                # Synchronize visibility state onto MapLayer object
                layer.visible = node_visible
                flat.append(layer)
        elif node.nodeType() == "group":
            for child in node.children():
                flat.extend(self._collect_layers(child, node_visible))
        return flat

# Aliases for backward-compatibility
LayerTreeCanvasBridge = QgsLayerTreeCanvasBridge
