from PySide6.QtWidgets import QWidget, QVBoxLayout, QToolBar, QInputDialog, QMessageBox
from PySide6.QtCore import Qt, Signal, QSize, QPointF
from PySide6.QtGui import QIcon, QAction, QPixmap, QPainter, QColor, QPen, QBrush

from core.qgsproject import QgsProject
from core.layertree import QgsLayerTreeGroup, QgsLayerTreeLayer, QgsLayerTreeNode
from gui.layertree.qgslayertreeview import QgsLayerTreeView
from gui.layertree.qgslayertreemodel import QgsLayerTreeModel

class QgsLayerTreeWidget(QWidget):
    """
    Unified Layers Panel widget bundling the QgsLayerTreeView 
    with a premium, fully-functional management toolbar on top.
    """
    add_raster_requested = Signal()
    add_vector_requested = Signal()
    remove_layer_requested = Signal(str)
    zoom_to_layer_requested = Signal(str)
    properties_requested = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        
        # 1. Main layout
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        
        # 2. Toolbar
        self.toolbar = QToolBar("Layers Panel Toolbar", self)
        self.toolbar.setIconSize(QSize(16, 16))
        self.toolbar.setStyleSheet("""
            QToolBar {
                background-color: #f6f7f9;
                border: 1px solid #d4d8de;
                border-bottom: none;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
                padding: 2px;
                spacing: 4px;
            }
            QToolButton {
                background: transparent;
                border: 1px solid transparent;
                border-radius: 3px;
                padding: 4px;
            }
            QToolButton:hover {
                background-color: #eef1f5;
                border: 1px solid #1f6feb;
            }
            QToolButton:pressed {
                background-color: #dae0e5;
            }
        """)
        
        # Create premium custom SVGs/gradients for icons so they don't depend on missing system themes!
        # Icon 1: Add Group (Folder with Plus)
        self.addGroupAct = QAction(self._create_group_icon(), "Add Group (添加图层组)", self)
        self.addGroupAct.triggered.connect(self._add_group)
        self.toolbar.addAction(self.addGroupAct)
        
        # Icon 2: Expand All
        self.expandAllAct = QAction(self._create_expand_icon(), "Expand All (展开全部)", self)
        self.expandAllAct.triggered.connect(self._expand_all)
        self.toolbar.addAction(self.expandAllAct)
        
        # Icon 3: Collapse All
        self.collapseAllAct = QAction(self._create_collapse_icon(), "Collapse All (折叠全部)", self)
        self.collapseAllAct.triggered.connect(self._collapse_all)
        self.toolbar.addAction(self.collapseAllAct)
        
        # Icon 4: Remove Selected
        self.removeAct = QAction(self._create_remove_icon(), "Remove Layer/Group (移除选中项)", self)
        self.removeAct.triggered.connect(self._remove_selected)
        self.toolbar.addAction(self.removeAct)
        
        self.toolbar.addSeparator()
        
        # Icon 5: Add Raster
        self.addRasterAct = QAction(self._create_raster_icon(), "Add Raster Layer... (添加栅格图层)", self)
        self.addRasterAct.triggered.connect(self.add_raster_requested.emit)
        self.toolbar.addAction(self.addRasterAct)
        
        # Icon 6: Add Vector
        self.addVectorAct = QAction(self._create_vector_icon(), "Add Vector Layer... (添加矢量图层)", self)
        self.addVectorAct.triggered.connect(self.add_vector_requested.emit)
        self.toolbar.addAction(self.addVectorAct)
        
        layout.addWidget(self.toolbar)
        
        # 3. Tree View
        self.view = QgsLayerTreeView(self)
        self.model = QgsLayerTreeModel(self)
        self.view.setModel(self.model)
        
        # Forward signals from internal view to main widget
        self.view.properties_requested.connect(self.properties_requested.emit)
        self.view.remove_layer_requested.connect(self.remove_layer_requested.emit)
        self.view.zoom_to_layer_requested.connect(self.zoom_to_layer_requested.emit)
        
        layout.addWidget(self.view)
 
    def _create_group_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        # Yellow folder
        p.setBrush(QBrush(QColor(240, 190, 70)))
        p.setPen(QPen(QColor(200, 150, 30), 1))
        p.drawRect(1, 4, 10, 8)
        p.drawPolygon([QPointF(1, 4), QPointF(4, 4), QPointF(6, 6), QPointF(1, 6)])
        # Green Plus
        p.setPen(QPen(QColor(40, 160, 40), 2))
        p.drawLine(12, 10, 12, 14)
        p.drawLine(10, 12, 14, 12)
        p.end()
        return QIcon(pix)

    def _create_expand_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(QColor(0, 122, 194), 2))
        p.drawLine(3, 5, 8, 10)
        p.drawLine(8, 10, 13, 5)
        p.drawLine(3, 11, 13, 11)
        p.end()
        return QIcon(pix)

    def _create_collapse_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(QColor(0, 122, 194), 2))
        p.drawLine(3, 11, 8, 6)
        p.drawLine(8, 6, 13, 11)
        p.drawLine(3, 3, 13, 3)
        p.end()
        return QIcon(pix)

    def _create_remove_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        p.setBrush(QBrush(QColor(220, 50, 50)))
        p.setPen(QPen(QColor(160, 30, 30), 1))
        # Trash can shape
        p.drawRect(4, 5, 8, 9)
        p.drawRect(2, 3, 12, 2)
        p.drawRect(6, 1, 4, 2)
        p.end()
        return QIcon(pix)

    def _create_raster_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        p.setBrush(QBrush(QColor(50, 180, 180)))
        p.setPen(QPen(QColor(30, 120, 120), 1))
        # Grid shape
        p.drawRect(2, 2, 12, 12)
        p.drawLine(6, 2, 6, 14)
        p.drawLine(10, 2, 10, 14)
        p.drawLine(2, 6, 14, 6)
        p.drawLine(2, 10, 14, 10)
        p.end()
        return QIcon(pix)

    def _create_vector_icon(self) -> QIcon:
        pix = QPixmap(16, 16)
        pix.fill(Qt.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.Antialiasing)
        p.setBrush(QBrush(QColor(100, 150, 240)))
        p.setPen(QPen(QColor(50, 80, 180), 1.5))
        # Polygon shape
        p.drawPolygon([QPointF(2, 6), QPointF(8, 2), QPointF(14, 7), QPointF(11, 13), QPointF(4, 12)])
        p.end()
        return QIcon(pix)

    def _add_group(self):
        group_name, ok = QInputDialog.getText(self, "Add Group", "Group Name:")
        if ok and group_name.strip():
            root = QgsProject.instance().layerTreeRoot()
            group = QgsLayerTreeGroup(group_name.strip())
            
            selected_idx = self.view.currentIndex()
            if selected_idx.isValid():
                parent_node = selected_idx.internalPointer()
                if parent_node.nodeType() == "group":
                    parent_node.addChildNode(group)
                else:
                    gp = parent_node.parent()
                    if gp:
                        gp.addChildNode(group)
                    else:
                        root.addChildNode(group)
            else:
                root.addChildNode(group)
                
            self.model.layoutChanged.emit()
            self.view.expandAll()

    def _expand_all(self):
        self.view.expandAll()

    def _collapse_all(self):
        self.view.collapseAll()

    def _remove_selected(self):
        selected_idx = self.view.currentIndex()
        if selected_idx.isValid():
            node = selected_idx.internalPointer()
            if node.nodeType() == "layer":
                self.remove_layer_requested.emit(node.layer_id)
            elif node.nodeType() == "group":
                confirm = QMessageBox.question(
                    self, "Remove Group", 
                    f"Are you sure you want to remove the group '{node.name}' and all its layers?",
                    QMessageBox.Yes | QMessageBox.No
                )
                if confirm == QMessageBox.Yes:
                    self.view._remove_group_node(node)

# Alias for backward-compatibility
LayerTreeWidget = QgsLayerTreeWidget
