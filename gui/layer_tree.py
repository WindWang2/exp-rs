from PySide6.QtWidgets import QTreeView, QMenu, QMessageBox
from PySide6.QtCore import Qt, Signal, QModelIndex
from PySide6.QtGui import QStandardItemModel, QStandardItem, QIcon, QAction
import os

class LayerTreeItem(QStandardItem):
    """Represents a single layer or group node in the Layer Tree."""
    def __init__(self, layer_id: str, name: str, layer_type: str, file_path: str):
        super().__init__(name)
        self.layer_id = layer_id
        self.layer_type = layer_type
        self.file_path = file_path
        
        # Setup item parameters
        self.setCheckable(True)
        self.setCheckState(Qt.Checked)
        self.setEditable(False)
        self.setDragEnabled(True)
        self.setDropEnabled(False) # Individual layers do not accept child items
        
        # Store metadata in custom Qt User Roles
        self.setData(layer_id, Qt.UserRole)
        self.setData(layer_type, Qt.UserRole + 1)
        self.setData(file_path, Qt.UserRole + 2)

        # RsRowDelegate roles
        from gui.rs_widgets import ROLE_SWATCH, ROLE_META, ROLE_ICON
        palette = {"raster": "#7dd35c", "vector": "#4fb6e6"}
        self.setData(palette.get(layer_type, "#8a92a0"), ROLE_SWATCH)
        self.setData(layer_type, ROLE_META)
        self.setData("raster" if layer_type == "raster" else "vector", ROLE_ICON)

        # Set system-fallback icons for layers
        if layer_type == "raster":
            self.setIcon(QIcon.fromTheme("image-x-generic"))
        else:
            self.setIcon(QIcon.fromTheme("vector-polygon"))

class LayerTreeModel(QStandardItemModel):
    """
    Simplified PySide6 implementation of QgsLayerTreeModel.
    Manages the structural hierarchy, drawing order, and checkboxes.
    """
    visibility_changed = Signal(str, bool) # Emits (layer_id, visible)
    layers_reordered = Signal(list) # Emits list of active layer IDs in drawing order
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setHorizontalHeaderLabels(["Layers"])
        self.itemChanged.connect(self._handle_item_changed)
        
    def _handle_item_changed(self, item):
        """Monitors layer visibility checkboxes."""
        if isinstance(item, LayerTreeItem):
            visible = (item.checkState() == Qt.Checked)
            self.visibility_changed.emit(item.layer_id, visible)

    def add_layer_item(self, layer_id: str, name: str, layer_type: str, file_path: str) -> LayerTreeItem:
        """Adds a layer item at the top of the tree (newest drawn on top)."""
        item = LayerTreeItem(layer_id, name, layer_type, file_path)
        self.insertRow(0, item)
        self._emit_reorder()
        return item

    def remove_layer_item(self, layer_id: str):
        """Finds and removes a layer from the tree."""
        for row in range(self.rowCount()):
            item = self.item(row)
            if isinstance(item, LayerTreeItem) and item.layer_id == layer_id:
                self.removeRow(row)
                self._emit_reorder()
                break

    def get_layer_order(self) -> list:
        """Retrieves list of active layer IDs in drawing order (top to bottom)."""
        order = []
        for row in range(self.rowCount()):
            item = self.item(row)
            if isinstance(item, LayerTreeItem):
                order.append(item.layer_id)
        return order

    def _emit_reorder(self):
        self.layers_reordered.emit(self.get_layer_order())

    # Override internal mime drop to trigger canvas redraws on reorder
    def dropMimeData(self, data, action, row, column, parent):
        success = super().dropMimeData(data, action, row, column, parent)
        if success:
            self._emit_reorder()
        return success

class LayerTreeView(QTreeView):
    """
    Simplified PySide6 implementation of QgsLayerTreeView.
    Provides context menus, re-ordering actions, and stylesheet overrides.
    """
    zoom_to_layer_requested = Signal(str)
    remove_layer_requested = Signal(str)
    properties_requested = Signal(str) # Emits layer_id
    
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
            
        item = self.model().itemFromIndex(index)
        if not isinstance(item, LayerTreeItem):
            return
            
        menu = QMenu(self)
        
        zoom_act = QAction("Zoom to Layer", self)
        zoom_act.triggered.connect(lambda: self.zoom_to_layer_requested.emit(item.layer_id))
        menu.addAction(zoom_act)
        
        remove_act = QAction("Remove Layer", self)
        remove_act.triggered.connect(lambda: self.remove_layer_requested.emit(item.layer_id))
        menu.addAction(remove_act)
        
        properties_act = QAction("Properties...", self)
        properties_act.triggered.connect(lambda: self.properties_requested.emit(item.layer_id))
        menu.addAction(properties_act)
        
        menu.exec_(event.globalPos())
