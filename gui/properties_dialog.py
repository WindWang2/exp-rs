from PySide6.QtWidgets import (QDialog, QTabWidget, QWidget, QVBoxLayout, 
                                QHBoxLayout, QFormLayout, QLabel, QLineEdit, 
                                QComboBox, QSlider, QSpinBox, QPushButton, 
                                QColorDialog, QGroupBox, QDialogButtonBox, QMessageBox)
from PySide6.QtCore import Qt, Slot
from PySide6.QtGui import QColor, QIcon

class LayerPropertiesDialog(QDialog):
    """
    A premium layer properties dialog for Antigravity RS.
    Allows viewing/changing layer metadata, CRS details, and custom symbology.
    """
    def __init__(self, layer, parent=None):
        super().__init__(parent)
        self.layer = layer
        self.layer_type = "raster" if hasattr(layer, "provider") and hasattr(layer.provider, "reader") and layer.provider.reader.is_raster else "vector"
        
        self.setWindowTitle(f"Layer Properties - {layer.name}")
        self.resize(500, 480)
        
        # 1. Main Layout
        self.main_layout = QVBoxLayout(self)
        self.main_layout.setContentsMargins(15, 15, 15, 15)
        self.main_layout.setSpacing(12)
        
        # 2. Tab Widget
        self.tabs = QTabWidget(self)
        self.tabs.setStyleSheet("""
            QTabWidget::pane {
                border: 1px solid #d1d1d1;
                border-radius: 4px;
                background-color: #fcfcfc;
            }
            QTabBar::tab {
                background: #e1e1e1;
                border: 1px solid #c8c8c8;
                border-bottom-color: none;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
                padding: 6px 12px;
                margin-right: 2px;
                color: #333333;
                font-family: 'Segoe UI', 'Inter', sans-serif;
                font-size: 12px;
            }
            QTabBar::tab:selected, QTabBar::tab:hover {
                background: #fcfcfc;
                border-color: #d1d1d1;
                color: #007ac2;
                font-weight: bold;
            }
        """)
        
        # 3. Create Tabs
        self.info_tab = self._create_info_tab()
        self.style_tab = self._create_style_tab()
        
        self.tabs.addTab(self.style_tab, "Symbology")
        self.tabs.addTab(self.info_tab, "Information")
        
        self.main_layout.addWidget(self.tabs)
        
        # 4. Buttons (Ok, Cancel, Apply)
        self.button_box = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self)
        self.button_box.accepted.connect(self.accept)
        self.button_box.rejected.connect(self.reject)
        
        # Style buttons premium
        self.button_box.setStyleSheet("""
            QPushButton {
                background-color: #f1f3f5;
                border: 1px solid #c8c8c8;
                border-radius: 4px;
                padding: 6px 16px;
                font-family: 'Segoe UI', 'Inter';
                font-size: 12px;
                color: #333333;
            }
            QPushButton:hover {
                background-color: #e2e6ea;
                border-color: #007ac2;
            }
            QPushButton:pressed {
                background-color: #dae0e5;
            }
            QPushButton[text="OK"] {
                background-color: #007ac2;
                color: white;
                border: 1px solid #007ac2;
            }
            QPushButton[text="OK"]:hover {
                background-color: #006099;
                border-color: #006099;
            }
        """)
        
        self.main_layout.addWidget(self.button_box)
        
    def _create_info_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        # Group General
        general_group = QGroupBox("General Metadata")
        general_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #007ac2; }")
        g_layout = QFormLayout(general_group)
        g_layout.setContentsMargins(10, 15, 10, 10)
        g_layout.setSpacing(8)
        
        self.name_edit = QLineEdit(self.layer.name)
        self.name_edit.setStyleSheet("QLineEdit { border: 1px solid #c8c8c8; border-radius: 3px; padding: 4px; color: #333333; }")
        g_layout.addRow("Layer Name:", self.name_edit)
        
        path_label = QLabel(self.layer.provider.reader.file_path if self.layer_type == "raster" else self.layer.provider.reader.file_path)
        path_label.setWordWrap(True)
        path_label.setStyleSheet("color: #555555;")
        g_layout.addRow("Source Path:", path_label)
        
        type_label = QLabel(self.layer_type.upper())
        type_label.setStyleSheet("font-weight: bold; color: #555555;")
        g_layout.addRow("Layer Type:", type_label)
        
        layout.addWidget(general_group)
        
        # Group CRS
        crs_group = QGroupBox("Coordinate Reference System (CRS)")
        crs_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #007ac2; }")
        c_layout = QFormLayout(crs_group)
        c_layout.setContentsMargins(10, 15, 10, 10)
        c_layout.setSpacing(8)
        
        crs_str = self.layer.crs if self.layer.crs else "None (Local/Unknown Coordinates)"
        crs_label = QLabel(crs_str)
        crs_label.setWordWrap(True)
        crs_label.setStyleSheet("font-family: Consolas, monospace; color: #333333; font-size: 11px;")
        c_layout.addRow("Native Projection:", crs_label)
        
        canvas_crs_label = QLabel("EPSG:3857 (Web Mercator Standard for Display)")
        canvas_crs_label.setStyleSheet("color: #555555;")
        c_layout.addRow("Canvas Target CRS:", canvas_crs_label)
        
        layout.addWidget(crs_group)
        
        # Group Advanced Stats
        stats_group = QGroupBox("Spatial/Band Dimensions")
        stats_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #007ac2; }")
        s_layout = QFormLayout(stats_group)
        s_layout.setContentsMargins(10, 15, 10, 10)
        s_layout.setSpacing(8)
        
        meta = self.layer.provider.reader.metadata
        if self.layer_type == "raster":
            s_layout.addRow("Resolution (Width x Height):", QLabel(f"{meta.get('width')} x {meta.get('height')} pixels"))
            s_layout.addRow("Total Bands Count:", QLabel(str(meta.get('count', 1))))
            s_layout.addRow("Data Types:", QLabel(", ".join(meta.get('dtypes', []))))
        else:
            s_layout.addRow("Total Features Count:", QLabel(str(meta.get('count', 0))))
            s_layout.addRow("Driver Format:", QLabel(meta.get('driver', 'Unknown')))
            
        layout.addWidget(stats_group)
        layout.addStretch()
        return widget
        
    def _create_style_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)
        
        # Global Opacity Slider
        opacity_group = QGroupBox("Layer Opacity (透明度)")
        opacity_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 12px; color: #444; }")
        op_layout = QHBoxLayout(opacity_group)
        op_layout.setContentsMargins(10, 12, 10, 10)
        
        self.opacity_slider = QSlider(Qt.Horizontal)
        self.opacity_slider.setRange(0, 100)
        self.opacity_slider.setValue(int(self.layer.opacity * 100))
        
        self.opacity_label = QLabel(f"{int(self.layer.opacity * 100)}%")
        self.opacity_label.setFixedWidth(40)
        self.opacity_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.opacity_slider.valueChanged.connect(lambda v: self.opacity_label.setText(f"{v}%"))
        
        op_layout.addWidget(self.opacity_slider)
        op_layout.addWidget(self.opacity_label)
        layout.addWidget(opacity_group)
        
        if self.layer_type == "raster":
            self._setup_raster_style_widgets(layout)
        else:
            self._setup_vector_style_widgets(layout)
            
        layout.addStretch()
        return widget
        
    def _setup_raster_style_widgets(self, layout):
        symbology_group = QGroupBox("Raster Symbology (栅格渲染模式)")
        symbology_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #007ac2; }")
        form = QFormLayout(symbology_group)
        form.setContentsMargins(10, 15, 10, 10)
        form.setSpacing(10)
        
        # 1. Render Type Combo
        self.render_type_combo = QComboBox()
        self.render_type_combo.addItems(["Multiband Color", "Singleband Gray", "Singleband Pseudocolor"])
        self.render_type_combo.setStyleSheet("QComboBox { border: 1px solid #c8c8c8; border-radius: 3px; padding: 4px; }")
        
        # Set initial value
        if self.layer.render_type == "multiband":
            self.render_type_combo.setCurrentIndex(0)
        elif self.layer.render_type == "grayscale":
            self.render_type_combo.setCurrentIndex(1)
        else:
            self.render_type_combo.setCurrentIndex(2)
            
        form.addRow("Render Type:", self.render_type_combo)
        
        # Setup band options
        band_count = self.layer.provider.reader.metadata.get("count", 1)
        band_list = [f"Band {i}" for i in range(1, band_count + 1)]
        
        # 2. RGB Bands Config Widget
        self.rgb_widget = QWidget()
        rgb_layout = QFormLayout(self.rgb_widget)
        rgb_layout.setContentsMargins(0, 0, 0, 0)
        rgb_layout.setSpacing(6)
        
        self.red_combo = QComboBox()
        self.red_combo.addItems(band_list)
        self.red_combo.setCurrentIndex(min(self.layer.red_band - 1, band_count - 1))
        
        self.green_combo = QComboBox()
        self.green_combo.addItems(band_list)
        self.green_combo.setCurrentIndex(min(self.layer.green_band - 1, band_count - 1))
        
        self.blue_combo = QComboBox()
        self.blue_combo.addItems(band_list)
        self.blue_combo.setCurrentIndex(min(self.layer.blue_band - 1, band_count - 1))
        
        rgb_layout.addRow("Red Band:", self.red_combo)
        rgb_layout.addRow("Green Band:", self.green_combo)
        rgb_layout.addRow("Blue Band:", self.blue_combo)
        
        form.addRow(self.rgb_widget)
        
        # 3. Grayscale Band Config Widget
        self.gray_widget = QWidget()
        gray_layout = QFormLayout(self.gray_widget)
        gray_layout.setContentsMargins(0, 0, 0, 0)
        
        self.gray_combo = QComboBox()
        self.gray_combo.addItems(band_list)
        self.gray_combo.setCurrentIndex(min(self.layer.gray_band - 1, band_count - 1))
        gray_layout.addRow("Gray Band:", self.gray_combo)
        
        form.addRow(self.gray_widget)
        
        # 4. Pseudocolor Band & Ramp Config Widget
        self.pseudo_widget = QWidget()
        p_layout = QFormLayout(self.pseudo_widget)
        p_layout.setContentsMargins(0, 0, 0, 0)
        p_layout.setSpacing(6)
        
        self.pseudo_combo = QComboBox()
        self.pseudo_combo.addItems(band_list)
        self.pseudo_combo.setCurrentIndex(min(self.layer.pseudocolor_band - 1, band_count - 1))
        
        self.ramp_combo = QComboBox()
        # Premium matplotlib colormaps
        self.ramp_combo.addItems(["viridis", "jet", "magma", "inferno", "plasma", "coolwarm", "YlGn", "Blues", "terrain"])
        self.ramp_combo.setCurrentText(self.layer.color_ramp)
        self.ramp_combo.setStyleSheet("QComboBox { text-transform: lowercase; }")
        
        p_layout.addRow("Target Band:", self.pseudo_combo)
        p_layout.addRow("Color Ramp:", self.ramp_combo)
        
        form.addRow(self.pseudo_widget)
        
        # 5. Min/Max Contrast Stretch Widget
        stretch_group = QGroupBox("Contrast Enhancement (拉伸值)")
        stretch_group.setStyleSheet("QGroupBox { font-family: 'Segoe UI'; font-size: 11px; color: #555; }")
        str_layout = QFormLayout(stretch_group)
        str_layout.setContentsMargins(10, 10, 10, 10)
        str_layout.setSpacing(6)
        
        self.min_edit = QLineEdit(str(self.layer.min_val) if self.layer.min_val is not None else "")
        self.min_edit.setPlaceholderText("Automatic Min")
        self.min_edit.setStyleSheet("QLineEdit { border: 1px solid #c8c8c8; border-radius: 3px; padding: 3px; }")
        
        self.max_edit = QLineEdit(str(self.layer.max_val) if self.layer.max_val is not None else "")
        self.max_edit.setPlaceholderText("Automatic Max")
        self.max_edit.setStyleSheet("QLineEdit { border: 1px solid #c8c8c8; border-radius: 3px; padding: 3px; }")
        
        str_layout.addRow("Minimum Value:", self.min_edit)
        str_layout.addRow("Maximum Value:", self.max_edit)
        form.addRow(str_layout)
        
        layout.addWidget(symbology_group)
        
        # Connect render type changing to dynamically toggle config widgets visibility
        self.render_type_combo.currentIndexChanged.connect(self._toggle_raster_widgets)
        self._toggle_raster_widgets()
        
    def _toggle_raster_widgets(self):
        index = self.render_type_combo.currentIndex()
        self.rgb_widget.setVisible(index == 0)
        self.gray_widget.setVisible(index == 1)
        self.pseudo_widget.setVisible(index == 2)
        
    def _setup_vector_style_widgets(self, layout):
        symbology_group = QGroupBox("Vector Symbology (矢量符号化)")
        symbology_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #007ac2; }")
        form = QFormLayout(symbology_group)
        form.setContentsMargins(10, 15, 10, 10)
        form.setSpacing(10)
        
        renderer = self.layer.renderer
        
        # 1. Fill Color Button
        self.fill_color = renderer.color()
        self.fill_btn = QPushButton()
        self._update_btn_color(self.fill_btn, self.fill_color)
        self.fill_btn.clicked.connect(self._pick_fill_color)
        form.addRow("Fill Color:", self.fill_btn)
        
        # 2. Outline Color Button
        self.stroke_color = renderer.stroke_color()
        self.stroke_btn = QPushButton()
        self._update_btn_color(self.stroke_btn, self.stroke_color)
        self.stroke_btn.clicked.connect(self._pick_stroke_color)
        form.addRow("Outline Color:", self.stroke_btn)
        
        # 3. Outline Width SpinBox
        self.stroke_width_spin = QSpinBox()
        self.stroke_width_spin.setRange(1, 10)
        self.stroke_width_spin.setValue(renderer.stroke_width())
        self.stroke_width_spin.setStyleSheet("QSpinBox { border: 1px solid #c8c8c8; border-radius: 3px; padding: 4px; width: 60px; }")
        form.addRow("Outline Width:", self.stroke_width_spin)
        
        layout.addWidget(symbology_group)
        
    def _update_btn_color(self, btn, color):
        btn.setStyleSheet(f"background-color: {color.name()}; border: 1px solid #999999; border-radius: 4px; height: 22px;")
        
    def _pick_fill_color(self):
        c = QColorDialog.getColor(self.fill_color, self, "Select Fill Color", QColorDialog.ShowAlphaChannel)
        if c.isValid():
            self.fill_color = c
            self._update_btn_color(self.fill_btn, c)
            
    def _pick_stroke_color(self):
        c = QColorDialog.getColor(self.stroke_color, self, "Select Outline Color")
        if c.isValid():
            self.stroke_color = c
            self._update_btn_color(self.stroke_btn, c)

    def accept(self):
        # 1. Save Layer Name
        new_name = self.name_edit.text().strip()
        if new_name:
            self.layer.name = new_name
            
        # 2. Save Opacity
        self.layer.opacity = self.opacity_slider.value() / 100.0
        
        # 3. Save Symbology Configuration
        if self.layer_type == "raster":
            idx = self.render_type_combo.currentIndex()
            if idx == 0:
                self.layer.render_type = "multiband"
                self.layer.red_band = self.red_combo.currentIndex() + 1
                self.layer.green_band = self.green_combo.currentIndex() + 1
                self.layer.blue_band = self.blue_combo.currentIndex() + 1
            elif idx == 1:
                self.layer.render_type = "grayscale"
                self.layer.gray_band = self.gray_combo.currentIndex() + 1
            else:
                self.layer.render_type = "pseudocolor"
                self.layer.pseudocolor_band = self.pseudo_combo.currentIndex() + 1
                self.layer.color_ramp = self.ramp_combo.currentText()
                
            # Parse min/max stretch values
            min_str = self.min_edit.text().strip()
            max_str = self.max_edit.text().strip()
            
            try:
                self.layer.min_val = float(min_str) if min_str else None
                self.layer.max_val = float(max_str) if max_str else None
            except ValueError:
                QMessageBox.warning(self, "Invalid Values", "Min/Max stretch values must be numeric.")
                return
        else:
            # Vector styling
            self.layer.renderer.set_color(self.fill_color)
            self.layer.renderer.set_stroke_color(self.stroke_color)
            self.layer.renderer.set_stroke_width(self.stroke_width_spin.value())
            
        super().accept()
