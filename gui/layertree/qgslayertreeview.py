from PySide6.QtWidgets import QTreeView, QMenu, QMessageBox
from PySide6.QtCore import Qt, Signal, QModelIndex
from core.qgsproject import QgsProject, GISProject
from core.layertree import QgsLayerTreeNode, QgsLayerTreeGroup, QgsLayerTreeLayer

class QgsLayerTreeView(QTreeView):
    """
    Hierarchical Tree View matching QgsLayerTreeView in QGIS.
    Displays LayerTreeModel and manages context menus, dynamic deletion,
    and hierarchical drag-and-drop layer reorder operations.
    """
    properties_requested = Signal(str) # Emits layer_id
    remove_layer_requested = Signal(str) # Emits layer_id
    zoom_to_layer_requested = Signal(str) # Emits layer_id
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setHeaderHidden(True)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(True)
        self.setDragDropMode(QTreeView.InternalMove)
        
        # Premium light engineering styling with custom checkmark vector graphics
        self.setStyleSheet("""
            QTreeView {
                background-color: #ffffff;
                border: 1px solid #d4d8de;
                border-radius: 4px;
                color: #2f3640;
                font-family: 'Segoe UI', 'Inter', sans-serif;
                font-size: 12px;
                show-decoration-selected: 1;
            }
            QTreeView::item {
                padding: 5px;
                border-radius: 2px;
            }
            QTreeView::item:hover {
                background-color: #eef1f5;
            }
            QTreeView::item:selected {
                background-color: #e0efff;
                color: #1f6feb;
            }
            QTreeView::indicator {
                width: 14px;
                height: 14px;
                border-radius: 2px;
                border: 1px solid #b3bac4;
            }
            QTreeView::indicator:checked {
                background-color: #1f6feb;
                border: 1px solid #1f6feb;
                image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSJ3aGl0ZSIgc3Ryb2tlLXdpZHRoPSIzIiBzdHJva2UtbGluZWNhcD0icm91bmQiIHN0cm9rZS1saW5lam9pbj0icm91bmQiPjxwb2x5bGluZSBwb2ludHM9IjIwIDYgOSAxNyA0IDEyIi8+PC9zdmc+);
            }
            QTreeView::indicator:unchecked {
                background-color: #ffffff;
            }
        """)
        
    def contextMenuEvent(self, event):
        """Spawns dynamic context menus on item right-click."""
        index = self.indexAt(event.pos())
        if not index.isValid():
            return
            
        node = index.internalPointer()
        if not node:
            return
            
        menu = QMenu(self)
        
        if node.nodeType() == "layer":
            zoom_act = menu.addAction("Zoom to Layer")
            zoom_act.triggered.connect(lambda: self.zoom_to_layer_requested.emit(node.layer_id))
            
            remove_act = menu.addAction("Remove Layer")
            remove_act.triggered.connect(lambda: self.remove_layer_requested.emit(node.layer_id))
            
            properties_act = menu.addAction("Properties...")
            properties_act.triggered.connect(lambda: self.properties_requested.emit(node.layer_id))
            
        elif node.nodeType() == "group":
            remove_gp_act = menu.addAction("Remove Group")
            remove_gp_act.triggered.connect(lambda: self._remove_group_node(node))
            
        menu.exec_(event.globalPos())
        
    def _remove_group_node(self, node: QgsLayerTreeGroup):
        """Recursively removes group node and all its children layers."""
        # Flat collect child layer IDs inside group
        def collect_layer_ids(gp):
            lids = []
            for child in gp.children():
                if child.nodeType() == "layer":
                    lids.append(child.layer_id)
                elif child.nodeType() == "group":
                    lids.extend(collect_layer_ids(child))
            return lids
            
        layer_ids = collect_layer_ids(node)
        
        # Remove from project registry
        QgsProject.instance().removeMapLayers(layer_ids)
        
        # Remove from parent tree group node
        parent = node.parent()
        if not parent:
            parent = QgsProject.instance().layerTreeRoot()
        parent.removeChildNode(node)
        self.model().layoutChanged.emit()

    # Drag and Drop internal row moves reordering support
    def dragMoveEvent(self, event):
        super().dragMoveEvent(event)
        
    def dropEvent(self, event):
        """
        Custom drag-and-drop layer reorder engine.
        Updates the underlying LayerTreeNode hierarchies and signals the Model/Canvas.
        """
        # Find index under drop cursor
        index = self.indexAt(event.pos())
        parent_node = QgsProject.instance().layerTreeRoot()
        insert_row = len(parent_node.children())
        
        if index.isValid():
            target_node = index.internalPointer()
            if target_node.nodeType() == "group":
                parent_node = target_node
                insert_row = len(parent_node.children())
            else:
                parent_node = target_node.parent()
                if not parent_node:
                    parent_node = QgsProject.instance().layerTreeRoot()
                try:
                    insert_row = parent_node.children().index(target_node)
                except ValueError:
                    pass
                    
        # Get selected indexes being dragged
        selected = self.selectedIndexes()
        if not selected:
            return
            
        src_index = selected[0]
        src_node = src_index.internalPointer()
        if not src_node:
            return
            
        # Prevent dragging a group into itself or its descendants
        if src_node == parent_node:
            event.ignore()
            return
        
        temp = parent_node
        while temp is not None:
            if temp == src_node:
                event.ignore()
                return
            temp = temp.parent()
            
        # Move the node in tree structure
        src_parent = src_node.parent()
        if not src_parent:
            src_parent = QgsProject.instance().layerTreeRoot()
            
        src_parent.removeChildNode(src_node)
        
        # Adjust insertion index if node was removed from the same parent group before insertion point
        if src_parent == parent_node:
            try:
                # If source row was before insert_row, shift index down by 1
                src_row = src_index.row()
                if src_row < insert_row:
                    insert_row = max(0, insert_row - 1)
            except Exception:
                pass
                
        parent_node.insertChildNode(insert_row, src_node)
        self.model().layoutChanged.emit()
        event.accept()


LayerTreeView = QgsLayerTreeView
