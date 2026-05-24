from PySide6.QtCore import Qt, QModelIndex, QAbstractItemModel, QPointF
from PySide6.QtGui import QIcon, QPixmap, QPainter, QColor, QPen, QBrush, QLinearGradient
from core.qgsproject import QgsProject, GISProject
from core.layertree import QgsLayerTreeNode, QgsLayerTreeGroup, QgsLayerTreeLayer

class QgsLayerTreeModel(QAbstractItemModel):
    """
    Hierarchical Tree Model wrapping QgsProject's layer tree structure.
    Subclasses QAbstractItemModel to dynamically query LayerTreeNode pointer indexes,
    fully matching QgsLayerTreeModel in QGIS.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.root_node = QgsProject.instance().layerTreeRoot()
        
        # Connect project/tree structural updates to view updates
        self.root_node.childAdded.connect(self._handle_structural_change)
        self.root_node.childRemoved.connect(self._handle_structural_change)
        
    def _handle_structural_change(self, node):
        self.layoutChanged.emit()

    def remove_layer_item(self, layer_id: str):
        """Helper to find and remove a layer node matching the layer_id from the root."""
        # Check recursively or top-level
        def search_and_remove(group):
            for child in list(group.children()):
                if child.nodeType() == "layer" and child.layer_id == layer_id:
                    group.removeChildNode(child)
                    return True
                elif child.nodeType() == "group":
                    if search_and_remove(child):
                        return True
            return False
        search_and_remove(self.root_node)
        self.layoutChanged.emit()

    def index(self, row: int, column: int, parent: QModelIndex = QModelIndex()) -> QModelIndex:
        if not self.hasIndex(row, column, parent):
            return QModelIndex()
            
        if not parent.isValid():
            parent_node = self.root_node
        else:
            parent_node = parent.internalPointer()
            
        if parent_node and row < len(parent_node.children()):
            child_node = parent_node.children()[row]
            return self.createIndex(row, column, child_node)
        return QModelIndex()

    def parent(self, index: QModelIndex) -> QModelIndex:
        if not index.isValid():
            return QModelIndex()
            
        node = index.internalPointer()
        if not node:
            return QModelIndex()
            
        p_node = node.parent()
        if p_node is None or p_node == self.root_node:
            return QModelIndex()
            
        # We need the row index of parent node under grandparent node
        gp_node = p_node.parent()
        if not gp_node:
            gp_node = self.root_node
            
        try:
            row = gp_node.children().index(p_node)
            return self.createIndex(row, 0, p_node)
        except ValueError:
            return QModelIndex()

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        if parent.column() > 0:
            return 0
            
        if not parent.isValid():
            parent_node = self.root_node
        else:
            parent_node = parent.internalPointer()
            
        if parent_node and parent_node.nodeType() == "group":
            return len(parent_node.children())
        return 0

    def columnCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 1

    def data(self, index: QModelIndex, role: int = Qt.DisplayRole):
        if not index.isValid():
            return None
            
        node = index.internalPointer()
        if not node:
            return None
            
        if role == Qt.DisplayRole:
            return node.name
        elif role == Qt.CheckStateRole:
            return Qt.Checked if node.visible else Qt.Unchecked
        elif role == Qt.DecorationRole:
            if node.nodeType() == "group":
                # Draw a sleek, modern yellow folder icon
                pixmap = QPixmap(16, 16)
                pixmap.fill(Qt.transparent)
                painter = QPainter(pixmap)
                painter.setRenderHint(QPainter.Antialiasing)
                painter.setBrush(QBrush(QColor(240, 190, 70)))
                painter.setPen(QPen(QColor(200, 150, 30), 1))
                painter.drawRect(1, 4, 14, 10)
                painter.drawPolygon([QPointF(1, 4), QPointF(5, 4), QPointF(7, 6), QPointF(1, 6)])
                painter.end()
                return QIcon(pixmap)
            else:
                layer = node.layer()
                if not layer:
                    return QIcon.fromTheme("image-x-generic")
                
                is_raster = hasattr(layer, "provider") and hasattr(layer.provider, "reader") and layer.provider.reader.is_raster
                
                pixmap = QPixmap(16, 16)
                pixmap.fill(Qt.transparent)
                painter = QPainter(pixmap)
                painter.setRenderHint(QPainter.Antialiasing)
                
                if is_raster:
                    # Draw a nice raster gradient matching selected rendering type
                    render_type = getattr(layer, "render_type", "grayscale")
                    if render_type == "grayscale":
                        grad = QLinearGradient(0, 0, 16, 16)
                        grad.setColorAt(0, QColor(50, 50, 50))
                        grad.setColorAt(1, QColor(220, 220, 220))
                        painter.fillRect(0, 0, 16, 16, grad)
                    elif render_type == "pseudocolor":
                        grad = QLinearGradient(0, 8, 16, 8)
                        import matplotlib
                        try:
                            cmap = matplotlib.colormaps[layer.color_ramp]
                            c0 = cmap(0.0)
                            c1 = cmap(0.5)
                            c2 = cmap(1.0)
                            grad.setColorAt(0, QColor(int(c0[0]*255), int(c0[1]*255), int(c0[2]*255)))
                            grad.setColorAt(0.5, QColor(int(c1[0]*255), int(c1[1]*255), int(c1[2]*255)))
                            grad.setColorAt(1.0, QColor(int(c2[0]*255), int(c2[1]*255), int(c2[2]*255)))
                        except Exception:
                            grad.setColorAt(0, QColor(68, 1, 84))
                            grad.setColorAt(0.5, QColor(33, 145, 140))
                            grad.setColorAt(1, QColor(253, 231, 37))
                        painter.fillRect(0, 0, 16, 16, grad)
                    else: # multiband
                        painter.fillRect(0, 0, 5, 16, QColor(220, 50, 50))
                        painter.fillRect(5, 0, 6, 16, QColor(50, 200, 50))
                        painter.fillRect(11, 0, 5, 16, QColor(50, 50, 220))
                    # Subtle border
                    painter.setBrush(Qt.NoBrush)
                    painter.setPen(QPen(QColor(180, 180, 180), 1))
                    painter.drawRect(0, 0, 15, 15)
                else:
                    # Vector swatch: fill and outline
                    fill_color = layer.renderer.color()
                    stroke_color = layer.renderer.stroke_color()
                    stroke_width = max(1, min(3, layer.renderer.stroke_width()))
                    
                    painter.setBrush(QBrush(fill_color))
                    pen = QPen(stroke_color)
                    pen.setWidth(stroke_width)
                    painter.setPen(pen)
                    painter.drawRect(2, 2, 12, 12)
                
                painter.end()
                return QIcon(pixmap)
        return None

    def setData(self, index: QModelIndex, value, role: int = Qt.EditRole) -> bool:
        if not index.isValid():
            return False
            
        node = index.internalPointer()
        if not node:
            return False
            
        if role == Qt.CheckStateRole:
            node.visible = (value == Qt.Checked)
            self.dataChanged.emit(index, index, [Qt.CheckStateRole])
            
            # Recursively emit dataChanged for group children to update checkstate visual checkboxes
            if node.nodeType() == "group":
                self._emit_children_changed(index, node)
            return True
        return False
        
    def _emit_children_changed(self, parent_index: QModelIndex, parent_node: QgsLayerTreeGroup):
        for row, child in enumerate(parent_node.children()):
            child_idx = self.index(row, 0, parent_index)
            self.dataChanged.emit(child_idx, child_idx, [Qt.CheckStateRole])
            if child.nodeType() == "group":
                self._emit_children_changed(child_idx, child)

    def flags(self, index: QModelIndex) -> Qt.ItemFlags:
        if not index.isValid():
            return Qt.ItemIsDropEnabled
            
        node = index.internalPointer()
        base_flags = Qt.ItemIsEnabled | Qt.ItemIsSelectable | Qt.ItemIsUserCheckable | Qt.ItemIsDragEnabled
        
        if node.nodeType() == "group":
            return base_flags | Qt.ItemIsDropEnabled
        return base_flags


LayerTreeModel = QgsLayerTreeModel
