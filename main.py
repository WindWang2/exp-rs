import sys
import os
from PySide6.QtWidgets import (QApplication, QMainWindow, QDockWidget, QFileDialog, 
                               QMessageBox, QStatusBar, QToolBar, QTabWidget, QVBoxLayout, QWidget)
from PySide6.QtCore import Qt, QThreadPool, QRunnable, Slot, Signal, QObject
from PySide6.QtGui import QAction, QIcon, QKeySequence

def load_stylesheet(app):
    """Loads the global Slate Light theme from resources/styles.qss."""
    style_path = os.path.join(os.path.dirname(__file__), "resources", "styles.qss")
    try:
        if os.path.exists(style_path):
            with open(style_path, "r") as f:
                app.setStyleSheet(f.read())
    except (IOError, OSError) as e:
        print(f"Warning: Could not load stylesheet: {e}")

# Core engine and GUI package imports
from engine.registry import ToolRegistry
from engine.core.reader import GeospatialReader
from engine.core.display.raster.layer import RasterLayer
from engine.core.display.vector.layer import VectorLayer
from gui.canvas import MapCanvas
from gui.map_tool import MapToolPan
from gui.layer_tree import LayerTreeModel, LayerTreeView
from gui.toolbox import ProcessingToolbox
from gui.agent_dock import AgentDockWidget
from gui.splash import OnboardingSplashScreen

class WorkerSignals(QObject):
    """Signals for background thread executions."""
    finished = Signal(bool, str, str) # Emits (success, output_file_path, error_message)

class ProcessingRunnable(QRunnable):
    """Background runner for GIS operations, keeping canvas perfectly responsive."""
    def __init__(self, fn, params):
        super().__init__()
        self.fn = fn
        self.params = params
        self.signals = WorkerSignals()
        
    def run(self):
        try:
            out_file = self.fn(**self.params)
            self.signals.finished.emit(True, out_file, "")
        except Exception as e:
            self.signals.finished.emit(False, "", str(e))

