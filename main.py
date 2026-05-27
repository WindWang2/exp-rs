#!/usr/bin/env python3
"""
Antigravity RS - QGIS-Style Main Interface

Minimalist remote sensing analysis platform with QGIS-inspired layout
using QGIS C++ rendering engine.
"""

import sys
import os
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QMenuBar, QToolBar, QStatusBar, QDockWidget,
    QPushButton, QLabel, QFileDialog, QMessageBox, QSplitter
)
from PySide6.QtCore import Qt, Signal, QTimer, QThreadPool
from PySide6.QtGui import QAction, QKeySequence

# Core imports
from core.qgsreader import GeospatialReader
from core.logger import log_info, log_error, log_warning
from core.raster.qgsrasterlayer import QgsRasterLayer

# C++-backed GUI components
import _antigravity_core as core
from gui.canvas import MapCanvas
from gui.map_tool import MapToolPan, MapToolZoom


class MinimalQgisWindow(QMainWindow):
    """
    Minimal QGIS-style interface using C++ rendering.

    Focus on core QGIS workflow:
    - Menu bar for all operations
    - Toolbar for common tools
    - Left panel for layers
    - Center for map canvas
    - Status bar for context
    """

    def __init__(self, sample_path=None):
        super().__init__()

        self.setWindowTitle("Antigravity RS")
        self.resize(1400, 900)

        # Initialize C++ components
        self.canvas = MapCanvas()
        self.layer_tree = core.QgsLayerTreeView()
        self.pan_tool = MapToolPan(self.canvas)
        self.zoom_tool = MapToolZoom(self.canvas, False)
        self.zoom_out_tool = MapToolZoom(self.canvas, True)

        self.sample_path = sample_path or os.path.join(
            os.path.dirname(__file__), "data", "sample_crops.tif"
        )

        self._setup_ui()
        self._setup_menus()
        self._setup_toolbar()
        self._setup_status_bar()
        self._setup_layer_panel()

        # Load sample data
        QTimer.singleShot(500, self.load_sample_data)

    def _setup_ui(self):
        """Setup main window UI."""
        # Create central widget to host the C++ map canvas
        # C++ QgsMapCanvas is a QWidget, so we can use it directly
        try:
            # Try to get the underlying QWidget from C++ QgsMapCanvas
            # In our wrapper, canvas.qgs_canvas is the C++ object
            cxx_canvas = self.canvas.qgs_canvas

            # Check if we can use it as a QWidget
            # For now, create a wrapper that displays canvas info
            self.canvas_container = QWidget()
            canvas_layout = QVBoxLayout(self.canvas_container)
            canvas_layout.setContentsMargins(0, 0, 0, 0)

            # Info label
            info_label = QLabel("C++ Rendering Engine - Map Canvas")
            info_label.setAlignment(Qt.AlignCenter)
            info_label.setStyleSheet("""
                QLabel {
                    background-color: #f0f0f0;
                    color: #333333;
                    font-size: 12px;
                    padding: 8px;
                    border: 1px solid #cccccc;
                }
            """)
            canvas_layout.addWidget(info_label)

            # Instructions
            instructions = QLabel("Use Toolbar → Add Layer to load imagery")
            instructions.setAlignment(Qt.AlignCenter)
            instructions.setStyleSheet("padding: 20px; color: #666666;")
            canvas_layout.addWidget(instructions)

            self.setCentralWidget(self.canvas_container)

        except Exception as e:
            # Fallback if canvas access fails
            print(f"Canvas setup error: {e}")
            placeholder = QLabel("Map Canvas (Initialization Error)")
            placeholder.setAlignment(Qt.AlignCenter)
            self.setCentralWidget(placeholder)


    def _setup_menus(self):
        """Setup QGIS-style menu bar."""
        menubar = self.menuBar()

        # Project menu (File equivalent)
        project_menu = menubar.addMenu("&Project")

        open_layer = QAction("Open Layer...", self)
        open_layer.setShortcut(QKeySequence("Ctrl+Shift+L"))
        open_layer.setStatusTip("Add a layer to the project")
        open_layer.triggered.connect(self.open_layer_dialog)
        project_menu.addAction(open_layer)

        project_menu.addSeparator()

        save_project = QAction("Save Project...", self)
        save_project.setShortcut(QKeySequence("Ctrl+S"))
        project_menu.addAction(save_project)

        project_menu.addSeparator()

        exit_action = QAction("Exit", self)
        exit_action.setShortcut(QKeySequence("Ctrl+Q"))
        exit_action.triggered.connect(self.close)
        project_menu.addAction(exit_action)

        # Edit menu
        edit_menu = menubar.addMenu("&Edit")

        undo_action = QAction("Undo", self)
        undo_action.setShortcut(QKeySequence.Undo)
        edit_menu.addAction(undo_action)

        redo_action = QAction("Redo", self)
        redo_action.setShortcut(QKeySequence.Redo)
        edit_menu.addAction(redo_action)

        # View menu
        view_menu = menubar.addMenu("&View")

        zoom_in = QAction("Zoom In", self)
        zoom_in.setShortcut(QKeySequence.ZoomIn)
        zoom_in.triggered.connect(self.zoom_in)
        view_menu.addAction(zoom_in)

        zoom_out = QAction("Zoom Out", self)
        zoom_out.setShortcut(QKeySequence.ZoomOut)
        zoom_out.triggered.connect(self.zoom_out)
        view_menu.addAction(zoom_out)

        view_menu.addSeparator()

        refresh = QAction("Refresh", self)
        refresh.setShortcut(QKeySequence.Refresh)
        refresh.triggered.connect(self.refresh_view)
        view_menu.addAction(refresh)

        # Layer menu
        layer_menu = menubar.addMenu("&Layer")

        add_layer = QAction("Add Layer...", self)
        add_layer.setShortcut(QKeySequence("Ctrl+L"))
        add_layer.triggered.connect(self.open_layer_dialog)
        layer_menu.addAction(add_layer)

        layer_menu.addSeparator()

        layer_properties = QAction("Layer Properties...", self)
        layer_menu.addAction(layer_properties)

        # Settings menu
        settings_menu = menubar.addMenu("Se&ttings")

        options = QAction("Options...", self)
        settings_menu.addAction(options)

        project_properties = QAction("Project Properties...", self)
        settings_menu.addAction(project_properties)

        # Help menu
        help_menu = menubar.addMenu("&Help")

        help_contents = QAction("Help Contents", self)
        help_contents.setShortcut(QKeySequence("F1"))
        help_menu.addAction(help_contents)

        help_menu.addSeparator()

        about = QAction("About Antigravity", self)
        about.triggered.connect(self.show_about)
        help_menu.addAction(about)

    def _setup_toolbar(self):
        """Setup main toolbar."""
        toolbar = QToolBar("File Toolbar", self)
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        # Project actions
        new_project = QAction("New Project", self)
        toolbar.addAction(new_project)

        open_project = QAction("Open Project", self)
        toolbar.addAction(open_project)

        save_project = QAction("Save", self)
        toolbar.addAction(save_project)

        toolbar.addSeparator()

        # Layer actions
        add_layer_action = QAction("Add Layer", self)
        add_layer_action.triggered.connect(self.open_layer_dialog)
        toolbar.addAction(add_layer_action)

        toolbar.addSeparator()

        # Map tools
        pan_action = QAction("Pan", self)
        pan_action.setCheckable(True)
        pan_action.setChecked(True)
        pan_action.triggered.connect(lambda: self.set_tool('pan'))
        toolbar.addAction(pan_action)

        zoom_in_action = QAction("Zoom In", self)
        zoom_in_action.setCheckable(True)
        zoom_in_action.triggered.connect(lambda: self.set_tool('zoom_in'))
        toolbar.addAction(zoom_in_action)

        zoom_out_action = QAction("Zoom Out", self)
        zoom_out_action.setCheckable(True)
        zoom_out_action.triggered.connect(lambda: self.set_tool('zoom_out'))
        toolbar.addAction(zoom_out_action)

        self.tool_actions = {
            'pan': pan_action,
            'zoom_in': zoom_in_action,
            'zoom_out': zoom_out_action
        }

    def _setup_status_bar(self):
        """Setup status bar."""
        status_bar = QStatusBar()
        self.setStatusBar(status_bar)

        # CRS label
        crs_label = QLabel("EPSG:3857")
        status_bar.addWidget(QLabel("CRS:"))
        status_bar.addWidget(crs_label)

        # Coordinates
        coords_label = QLabel("X: 0.00, Y: 0.00")
        coords_label.setMinimumWidth(200)
        status_bar.addPermanentWidget(coords_label)
        self.coords_label = coords_label

        # Scale
        scale_label = QLabel("Scale: 1:0")
        scale_label.setMinimumWidth(120)
        status_bar.addPermanentWidget(scale_label)
        self.scale_label = scale_label

        # Connect coordinate updates
        self.canvas.coordinates_changed.connect(
            lambda x, y: coords_label.setText(f"X: {x:.2f}, Y: {y:.2f}")
        )

        # Update scale every 500ms
        self.scale_timer = QTimer()
        self.scale_timer.timeout.connect(self.update_scale)
        self.scale_timer.start(500)

    def _setup_layer_panel(self):
        """Setup layer tree panel."""
        layer_dock = QDockWidget("Layers", self)
        layer_dock.setAllowedAreas(Qt.LeftDockWidgetArea)

        # Create container for layer tree
        layer_container = QWidget()
        layer_layout = QVBoxLayout(layer_container)
        layer_layout.setContentsMargins(4, 4, 4, 4)

        # Layer tree placeholder (QgsLayerTreeView needs QWidget binding)
        layer_label = QLabel("Layer Tree")
        layer_label.setStyleSheet("padding: 8px; font-weight: bold;")
        layer_layout.addWidget(layer_label)

        # Placeholder content
        layer_info = QLabel("No layers loaded")
        layer_info.setAlignment(Qt.AlignCenter)
        layer_info.setStyleSheet("padding: 20px; color: #888;")
        layer_layout.addWidget(layer_info)
        self.layer_info_label = layer_info

        layer_dock.setWidget(layer_container)
        self.addDockWidget(Qt.LeftDockWidgetArea, layer_dock)
        self.layer_dock = layer_dock

    def update_scale(self):
        """Update scale display."""
        try:
            scale = self.canvas.scale()
            if scale > 0:
                self.scale_label.setText(f"Scale: 1:{int(scale)}")
        except:
            pass

    def set_tool(self, tool_name):
        """Set current map tool."""
        tools = {
            'pan': self.pan_tool,
            'zoom_in': self.zoom_tool,
            'zoom_out': self.zoom_out_tool
        }

        if tool_name in tools:
            self.canvas.set_map_tool(tools[tool_name])

            # Update tool button states
            for name, action in self.tool_actions.items():
                action.setChecked(name == tool_name)

    def open_layer_dialog(self):
        """Open file dialog to add layer."""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Layer",
            os.path.expanduser("~/data"),
            "Raster Files (*.tif *.tiff *.img);;All Files (*.*)"
        )

        if file_path:
            self.load_layer(file_path)

    def load_layer(self, file_path):
        """Load a layer into the project."""
        try:
            layer_name = os.path.basename(file_path)
            layer_id = f"layer_{os.urandom(4).hex()}"

            layer = QgsRasterLayer(layer_id, layer_name, file_path)

            self.canvas.add_layer(layer)
            self.canvas.zoom_to_extent(layer.extent)  # extent is a property

            if self.layer_info_label:
                self.layer_info_label.setText(f"1 layer loaded")

            self.statusBar().showMessage(f"Loaded: {layer_name}", 3000)
            log_info(f"Loaded layer: {layer_name}")

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load layer:\n{e}")
            log_error(f"Layer load failed: {e}")

    def load_sample_data(self):
        """Load sample data on startup."""
        if os.path.exists(self.sample_path):
            self.load_layer(self.sample_path)

    def zoom_in(self):
        """Zoom in."""
        self.canvas.zoomIn()

    def zoom_out(self):
        """Zoom out."""
        self.canvas.zoomOut()

    def refresh_view(self):
        """Refresh map display."""
        self.canvas.refresh()

    def show_about(self):
        """Show about dialog."""
        QMessageBox.about(
            self,
            "About Antigravity RS",
            """
            <h2>Antigravity RS</h2>
            <p><b>Remote Sensing Analysis Platform</b></p>
            <p>Powered by QGIS C++ Rendering Engine</p>
            <hr>
            <p>Version: 0.2.0 (QGIS-Style)</p>
            <p>Licensed under GPL v2+</p>
            """
        )


def main():
    """Main entry point."""
    app = QApplication(sys.argv)
    app.setApplicationName("Antigravity RS")
    app.setOrganizationName("Antigravity")

    # Optional: Show splash screen
    # from gui.splash import OnboardingSplashScreen
    # splash = OnboardingSplashScreen()
    # splash.show()

    window = MinimalQgisWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
