from PySide6.QtWidgets import (QDialog, QTabWidget, QWidget, QVBoxLayout, 
                                QHBoxLayout, QFormLayout, QLabel, QLineEdit, 
                                QComboBox, QSlider, QSpinBox, QDoubleSpinBox, 
                                QPushButton, QColorDialog, QGroupBox, QDialogButtonBox, 
                                QMessageBox, QRadioButton, QButtonGroup)
from PySide6.QtCore import Qt, Slot
from PySide6.QtGui import QColor, QIcon

class LayerPropertiesDialog(QDialog):
    """
    A premium layer properties dialog for Antigravity RS.
    Allows viewing/changing layer metadata, CRS details, and custom symbology.
    Aligned perfectly with QGIS rendering, band selections, and Min/Max settings.
    """
    def __init__(self, layer, parent=None):
        super().__init__(parent)
        self.layer = layer
        self.layer_type = "raster" if hasattr(layer, "provider") and hasattr(layer.provider, "reader") and layer.provider.reader.is_raster else "vector"
        
        self.setWindowTitle(f"Layer Properties - {layer.name}")
        self.resize(520, 560)
        
        # 1. Main Layout
        self.main_layout = QVBoxLayout(self)
        self.main_layout.setContentsMargins(15, 15, 15, 15)
        self.main_layout.setSpacing(12)
        
        # 2. Tab Widget — styled globally via QSS
        self.tabs = QTabWidget(self)
        
        # 3. Create Tabs
        self.info_tab = self._create_info_tab()
        self.style_tab = self._create_style_tab()
        
        self.tabs.addTab(self.style_tab, "Symbology")
        self.tabs.addTab(self.info_tab, "Information")
        
        self.main_layout.addWidget(self.tabs)
        
        # 4. Buttons (Ok, Cancel) — styled globally via QSS; accent the OK button
        self.button_box = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self)
        self.button_box.accepted.connect(self.accept)
        self.button_box.rejected.connect(self.reject)
        ok_btn = self.button_box.button(QDialogButtonBox.Ok)
        if ok_btn:
            ok_btn.setProperty("primary", "true")
        
        self.main_layout.addWidget(self.button_box)
        
    def _create_info_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(10)
        
        # Group General
        general_group = QGroupBox("General Metadata")
        general_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #1f6feb; }")
        g_layout = QFormLayout(general_group)
        g_layout.setContentsMargins(10, 15, 10, 10)
        g_layout.setSpacing(8)
        
        self.name_edit = QLineEdit(self.layer.name)
        self.name_edit.setStyleSheet("QLineEdit { border: 1px solid #d4d8de; border-radius: 3px; padding: 4px; color: #2f3640; }")
        g_layout.addRow("Layer Name:", self.name_edit)
        
        path_label = QLabel(self.layer.provider.reader.file_path)
        path_label.setWordWrap(True)
        path_label.setStyleSheet("color: #5b6473;")
        g_layout.addRow("Source Path:", path_label)
        
        type_label = QLabel(self.layer_type.upper())
        type_label.setStyleSheet("font-weight: bold; color: #5b6473;")
        g_layout.addRow("Layer Type:", type_label)
        
        layout.addWidget(general_group)
        
        # Group CRS
        crs_group = QGroupBox("Coordinate Reference System (CRS)")
        crs_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #1f6feb; }")
        c_layout = QFormLayout(crs_group)
        c_layout.setContentsMargins(10, 15, 10, 10)
        c_layout.setSpacing(8)
        
        crs_str = self.layer.crs if self.layer.crs else "None (Local/Unknown Coordinates)"
        crs_label = QLabel(crs_str)
        crs_label.setWordWrap(True)
        crs_label.setStyleSheet("font-family: Consolas, monospace; color: #2f3640; font-size: 11px;")
        c_layout.addRow("Native Projection:", crs_label)
        
        canvas_crs_label = QLabel("EPSG:3857 (Web Mercator Standard for Display)")
        canvas_crs_label.setStyleSheet("color: #5b6473;")
        c_layout.addRow("Canvas Target CRS:", canvas_crs_label)
        
        layout.addWidget(crs_group)
        
        # Group Advanced Stats
        stats_group = QGroupBox("Spatial/Band Dimensions")
        stats_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #1f6feb; }")
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
        opacity_group = QGroupBox("Layer Opacity (不透明度)")
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
        symbology_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #1f6feb; }")
        form = QFormLayout(symbology_group)
        form.setContentsMargins(10, 15, 10, 10)
        form.setSpacing(10)
        
        # 1. Render Type Combo
        self.render_type_combo = QComboBox()
        self.render_type_combo.addItems(["Multiband Color", "Singleband Gray", "Singleband Pseudocolor"])
        self.render_type_combo.setStyleSheet("QComboBox { border: 1px solid #d4d8de; border-radius: 3px; padding: 4px; }")
        
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
        self.ramp_combo.addItems(["viridis", "jet", "magma", "inferno", "plasma", "coolwarm", "YlGn", "Blues", "terrain"])
        self.ramp_combo.setCurrentText(self.layer.color_ramp)
        self.ramp_combo.setStyleSheet("QComboBox { text-transform: lowercase; }")
        
        p_layout.addRow("Target Band:", self.pseudo_combo)
        p_layout.addRow("Color Ramp:", self.ramp_combo)
        
        form.addRow(self.pseudo_widget)
        layout.addWidget(symbology_group)

        # 5. QGIS-Aligned Contrast Stretch Config Widget
        contrast_group = QGroupBox("Contrast Enhancement (对比度增强与拉伸)")
        contrast_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 12px; color: #2f3640; }")
        con_layout = QVBoxLayout(contrast_group)
        con_layout.setContentsMargins(10, 15, 10, 10)
        con_layout.setSpacing(10)

        # Contrast Algorithm Combo
        algo_row = QHBoxLayout()
        algo_row.addWidget(QLabel("Contrast Enhancement:"))
        self.contrast_combo = QComboBox()
        self.contrast_combo.addItems(["No Stretch", "Stretch to MinMax"])
        self.contrast_combo.setCurrentIndex(0 if self.layer.contrast_enhancement == "none" else 1)
        self.contrast_combo.setStyleSheet("QComboBox { border: 1px solid #d4d8de; border-radius: 3px; padding: 4px; }")
        algo_row.addWidget(self.contrast_combo)
        con_layout.addLayout(algo_row)

        # Min/Max Limit Settings Group Box
        self.min_max_group = QGroupBox("Min / Max Value Settings (最小/最大值计算范围)")
        self.min_max_group.setStyleSheet("QGroupBox { font-family: 'Segoe UI'; font-size: 11px; color: #5b6473; font-weight: normal; }")
        limits_layout = QVBoxLayout(self.min_max_group)
        limits_layout.setSpacing(8)

        # Radio Buttons
        self.radio_cumulative = QRadioButton("Cumulative pixel count cut (累积像素百分比裁剪)")
        self.radio_min_max = QRadioButton("Min / Max (绝对最小 / 最大值)")
        self.radio_std_dev = QRadioButton("Mean +/- standard deviation (平均值 +/- 标准差拉伸)")
        self.radio_user = QRadioButton("User Defined (用户自定数值限界)")

        # Create Button Group
        self.radio_group = QButtonGroup(self)
        self.radio_group.addButton(self.radio_cumulative)
        self.radio_group.addButton(self.radio_min_max)
        self.radio_group.addButton(self.radio_std_dev)
        self.radio_group.addButton(self.radio_user)

        # Set initial Radio check state
        method = self.layer.min_max_limits_method
        if method == "cumulative_cut":
            self.radio_cumulative.setChecked(True)
        elif method == "min_max":
            self.radio_min_max.setChecked(True)
        elif method == "std_dev":
            self.radio_std_dev.setChecked(True)
        else:
            self.radio_user.setChecked(True)

        # Sub-widgets for settings input
        # Cumulative cut limits inputs
        self.cumulative_widget = QWidget()
        cum_layout = QHBoxLayout(self.cumulative_widget)
        cum_layout.setContentsMargins(20, 0, 0, 0)
        cum_layout.addWidget(QLabel("Min Cut:"))
        self.spin_low_percent = QDoubleSpinBox()
        self.spin_low_percent.setRange(0.0, 50.0)
        self.spin_low_percent.setSuffix(" %")
        self.spin_low_percent.setValue(self.layer.cumulative_cut_lower)
        self.spin_low_percent.setFixedWidth(80)
        cum_layout.addWidget(self.spin_low_percent)

        cum_layout.addWidget(QLabel("Max Cut:"))
        self.spin_high_percent = QDoubleSpinBox()
        self.spin_high_percent.setRange(50.0, 100.0)
        self.spin_high_percent.setSuffix(" %")
        self.spin_high_percent.setValue(self.layer.cumulative_cut_upper)
        self.spin_high_percent.setFixedWidth(80)
        cum_layout.addWidget(self.spin_high_percent)
        cum_layout.addStretch()

        # Std Dev limits input
        self.std_dev_widget = QWidget()
        std_layout = QHBoxLayout(self.std_dev_widget)
        std_layout.setContentsMargins(20, 0, 0, 0)
        std_layout.addWidget(QLabel("Std Dev Multiplier:"))
        self.spin_std_dev = QDoubleSpinBox()
        self.spin_std_dev.setRange(0.1, 10.0)
        self.spin_std_dev.setSingleStep(0.1)
        self.spin_std_dev.setValue(self.layer.std_dev_factor)
        self.spin_std_dev.setFixedWidth(80)
        std_layout.addWidget(self.spin_std_dev)
        std_layout.addStretch()

        # User defined limits input
        self.user_widget = QWidget()
        u_layout = QHBoxLayout(self.user_widget)
        u_layout.setContentsMargins(20, 0, 0, 0)
        u_layout.addWidget(QLabel("Min Override:"))
        self.user_min_edit = QLineEdit(str(self.layer.user_min) if self.layer.user_min is not None else "")
        self.user_min_edit.setPlaceholderText("Auto")
        self.user_min_edit.setFixedWidth(80)
        self.user_min_edit.setStyleSheet("QLineEdit { border: 1px solid #d4d8de; border-radius: 3px; padding: 3px; }")
        u_layout.addWidget(self.user_min_edit)

        u_layout.addWidget(QLabel("Max Override:"))
        self.user_max_edit = QLineEdit(str(self.layer.user_max) if self.layer.user_max is not None else "")
        self.user_max_edit.setPlaceholderText("Auto")
        self.user_max_edit.setFixedWidth(80)
        self.user_max_edit.setStyleSheet("QLineEdit { border: 1px solid #d4d8de; border-radius: 3px; padding: 3px; }")
        u_layout.addWidget(self.user_max_edit)
        u_layout.addStretch()

        # Assemble Limits Panel
        limits_layout.addWidget(self.radio_cumulative)
        limits_layout.addWidget(self.cumulative_widget)
        limits_layout.addWidget(self.radio_min_max)
        limits_layout.addWidget(self.radio_std_dev)
        limits_layout.addWidget(self.std_dev_widget)
        limits_layout.addWidget(self.radio_user)
        limits_layout.addWidget(self.user_widget)

        con_layout.addWidget(self.min_max_group)
        layout.addWidget(contrast_group)

        # Wire Up UI dynamic enable/disable slots
        self.contrast_combo.currentIndexChanged.connect(self._toggle_contrast_enhancement)
        self.radio_cumulative.toggled.connect(self._toggle_limits_subwidgets)
        self.radio_min_max.toggled.connect(self._toggle_limits_subwidgets)
        self.radio_std_dev.toggled.connect(self._toggle_limits_subwidgets)
        self.radio_user.toggled.connect(self._toggle_limits_subwidgets)

        # Trigger initial updates
        self.render_type_combo.currentIndexChanged.connect(self._toggle_raster_widgets)
        self._toggle_raster_widgets()
        self._toggle_contrast_enhancement()
        self._toggle_limits_subwidgets()
        
    def _toggle_raster_widgets(self):
        index = self.render_type_combo.currentIndex()
        self.rgb_widget.setVisible(index == 0)
        self.gray_widget.setVisible(index == 1)
        self.pseudo_widget.setVisible(index == 2)

    def _toggle_contrast_enhancement(self):
        is_stretch = (self.contrast_combo.currentIndex() == 1)
        self.min_max_group.setEnabled(is_stretch)

    def _toggle_limits_subwidgets(self):
        self.cumulative_widget.setEnabled(self.radio_cumulative.isChecked())
        self.std_dev_widget.setEnabled(self.radio_std_dev.isChecked())
        self.user_widget.setEnabled(self.radio_user.isChecked())
        
    def _setup_vector_style_widgets(self, layout):
        symbology_group = QGroupBox("Vector Symbology (矢量符号化)")
        symbology_group.setStyleSheet("QGroupBox { font-weight: bold; font-family: 'Segoe UI'; font-size: 13px; color: #1f6feb; }")
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
        self.stroke_width_spin.setStyleSheet("QSpinBox { border: 1px solid #d4d8de; border-radius: 3px; padding: 4px; width: 60px; }")
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
            # Save band render types
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
                
            # Save contrast enhancement algorithms
            self.layer.contrast_enhancement = "none" if self.contrast_combo.currentIndex() == 0 else "stretch_to_min_max"
            
            # Save limits calculation settings
            if self.radio_cumulative.isChecked():
                self.layer.min_max_limits_method = "cumulative_cut"
                self.layer.cumulative_cut_lower = self.spin_low_percent.value()
                self.layer.cumulative_cut_upper = self.spin_high_percent.value()
            elif self.radio_min_max.isChecked():
                self.layer.min_max_limits_method = "min_max"
            elif self.radio_std_dev.isChecked():
                self.layer.min_max_limits_method = "std_dev"
                self.layer.std_dev_factor = self.spin_std_dev.value()
            else:
                self.layer.min_max_limits_method = "user_defined"
                min_str = self.user_min_edit.text().strip()
                max_str = self.user_max_edit.text().strip()
                try:
                    self.layer.user_min = float(min_str) if min_str else None
                    self.layer.user_max = float(max_str) if max_str else None
                except ValueError:
                    QMessageBox.warning(self, "Invalid Values", "User defined Min/Max stretch values must be numeric.")
                    return
        else:
            # Vector styling
            self.layer.renderer.set_color(self.fill_color)
            self.layer.renderer.set_stroke_color(self.stroke_color)
            self.layer.renderer.set_stroke_width(self.stroke_width_spin.value())
            
        super().accept()