class MainWindow(QMainWindow):
    """
    Main Application Dashboard for Antigravity RS.
    Coordinates layer additions, canvas updates, background processing, and AI integrations.
    """
    def __init__(self, sample_path: str = None):
        super().__init__()
        self.setWindowTitle("Antigravity RS — Advanced Remote Sensing Platform")
        self.resize(1280, 800)
        
        self.registry = ToolRegistry()
        self.threadpool = QThreadPool.globalInstance()
        
        # 1. Initialize Central Map Canvas
        self.canvas = MapCanvas(self)
        self.setCentralWidget(self.canvas)
        
        # Set default map tool
        self.pan_tool = MapToolPan(self.canvas)
        self.canvas.set_map_tool(self.pan_tool)
        
        # 2. Setup Layer Tree Manager (QGIS C++ emulations)
        self.layer_model = LayerTreeModel(self)
        self.layer_view = LayerTreeView(self)
        self.layer_view.setModel(self.layer_model)
        
        # Sync layer tree events to canvas overlays
        self.layer_model.visibility_changed.connect(self._handle_layer_visibility_changed)
        self.layer_model.layers_reordered.connect(self._handle_layer_reorder)
        self.layer_view.zoom_to_layer_requested.connect(self._zoom_to_layer)
        self.layer_view.remove_layer_requested.connect(self._remove_layer)
        self.layer_view.properties_requested.connect(self._show_layer_properties)
        
        # Dockable Layer Manager panel
        self.layer_dock = QDockWidget("Layers Panel", self)
        self.layer_dock.setWidget(self.layer_view)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.layer_dock)
        
        # 3. Setup Processing Toolbox
        self.toolbox = ProcessingToolbox(self)
        self.toolbox.tool_triggered.connect(self._execute_tool)
        
        # Dockable Toolbox panel
        self.toolbox_dock = QDockWidget("Processing Toolbox", self)
        self.toolbox_dock.setWidget(self.toolbox)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.toolbox_dock)
        self.tabifyDockWidget(self.layer_dock, self.toolbox_dock)
        self.layer_dock.raise_() # Show layers tree on start
        
        # 4. Setup AI Agent Conversational Dock
        self.agent_dock = AgentDockWidget(self)
        self.agent_dock.tool_execution_requested.connect(self._execute_tool)
        self.addDockWidget(Qt.RightDockWidgetArea, self.agent_dock)
        
        # 5. Build Toolbar and Menus
        self._create_menus_and_toolbars()
        
        # 6. Status Bar coordinate feedback
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.canvas.coordinates_changed.connect(
            lambda x, y: self.status.showMessage(f"Canvas coordinate: X = {x:.2f}, Y = {y:.2f} (Web Mercator EPSG:3857)")
        )
        
        # Store loaded layers metadata: {layer_id: {"name": str, "type": str, "path": str, "extent": QRectF}}
        self.loaded_layers = {}
        
        # Auto-load synthetic crop dataset if supplied
        if sample_path and os.path.exists(sample_path):
            # Give short delay for canvas to boot and paint
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, lambda: self._load_file(sample_path))

    def _create_menus_and_toolbars(self):
        toolbar = QToolBar("Main Navigation Toolbar")
        self.addToolBar(toolbar)
        
        # File loading actions
        load_raster_act = QAction("Add Raster...", self)
        load_raster_act.triggered.connect(self._open_raster_dialog)
        toolbar.addAction(load_raster_act)
        
        load_vector_act = QAction("Add Vector...", self)
        load_vector_act.triggered.connect(self._open_vector_dialog)
        toolbar.addAction(load_vector_act)
        
        toolbar.addSeparator()
        
        # Navigation actions
        zoom_all_act = QAction("Zoom Full Extent", self)
        zoom_all_act.triggered.connect(self._zoom_to_all)
        toolbar.addAction(zoom_all_act)

    def _open_raster_dialog(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open Multi-Spectral Raster File", "", "GeoTIFF (*.tif *.tiff *.img);;All Files (*)")
        if file_path:
            self._load_file(file_path)

    def _open_vector_dialog(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Open Shapefile or GeoJSON Vector File", "", "Vectors (*.shp *.geojson *.gpkg);;All Files (*)")
        if file_path:
            self._load_file(file_path)

    def _load_file(self, file_path: str):
        """Loads dataset, registers in QGIS Model, and renders on QGraphicsScene."""
        try:
            reader = GeospatialReader(file_path)
            basename = os.path.basename(file_path)
            layer_id = f"layer_{len(self.loaded_layers) + 1}_{os.path.splitext(basename)[0]}"
            
            # Map layer in GUI tree model
            layer_type = "raster" if reader.is_raster else "vector"
            
            # Create and add layer to canvas
            if reader.is_raster:
                layer = RasterLayer(layer_id, basename, file_path)
            else:
                layer = VectorLayer(layer_id, basename, file_path)
                
            self.canvas.add_layer(layer)
            extent = layer.extent
                
            self.loaded_layers[layer_id] = {
                "name": basename,
                "type": layer_type,
                "path": file_path,
                "extent": extent
            }
            
            # Add to PyQt Layer Tree view model
            self.layer_model.add_layer_item(layer_id, basename, layer_type, file_path)
            self._zoom_to_layer(layer_id)
            
            self.status.showMessage(f"Successfully loaded {layer_type} layer: {basename}", 4000)
            
        except Exception as e:
            QMessageBox.critical(self, "Loading Error", f"Failed to load spatial file: {e}")

    @Slot(str, bool)
    def _handle_layer_visibility_changed(self, layer_id: str, visible: bool):
        """Synchronizes PyQt checkboxes toggling to canvas re-rendering."""
        for layer in self.canvas.layers:
            if layer.id == layer_id:
                layer.visible = visible
                self.canvas.refresh()
                break

    @Slot(list)
    def _handle_layer_reorder(self, layer_ids: list):
        """Re-orders the visual drawing stacks matching custom drag re-ordering."""
        layer_map = {l.id: l for l in self.canvas.layers}
        new_layers = []
        # Draw order in MapSettings is index 0 (bottom) to N (top).
        # layer_ids is top-to-bottom from UI.
        for lid in reversed(layer_ids):
            if lid in layer_map:
                new_layers.append(layer_map[lid])
        
        self.canvas.layers = new_layers
        self.canvas.refresh()

    @Slot(str)
    def _zoom_to_layer(self, layer_id: str):
        if layer_id in self.loaded_layers:
            extent = self.loaded_layers[layer_id]["extent"]
            self.canvas.zoom_to_extent(extent)

    @Slot(str)
    def _remove_layer(self, layer_id: str):
        """Removes layer from both layers database, view model, and map canvas."""
        if layer_id in self.loaded_layers:
            self.canvas.remove_layer(layer_id)
            self.layer_model.remove_layer_item(layer_id)
            del self.loaded_layers[layer_id]
            self.status.showMessage(f"Removed layer: {layer_id}", 3000)

    @Slot(str)
    def _show_layer_properties(self, layer_id: str):
        """Finds active layer from canvas and shows the dynamic styling and metadata dialog."""
        layer = None
        for l in self.canvas.layers:
            if l.id == layer_id:
                layer = l
                break
        if not layer:
            return
            
        from gui.properties_dialog import LayerPropertiesDialog
        dialog = LayerPropertiesDialog(layer, self)
        if dialog.exec() == LayerPropertiesDialog.Accepted:
            # Update local layer database name
            if layer_id in self.loaded_layers:
                self.loaded_layers[layer_id]["name"] = layer.name
                
            # Update display name in tree model
            for row in range(self.layer_model.rowCount()):
                item = self.layer_model.item(row)
                if hasattr(item, "layer_id") and item.layer_id == layer_id:
                    item.setText(layer.name)
                    break
                    
            self.canvas.refresh()

    def _zoom_to_all(self):
        """Fits viewport around consolidated bounds of all layers."""
        if not self.loaded_layers:
            return
            
        # Combine QRectF bounds
        union_rect = QRectF()
        for layer in self.loaded_layers.values():
            union_rect = union_rect.united(layer["extent"])
            
        if not union_rect.isEmpty():
            self.canvas.zoom_to_extent(union_rect)

    @Slot(str, dict)
    def _execute_tool(self, tool_name: str, params: dict):
        """Executes a Processing algorithm asynchronously in our QThreadPool."""
        tool = self.registry.get_tool(tool_name)
        if not tool:
            return
            
        self.status.showMessage(f"Running processing tool '{tool['label']}' in background...")
        
        # Instantiate background runner
        runnable = ProcessingRunnable(tool["fn"], params)
        # Handle completion callback in thread-safe signal loop
        runnable.signals.finished.connect(self._handle_tool_finished)
        self.threadpool.start(runnable)

    @Slot(bool, str, str)
    def _handle_tool_finished(self, success: bool, output_file: str, err_msg: str):
        if success:
            QMessageBox.information(self, "Success", f"Processing completed successfully!\nOutput file: {output_file}")
            # Auto load resulting computed raster layer on map canvas
            if os.path.exists(output_file):
                self._load_file(output_file)
        else:
            QMessageBox.critical(self, "Processing Error", f"Algorithm execution failed:\n{err_msg}")

def main():
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    load_stylesheet(app)
    
    # 1. Launch animated splash onboarding panel
    splash = OnboardingSplashScreen()
    splash.show()
    
    main_window = None
    
    # 2. Boot coordinator callback
    def on_boot_complete(sample_path: str):
        nonlocal main_window
        main_window = MainWindow(sample_path)
        main_window.show()
        
    splash.boot_complete.connect(on_boot_complete)
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
