import sys
from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
                             QTableWidget, QTableWidgetItem, QLineEdit, QComboBox, QWidget,
                             QGraphicsView, QGraphicsScene, QGraphicsRectItem, QGraphicsLineItem,
                             QGraphicsTextItem)
from PySide6.QtCore import Qt, QPointF, QRectF
from PySide6.QtGui import QColor, QFont, QPen, QBrush, QPainter

from gui.rs_widgets import section_header, RsSearchInput

# Matplotlib integration for Spectral Profile
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

class SpectralProfileDialog(QDialog):
    """Artboard 05: Spectral Profile tool with real curves plotted in Matplotlib."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Spectral Profile Tool (光谱剖面工具)")
        self.resize(780, 520)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)

        # 1. Header info panel
        layout.addWidget(section_header("Spectral Profile Curve Analysis (光谱采样与库比对分析)"))

        # Controls layout
        ctrl_layout = QHBoxLayout()
        ctrl_layout.addWidget(QLabel("Reference Library:"))
        self.lib_combo = QComboBox()
        self.lib_combo.addItems(["USGS Vegetation/Water standard", "Local Agricultural Samples"])
        ctrl_layout.addWidget(self.lib_combo)

        self.smooth_btn = QPushButton("Smooth Curve (Savitzky-Golay)")
        ctrl_layout.addWidget(self.smooth_btn)
        ctrl_layout.addStretch()
        layout.addLayout(ctrl_layout)
        
        # 2. Matplotlib spectral graph
        self.fig = Figure(figsize=(7, 4), dpi=100)
        self.fig.patch.set_facecolor('#ffffff')
        self.canvas = FigureCanvas(self.fig)
        self.canvas.setStyleSheet("border: 1px solid #d4d8de; border-radius: 4px;")
        
        self.ax = self.fig.add_subplot(111)
        self.ax.set_facecolor('#ffffff')
        self.ax.spines['top'].set_visible(False)
        self.ax.spines['right'].set_visible(False)
        self.ax.spines['left'].set_color('#8a92a0')
        self.ax.spines['bottom'].set_color('#8a92a0')
        self.ax.tick_params(colors='#5b6473', labelsize=8)
        self.ax.grid(True, linestyle='--', alpha=0.3, color='#cfd5dd')
        
        # Sample Spectral Signature Bands (Sentinel-2 bands)
        bands = ["Coastal(B1)", "Blue(B2)", "Green(B3)", "Red(B4)", "RedEdge(B5)", "RedEdge(B6)", "NIR(B8)", "SWIR(B11)"]
        x_indices = range(len(bands))
        
        # 1. Vegetation Curve (Green)
        veg_y = [0.08, 0.07, 0.14, 0.08, 0.28, 0.48, 0.55, 0.22]
        self.ax.plot(x_indices, veg_y, marker='o', linewidth=2.0, color='#2da44e', label='Healthy Crop (NDVI=0.82)')
        self.ax.fill_between(x_indices, veg_y, color='#2da44e', alpha=0.08)
        
        # 2. Water Curve (Blue)
        water_y = [0.18, 0.16, 0.12, 0.08, 0.04, 0.02, 0.01, 0.00]
        self.ax.plot(x_indices, water_y, marker='o', linewidth=2.0, color='#0969da', label='Open Water (NDWI=0.65)')
        self.ax.fill_between(x_indices, water_y, color='#0969da', alpha=0.08)
        
        # 3. Sandy Soil Curve (Brown)
        soil_y = [0.12, 0.15, 0.18, 0.24, 0.28, 0.32, 0.38, 0.46]
        self.ax.plot(x_indices, soil_y, marker='o', linewidth=2.0, color='#b07730', label='Sandy Soil (NDVI=0.15)')
        self.ax.fill_between(x_indices, soil_y, color='#b07730', alpha=0.08)
        
        self.ax.set_xticks(x_indices)
        self.ax.set_xticklabels(bands)
        self.ax.set_ylabel("Reflectance Value (反射率)", color='#2f3640', fontfamily='sans-serif')
        self.ax.set_xlabel("Sentinel-2 Spectral Bands", color='#2f3640', fontfamily='sans-serif')
        self.ax.legend(facecolor='#ffffff', edgecolor='#d4d8de', fontsize=9)
        
        layout.addWidget(self.canvas)
        
        # Close button box
        close_btn = QPushButton("Close")
        close_btn.setProperty("primary", "true")
        close_btn.clicked.connect(self.accept)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)


class ModelBuilderDialog(QDialog):
    """Artboard 03: Visual flowchart of a modular remote sensing model."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Model Builder (模型构建器)")
        self.resize(860, 560)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)

        # 1. Header info panel
        layout.addWidget(section_header("Visual Modeling & Analysis Workflow Editor (可视化建模工作流)"))

        # Graphics View for Nodes Flowchart
        self.scene = QGraphicsScene(self)
        self.view = QGraphicsView(self.scene)
        self.view.setRenderHint(QPainter.Antialiasing)
        layout.addWidget(self.view)

        # Build Nodes in the Scene
        self._populate_nodes()

        # Close button
        close_btn = QPushButton("Save & Close")
        close_btn.setProperty("primary", "true")
        close_btn.clicked.connect(self.accept)
        
        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
        
    def _populate_nodes(self):
        self.scene.clear()
        
        # Draw dotted grid lines
        grid_pen = QPen(QColor("#eef1f5"), 1, Qt.DashLine)
        for x in range(0, 1000, 40):
            self.scene.addLine(x, 0, x, 600, grid_pen)
        for y in range(0, 600, 40):
            self.scene.addLine(0, y, 1000, y, grid_pen)
            
        # Draw Nodes: (title, x, y, width, height, stroke_color, bg_color)
        nodes_data = [
            ("Sentinel-2 MSI\n[Multispectral Input]", 30, 150, 140, 65, "#1f6feb", "#e0efff"),
            ("NDVI Index\n(NIR-Red)/(NIR+Red)", 220, 90, 150, 65, "#bf8700", "#fff8e1"),
            ("MeanShift Filter\n[SpatialRadius=15]", 220, 210, 150, 65, "#8250df", "#f5eeff"),
            ("Random Forest Classifier\n[Trees=100]", 430, 150, 160, 65, "#1a7f37", "#e6f4ea"),
            ("Crop Highlight\n[Filter Layer Map]", 650, 150, 140, 65, "#cf222e", "#ffeef0")
        ]
        
        node_items = {}
        for i, (title, x, y, w, h, s_col, bg_col) in enumerate(nodes_data):
            # Outer rect
            rect = QGraphicsRectItem(x, y, w, h)
            rect.setPen(QPen(QColor(s_col), 2))
            rect.setBrush(QBrush(QColor(bg_col)))
            self.scene.addItem(rect)
            
            # Label
            text = QGraphicsTextItem(title)
            text.setDefaultTextColor(QColor("#2f3640"))
            text.setFont(QFont("Segoe UI", 9, QFont.Bold))
            text.setPos(x + 5, y + 10)
            self.scene.addItem(text)
            
            node_items[i] = (x, y, w, h)
            
        # Connect nodes with beautiful arrows/lines
        def draw_connector(n1, n2):
            x1, y1, w1, h1 = node_items[n1]
            x2, y2, w2, h2 = node_items[n2]
            
            p_out = QPointF(x1 + w1, y1 + h1/2)
            p_in = QPointF(x2, y2 + h2/2)
            
            line_pen = QPen(QColor("#8a92a0"), 2)
            self.scene.addLine(p_out.x(), p_out.y(), p_in.x(), p_in.y(), line_pen)
            
            # Draw tiny arrow head
            arrow_pen = QPen(QColor("#8a92a0"), 2)
            self.scene.addLine(p_in.x(), p_in.y(), p_in.x() - 6, p_in.y() - 4, arrow_pen)
            self.scene.addLine(p_in.x(), p_in.y(), p_in.x() - 6, p_in.y() + 4, arrow_pen)
            
        draw_connector(0, 1)
        draw_connector(0, 2)
        draw_connector(1, 3)
        draw_connector(2, 3)
        draw_connector(3, 4)


