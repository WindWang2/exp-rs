#!/usr/bin/env python3
"""
Antigravity RS - QGIS-style Main Window

Redesigned interface following QGIS layout principles:
- Top: Menu bar + Toolbar
- Left: Layer panel (C++ QgsLayerTreeView)
- Center: Map canvas (C++ QgsMapCanvas)
- Bottom: Status bar with coordinates + scale
"""

import sys
import os
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QMenuBar, QToolBar, QStatusBar, QDockWidget, QSplitter,
    QPushButton, QLabel, QFileDialog, QMessageBox
)
from PySide6.QtCore import Qt, Signal, QSize
from PySide6.QtGui import QIcon, QKeySequence, QAction

# Import our C++-backed components
import _antigravity_core as core
from gui.canvas import MapCanvas
from gui.map_tool import MapToolPan, MapToolZoom


class QgisStyleMainWindow(QMainWindow):
    """
    QGIS-style main window using C++ rendering engine.

    Layout follows QGIS Desktop conventions:
    - Menu bar: File, View, Layer, Settings, Help
    - Toolbar: File operations, map tools
    - Left dock: Layer tree (QgsLayerTreeView)
    - Central widget: Map canvas (QgsMapCanvas)
    - Status bar: Coordinates, scale, CRS
    """

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Antigravity RS - Remote Sensing Analysis")
        self.resize(1200, 800)

        # C++ components
        self.canvas = MapCanvas()
        self.layer_tree = core.QgsLayerTreeView()
        self.pan_tool = MapToolPan(self.canvas)
        self.zoom_tool = MapToolZoom(self.canvas, False)
        self.zoom_out_tool = MapToolZoom(self.canvas, True)

        self._setup_ui()
        self._setup_menu_bar()
        self._setup_toolbar()
        self._setup_status_bar()
        self._connect_signals()

        # Load sample data
        self._load_sample_layer()

    def _setup_ui(self):
        """Setup the main UI layout."""
        # Create a container widget for the central area
        central_widget = QWidget()
        central_layout = QVBoxLayout(central_widget)
        central_layout.setContentsMargins(0, 0, 0, 0)

        # For now, we'll use a placeholder since we need proper QWidget binding
        # The C++ QgsMapCanvas is a QGraphicsView but pybind11 doesn't expose QWidget inheritance
        placeholder = QLabel("Map Canvas - C++ Rendering Engine\n(Use toolbar to load layers)")
        placeholder.setAlignment(Qt.AlignCenter)
        placeholder.setStyleSheet("background-color: #e0e0e0; border: 2px solid #c0c0c0;")
        central_layout.addWidget(placeholder)

        # Store reference for direct access
        self._canvas_placeholder = placeholder
        self._canvas_container = central_layout

        self.setCentralWidget(central_widget)

        # Layer panel (left dock)
        self.layer_dock = QDockWidget("Layers", self)
        self.layer_dock.setAllowedAreas(Qt.LeftDockWidgetArea)
        self.layer_dock.setFeatures(QDockWidget.DockWidgetFloatable | QDockWidget.DockWidgetMovable)

        # Layer tree container (QgsLayerTreeView needs proper QWidget binding)
        layer_container = QWidget()
        layer_layout = QVBoxLayout(layer_container)
        layer_layout.setContentsMargins(4, 4, 4, 4)

        # Placeholder for layer tree
        layer_label = QLabel("Layer Tree\n(C++ QgsLayerTreeView)")
        layer_label.setAlignment(Qt.AlignTop)
        layer_layout.addWidget(layer_label)

        self.layer_dock.setWidget(layer_container)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.layer_dock)

        # Apply QGIS-like styling
        self.setStyleSheet("""
            QMainWindow {
                background-color: #f0f0f0;
            }
            QDockWidget {
                border: 1px solid #c0c0c0;
                titlebar-normal-icon-size: 16px;
            }
            QToolBar {
                spacing: 3px;
                padding: 2px;
            }
            QStatusBar {
                background-color: #e0e0e0;
            }
        """)

    def _setup_menu_bar(self):
        """Setup QGIS-style menu bar."""
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu("&File")

        open_action = QAction("Open Layer...", self)
        open_action.setShortcut(QKeySequence.Open)
        open_action.setStatusTip("Add a layer to the map")
        open_action.triggered.connect(self.open_layer)
        file_menu.addAction(open_action)

        file_menu.addSeparator()

        exit_action = QAction("E&xit", self)
        exit_action.setShortcut(QKeySequence.Quit)
        exit_action.setStatusTip("Exit the application")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # View menu
        view_menu = menubar.addMenu("&View")

        zoom_in_action = QAction("Zoom In", self)
        zoom_in_action.setShortcut(QKeySequence.ZoomIn)
        zoom_in_action.triggered.connect(self.zoom_in)
        view_menu.addAction(zoom_in_action)

        zoom_out_action = QAction("Zoom Out", self)
        zoom_out_action.setShortcut(QKeySequence.ZoomOut)
        zoom_out_action.triggered.connect(self.zoom_out)
        view_menu.addAction(zoom_out_action)

        view_menu.addSeparator()

        refresh_action = QAction("Refresh", self)
        refresh_action.setShortcut(QKeySequence.Refresh)
        refresh_action.triggered.connect(self.canvas.refresh)
        view_menu.addAction(refresh_action)

        # Layer menu
        layer_menu = menubar.addMenu("&Layer")

        add_layer_action = QAction("Add Layer...", self)
        add_layer_action.setShortcut(QKeySequence("Ctrl+L"))
        add_layer_action.triggered.connect(self.open_layer)
        layer_menu.addAction(add_layer_action)

        # Settings menu
        settings_menu = menubar.addMenu("&Settings")

        project_settings_action = QAction("Project Properties...", self)
        project_settings_action.triggered.connect(self.show_project_settings)
        settings_menu.addAction(project_settings_action)

        # Help menu
        help_menu = menubar.addMenu("&Help")

        about_action = QAction("About Antigravity RS", self)
        about_action.triggered.connect(self.show_about)
        help_menu.addAction(about_action)

    def _setup_toolbar(self):
        """Setup main toolbar."""
        toolbar = QToolBar("Main Toolbar", self)
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        # File operations
        open_action = QAction("Open Layer", self)
        open_action.setStatusTip("Add a layer to the map")
        open_action.triggered.connect(self.open_layer)
        toolbar.addAction(open_action)

        toolbar.addSeparator()

        # Map tools
        pan_action = QAction("Pan", self)
        pan_action.setCheckable(True)
        pan_action.setChecked(True)
        pan_action.setStatusTip("Pan the map")
        pan_action.triggered.connect(lambda: self.set_map_tool('pan'))
        toolbar.addAction(pan_action)

        zoom_in_action = QAction("Zoom In", self)
        zoom_in_action.setCheckable(True)
        zoom_in_action.setStatusTip("Zoom in")
        zoom_in_action.triggered.connect(lambda: self.set_map_tool('zoom_in'))
        toolbar.addAction(zoom_in_action)

        zoom_out_action = QAction("Zoom Out", self)
        zoom_out_action.setCheckable(True)
        zoom_out_action.setStatusTip("Zoom out")
        zoom_out_action.triggered.connect(lambda: self.set_map_tool('zoom_out'))
        toolbar.addAction(zoom_out_action)

        toolbar.addSeparator()

        # View operations
        refresh_action = QAction("Refresh", self)
        refresh_action.setStatusTip("Refresh the map")
        refresh_action.triggered.connect(self.canvas.refresh)
        toolbar.addAction(refresh_action)

        # Store tool actions for mutual exclusion
        self._tool_actions = {
            'pan': pan_action,
            'zoom_in': zoom_in_action,
            'zoom_out': zoom_out_action
        }

    def _setup_status_bar(self):
        """Setup QGIS-style status bar."""
        status_bar = QStatusBar()
        self.setStatusBar(status_bar)

        # Left: CRS display
        self.crs_label = QLabel("EPSG:3857")
        self.crs_label.setStyleSheet("padding: 2px 5px;")
        status_bar.addWidget(self.crs_label)

        # Center: Coordinates (live update)
        self.coords_label = QLabel("X: 0.00, Y: 0.00")
        self.coords_label.setMinimumWidth(200)
        self.coords_label.setStyleSheet("padding: 2px 5px;")
        status_bar.addPermanentWidget(self.coords_label)

        # Right: Scale
        self.scale_label = QLabel("Scale: 1:0")
        self.scale_label.setMinimumWidth(120)
        self.scale_label.setStyleSheet("padding: 2px 5px;")
        status_bar.addPermanentWidget(self.scale_label)

    def _connect_signals(self):
        """Connect canvas signals to UI updates."""
        # Coordinate updates from C++ canvas
        self.canvas.coordinates_changed.connect(self._update_coordinates)

        # Scale updates (periodic)
        from PySide6.QtCore import QTimer
        self.scale_timer = QTimer(self)
        self.scale_timer.timeout.connect(self._update_scale)
        self.scale_timer.start(500)

    def _update_coordinates(self, x, y):
        """Update coordinate display in status bar."""
        self.coords_label.setText(f"X: {x:.2f}, Y: {y:.2f}")

    def _update_scale(self):
        """Update scale display in status bar."""
        try:
            scale = self.canvas.scale()
            if scale > 0:
                self.scale_label.setText(f"Scale: 1:{int(scale)}")
        except:
            pass

    def set_map_tool(self, tool_name):
        """Set the current map tool."""
        tool_map = {
            'pan': self.pan_tool,
            'zoom_in': self.zoom_tool,
            'zoom_out': self.zoom_out_tool
        }

        if tool_name in tool_map:
            self.canvas.set_map_tool(tool_map[tool_name])

            # Update checked state of tool buttons
            for name, action in self._tool_actions.items():
                action.setChecked(name == tool_name)

    def open_layer(self):
        """Open file dialog to add a layer."""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Open Layer",
            os.path.expanduser("~/data"),
            "Raster Files (*.tif *.tiff *.img);;All Files (*.*)"
        )

        if file_path:
            self._add_layer(file_path)

    def _add_layer(self, file_path):
        """Add a layer to the map."""
        try:
            from core.raster.qgsrasterlayer import QgsRasterLayer

            # Create layer
            layer_name = os.path.basename(file_path)
            layer = QgsRasterLayer(f"layer_{os.urandom(4).hex()}", layer_name, file_path)

            # Add to canvas
            self.canvas.add_layer(layer)

            # Zoom to layer extent
            if not layer.extent().isNull():
                self.canvas.zoom_to_extent(layer.extent())

            self.statusBar().showMessage(f"Loaded layer: {layer_name}", 3000)

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to load layer:\n{e}")

    def _load_sample_layer(self):
        """Load a sample layer on startup."""
        sample_path = os.path.join(
            os.path.dirname(__file__),
            "data", "sample_crops.tif"
        )

        if os.path.exists(sample_path):
            self._add_layer(sample_path)

    def zoom_in(self):
        """Zoom in."""
        self.canvas.zoomIn()

    def zoom_out(self):
        """Zoom out."""
        self.canvas.zoomOut()

    def show_project_settings(self):
        """Show project settings dialog."""
        QMessageBox.information(
            self,
            "Project Properties",
            "Project settings dialog (placeholder)\n\n"
            "Current CRS: EPSG:3857\n"
            "Layers: " + str(len(self.canvas.layers()))
        )

    def show_about(self):
        """Show about dialog."""
        QMessageBox.about(
            self,
            "About Antigravity RS",
            "<h3>Antigravity RS</h3>"
            "<p>Remote Sensing Analysis Platform</p>"
            "<p>Powered by QGIS C++ Rendering Engine</p>"
            "<p>Version: 0.1.0 (QGIS-style)</p>"
        )


def main():
    """Entry point."""
    app = QApplication(sys.argv)
    app.setApplicationName("Antigravity RS")
    app.setOrganizationName("Antigravity")

    window = QgisStyleMainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