class GeorefDialog(QDialog):
    """Artboard 07: GCP ground control points correction spreadsheet view."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("GCP Georeferencer (几何校正 GCP)")
        self.resize(760, 480)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)

        # 1. Header info panel
        layout.addWidget(section_header("Ground Control Points (GCP) Georeferencing Table"))

        # 2. Table widget
        self.table = QTableWidget(self)
        self.table.setColumnCount(6)
        self.table.setRowCount(5)
        self.table.setHorizontalHeaderLabels([
            "GCP ID", "Source X (Pixel)", "Source Y (Pixel)", "Destination X (Map)", "Destination Y (Map)", "Residual (meters)"
        ])
        self.table.horizontalHeader().setStretchLastSection(True)
        
        # Sample GCP point data
        gcp_data = [
            ("1", "102.40", "256.10", "12945620.10", "4851240.20", "0.45m"),
            ("2", "894.20", "112.50", "12965640.40", "4871260.50", "0.62m"),
            ("3", "512.00", "512.00", "12955630.20", "4861250.30", "0.31m"),
            ("4", "115.60", "920.40", "12945650.50", "4841220.10", "0.85m"),
            ("5", "950.80", "880.10", "12965610.30", "4841210.40", "0.54m")
        ]
        
        for row_idx, row in enumerate(gcp_data):
            for col_idx, text in enumerate(row):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(row_idx, col_idx, item)
                
        layout.addWidget(self.table)
        
        # Residual display and action buttons
        bottom_layout = QHBoxLayout()
        
        resid_lbl = QLabel("Total RMS Error: 0.58 meters (Target threshold: < 1.0m - SUCCESS)")
        resid_lbl.setStyleSheet("color: #1a7f37; font-weight: bold; font-family: 'Segoe UI', 'Inter';")
        bottom_layout.addWidget(resid_lbl)
        bottom_layout.addStretch()
        
        add_btn = QPushButton("Add GCP Point")
        bottom_layout.addWidget(add_btn)

        apply_btn = QPushButton("Run Geometric Rectify")
        apply_btn.setProperty("primary", "true")
        apply_btn.clicked.connect(self.accept)
        bottom_layout.addWidget(apply_btn)
        
        layout.addLayout(bottom_layout)


class ROIEditorDialog(QDialog):
    """Artboard 08: ROI supervised classification training samples spreadsheet editor."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Training Samples ROI Editor (分类样本编辑器)")
        self.resize(760, 480)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)

        # 1. Header
        layout.addWidget(section_header("Classification Region of Interest (ROI) Samples Management"))

        # 2. Table widget
        self.table = QTableWidget(self)
        self.table.setColumnCount(6)
        self.table.setRowCount(4)
        self.table.setHorizontalHeaderLabels([
            "ROI ID", "Class Name (类型名称)", "Samples count", "Geometry Type", "Visual Color", "Jeffries-Matusita Separability"
        ])
        self.table.horizontalHeader().setStretchLastSection(True)
        
        roi_data = [
            ("1", "Dense Forest (林地)", "142 pixels", "Polygon", "#2da44e", "1.98 (Excellent)"),
            ("2", "Water Body (水体)", "85 pixels", "Polygon", "#0969da", "2.00 (Perfect)"),
            ("3", "Sandy Soil (沙地)", "64 pixels", "Polygon", "#b07730", "1.89 (Good)"),
            ("4", "Urban Built-up (城区)", "112 pixels", "Polygon", "#d29922", "1.94 (Excellent)")
        ]
        
        for row_idx, row in enumerate(roi_data):
            for col_idx, val in enumerate(row):
                if col_idx == 4: # Color block
                    color_lbl = QLabel()
                    color_lbl.setStyleSheet(f"background-color: {val}; margin: 6px; border: 1px solid #666; border-radius: 2px;")
                    self.table.setCellWidget(row_idx, col_idx, color_lbl)
                else:
                    item = QTableWidgetItem(val)
                    item.setTextAlignment(Qt.AlignCenter)
                    self.table.setItem(row_idx, col_idx, item)
                    
        layout.addWidget(self.table)
        
        # Action Buttons
        bottom_layout = QHBoxLayout()
        bottom_layout.addStretch()
        
        calc_btn = QPushButton("Calculate JM Matrix")
        bottom_layout.addWidget(calc_btn)

        apply_btn = QPushButton("Extract Samples & Classify")
        apply_btn.setProperty("primary", "true")
        apply_btn.clicked.connect(self.accept)
        bottom_layout.addWidget(apply_btn)
        
        layout.addLayout(bottom_layout)


class AttributeTableDialog(QDialog):
    """Artboard 09: Vector features attribute spreadsheet with expression filter."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Layer Attribute Table (矢量属性表)")
        self.resize(760, 480)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)

        # 1. Expression Filter top bar
        filter_layout = QHBoxLayout()
        filter_layout.addWidget(QLabel("Expression Filter:"))

        _search = RsSearchInput('e.g. "Area" > 5.0 AND "Confidence" > 0.90')
        # Expose .filter_edit pointing at the inner QLineEdit for backward-compat
        self.filter_edit = _search.line
        filter_layout.addWidget(_search, 1)

        filter_btn = QPushButton("Filter Features")
        filter_layout.addWidget(filter_btn)
        layout.addLayout(filter_layout)

        # 2. Table widget
        self.table = QTableWidget(self)
        self.table.setColumnCount(5)
        self.table.setRowCount(5)
        self.table.setHorizontalHeaderLabels([
            "FID", "FieldName (地物名称)", "LandCover (覆被分类)", "Area (Sq.Km)", "Confidence Score"
        ])
        self.table.horizontalHeader().setStretchLastSection(True)
        
        attr_data = [
            ("0", "River Delta (三角洲)", "Water Body", "12.45", "98.5%"),
            ("1", "Agricultural Field A", "Vegetation", "8.24", "94.2%"),
            ("2", "Agricultural Field B", "Vegetation", "6.12", "92.8%"),
            ("3", "New Industrial Zone", "Urban Built-up", "15.68", "96.4%"),
            ("4", "Sandbar Area", "Sandy Soil", "3.14", "89.7%")
        ]
        
        for row_idx, row in enumerate(attr_data):
            for col_idx, val in enumerate(row):
                item = QTableWidgetItem(val)
                item.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(row_idx, col_idx, item)
                
        layout.addWidget(self.table)
        
        # Close button box
        close_btn = QPushButton("Close Table")
        close_btn.setProperty("primary", "true")
        close_btn.clicked.connect(self.accept)
        
        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        btn_layout.addWidget(close_btn)
        layout.addLayout(btn_layout)
