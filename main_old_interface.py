import sys
import os
from PySide6.QtWidgets import (QApplication, QMainWindow, QFileDialog,
                               QMessageBox, QWidget)
from PySide6.QtCore import Qt, QThreadPool, QRunnable, Slot, Signal, QObject

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
from core.qgsreader import GeospatialReader
from core.logger import log_info, log_error, log_warning, log_debug
from core.raster.qgsrasterlayer import QgsRasterLayer as RasterLayer
from core.vector.qgsvectorlayer import QgsVectorLayer as VectorLayer
from gui.canvas import MapCanvas
from gui.map_tool import MapToolPan
from gui.layer_tree import LayerTreeModel, LayerTreeView
from gui.toolbox import ProcessingToolbox
from gui.agent_dock import AgentDockWidget
from gui.splash import OnboardingSplashScreen
from gui.prototype_views import (SpectralProfileDialog, ModelBuilderDialog, 
                                 GeorefDialog, ROIEditorDialog, AttributeTableDialog)

from analysis.qgsprocessingregistry import ToolRegistry

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
        
        from gui.workspace import RsWorkspace
        self.workspace = RsWorkspace(self)
        self.setCentralWidget(self.workspace)

        # Aliases so existing slots keep working unchanged
        self.canvas = self.workspace.canvas
        self.layer_model = self.workspace.layer_model
        self.layer_view = self.workspace.layer_view
        self.toolbox = self.workspace.toolbox
        self.status = self.workspace.status

        # Map tool
        self.pan_tool = MapToolPan(self.canvas)
        self.canvas.set_map_tool(self.pan_tool)

        # Layer tree signals (unchanged handlers)
        self.layer_model.visibility_changed.connect(self._handle_layer_visibility_changed)
        self.layer_model.layers_reordered.connect(self._handle_layer_reorder)
        self.layer_view.zoom_to_layer_requested.connect(self._zoom_to_layer)
        self.layer_view.remove_layer_requested.connect(self._remove_layer)
        self.layer_view.properties_requested.connect(self._show_layer_properties)
        self.layer_view.selectionModel().selectionChanged.connect(self._on_layer_selected)

        # Toolbox
        self.toolbox.tool_triggered.connect(self._execute_tool)

        # Console: stream real logs
        self.workspace.console.attach_logger("RSStudio")

        # AI Agent dock (hidden by default, toggled by toolbar/menu)
        self.agent_dock = AgentDockWidget(self)
        self.agent_dock.tool_execution_requested.connect(self._execute_tool)
        self.addDockWidget(Qt.RightDockWidgetArea, self.agent_dock)
        self.agent_dock.hide()

        # Status bar coordinate feedback
        self.canvas.coordinates_changed.connect(
            lambda x, y: self.status.set_coord(f"{x:.2f}, {y:.2f}"))
        self.status.set_crs("EPSG:3857 — Web Mercator")

        # Toolbar wiring
        self.workspace.toolbar.triggered.connect(self._on_toolbar)

        self._build_menubar()

        self.loaded_layers = {}
        if sample_path and os.path.exists(sample_path):
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, lambda: self._load_file(sample_path))

    def _build_menubar(self):
        from gui.rs_widgets import RsMenuBar
        from PySide6.QtWidgets import QLabel, QWidget, QHBoxLayout

        # Replace the native menu bar with our custom one
        mb = RsMenuBar(self)
        self.setMenuBar(mb)

        # Build all menus
        for label in ["文件", "编辑", "视图", "图层", "处理", "栅格", "矢量",
                      "数据库", "AI 助手", "插件", "窗口", "帮助"]:
            menu = mb.addMenu(label)
            if label == "文件":
                a1 = menu.addAction("添加栅格…"); a1.triggered.connect(self._open_raster_dialog)
                a2 = menu.addAction("添加矢量…"); a2.triggered.connect(self._open_vector_dialog)
                menu.addSeparator()
                a3 = menu.addAction("保存工程")
                a3.triggered.connect(lambda: QMessageBox.information(self, "保存", "工程已保存"))
            elif label == "视图":
                menu.addAction(self.agent_dock.toggleViewAction())
            elif label == "处理":
                menu.addAction("光谱剖面…").triggered.connect(self._show_spectral_profile)
                menu.addAction("ROI 编辑器…").triggered.connect(self._show_roi_editor)
                menu.addAction("模型构建器…").triggered.connect(self._show_model_builder)
                menu.addAction("几何校正…").triggered.connect(self._show_gcp_georef)
                menu.addAction("属性表…").triggered.connect(self._show_attribute_table)

        # Add right-side icons
        mb.add_version_icon("bell")
        mb.add_version_icon("user")

    def _on_toolbar(self, action_id):
        handlers = {
            "open": self._open_raster_dialog,
            "save": lambda: QMessageBox.information(self, "保存", "工程已保存"),
            "pan": lambda: self.canvas.set_map_tool(self.pan_tool),
            "zoomIn": lambda: getattr(self.canvas, "zoomIn", lambda: None)(),
            "zoomOut": lambda: getattr(self.canvas, "zoomOut", lambda: None)(),
            "zoomFit": self._zoom_to_all,
            "classify": self._show_roi_editor,
            "model": self._show_model_builder,
            "process": lambda: None,
            "ai": self._toggle_agent,
        }
        fn = handlers.get(action_id)
        if fn:
            fn()

    def _toggle_agent(self):
        self.agent_dock.setVisible(not self.agent_dock.isVisible())

    def _on_layer_selected(self, *_):
        idx = self.layer_view.currentIndex()
        item = self.layer_model.itemFromIndex(idx) if idx.isValid() else None
        lid = getattr(item, "layer_id", None)
        meta = self.loaded_layers.get(lid) if lid else None
        layer = next((l for l in self.canvas.layers() if getattr(l, "id", None) == lid), None)
        self.workspace.property_panel.set_layer(meta, layer)

    def _show_spectral_profile(self):
        log_info("Main Window: Opening Spectral Profile tool (📊 光谱剖面)")
        dlg = SpectralProfileDialog(self)
        dlg.exec()

    def _show_model_builder(self):
        log_info("Main Window: Opening Model Builder workflow editor (🔗 模型构建器)")
        dlg = ModelBuilderDialog(self)
        dlg.exec()

    def _show_gcp_georef(self):
        log_info("Main Window: Opening GCP Georeferencer table (📐 几何校正 GCP)")
        dlg = GeorefDialog(self)
        dlg.exec()

    def _show_roi_editor(self):
        log_info("Main Window: Opening Training Samples ROI Editor (✏️ 分类样本编辑)")
        dlg = ROIEditorDialog(self)
        dlg.exec()

    def _show_attribute_table(self):
        log_info("Main Window: Opening Attribute Table spreadsheet (📋 属性表)")
        dlg = AttributeTableDialog(self)
        dlg.exec()

    def _open_raster_dialog(self):
        log_info("Main Window: Requesting to add new raster layer...")
        file_path, _ = QFileDialog.getOpenFileName(self, "Open Multi-Spectral Raster File", "", "GeoTIFF (*.tif *.tiff *.img);;All Files (*)")
        if file_path:
            self._load_file(file_path)

    def _open_vector_dialog(self):
        log_info("Main Window: Requesting to add new vector layer...")
        file_path, _ = QFileDialog.getOpenFileName(self, "Open Shapefile or GeoJSON Vector File", "", "Vectors (*.shp *.geojson *.gpkg);;All Files (*)")
        if file_path:
            self._load_file(file_path)

    def _load_file(self, file_path: str):
        """Loads dataset, registers in QGIS Model, and renders on QGraphicsScene."""
        log_info(f"Main Window (main.py): Requesting load of spatial file: '{file_path}'")
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
            
            log_info(f"Main Window (main.py): Successfully loaded {layer_type} layer '{basename}' (ID: {layer_id})")
            self.status.set_message(f"Successfully loaded {layer_type} layer: {basename}")
            
        except Exception as e:
            log_error(f"Main Window (main.py): Failed to load spatial file '{file_path}'. Error: {e}")
            QMessageBox.critical(self, "Loading Error", f"Failed to load spatial file: {e}")

    @Slot(str, bool)
    def _handle_layer_visibility_changed(self, layer_id: str, visible: bool):
        """Synchronizes PyQt checkboxes toggling to canvas re-rendering."""
        log_info(f"Layers Tree: Visibility toggled for layer '{layer_id}' -> visible={visible}")
        for layer in self.canvas.layers():
            if layer.id == layer_id:
                layer.visible = visible
                self.canvas.refresh()
                break

    @Slot(list)
    def _handle_layer_reorder(self, layer_ids: list):
        """Re-orders the visual drawing stacks matching custom drag re-ordering."""
        log_info(f"Layers Tree: Layers reordered: {layer_ids}")
        layer_map = {l.id: l for l in self.canvas.layers()}
        new_layers = []
        # Draw order in MapSettings is index 0 (bottom) to N (top).
        # layer_ids is top-to-bottom from UI.
        for lid in reversed(layer_ids):
            if lid in layer_map:
                new_layers.append(layer_map[lid])
        
        self.canvas.setLayers(new_layers)
        self.canvas.refresh()

    @Slot(str)
    def _zoom_to_layer(self, layer_id: str):
        if layer_id in self.loaded_layers:
            log_info(f"Map Canvas: Zooming to layer '{layer_id}' extent")
            extent = self.loaded_layers[layer_id]["extent"]
            self.canvas.zoom_to_extent(extent)

    @Slot(str)
    def _remove_layer(self, layer_id: str):
        """Removes layer from both layers database, view model, and map canvas."""
        log_info(f"Layers Tree: Removing layer '{layer_id}'")
        if layer_id in self.loaded_layers:
            self.canvas.remove_layer(layer_id)
            self.layer_model.remove_layer_item(layer_id)
            del self.loaded_layers[layer_id]
            self.status.set_message(f"Removed layer: {layer_id}")

    @Slot(str)
    def _show_layer_properties(self, layer_id: str):
        """Finds active layer from canvas and shows the dynamic styling and metadata dialog."""
        layer = None
        for l in self.canvas.layers():
            if l.id == layer_id:
                layer = l
                break
        if not layer:
            return
            
        log_info(f"Main Window: Launching layer properties style dialog for '{layer_id}'")
        from gui.properties_dialog import LayerPropertiesDialog
        dialog = LayerPropertiesDialog(layer, self)
        if dialog.exec() == LayerPropertiesDialog.Accepted:
            log_info(f"Main Window: Accepted styling properties for layer '{layer_id}' (name='{layer.name}', opacity={layer.opacity})")
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
        log_info("Map Canvas: Zooming to consolidated bounds of all loaded layers")
        if not self.loaded_layers:
            return

        # Combine QgsRectangle bounds
        from core.qgsrectangle import QgsRectangle
        union_rect = QgsRectangle()
        for layer in self.loaded_layers.values():
            ext = layer["extent"]
            union_rect = union_rect.united(ext)

        if not union_rect.isEmpty():
            self.canvas.zoom_to_extent(union_rect)

    @Slot(str, dict)
    def _execute_tool(self, tool_name: str, params: dict):
        """Executes a Processing algorithm asynchronously in our QThreadPool."""
        tool = self.registry.get_tool(tool_name)
        if not tool:
            log_warning(f"Processing: Failed to run tool '{tool_name}' (not registered)")
            return
            
        log_info(f"Processing: Running processing tool '{tool['label']}' asynchronously in background QThreadPool...")
        self.status.set_message(f"Running processing tool '{tool['label']}' in background...")
        
        # Instantiate background runner
        runnable = ProcessingRunnable(tool["fn"], params)
        # Handle completion callback in thread-safe signal loop
        runnable.signals.finished.connect(self._handle_tool_finished)
        self.threadpool.start(runnable)

    @Slot(bool, str, str)
    def _handle_tool_finished(self, success: bool, output_file: str, err_msg: str):
        if success:
            log_info(f"Processing: Background task completed successfully. Result path: '{output_file}'")
            QMessageBox.information(self, "Success", f"Processing completed successfully!\nOutput file: {output_file}")
            # Auto load resulting computed raster layer on map canvas
            if os.path.exists(output_file):
                self._load_file(output_file)
        else:
            log_error(f"Processing: Background task failed. Error message: {err_msg}")
            QMessageBox.critical(self, "Processing Error", f"Algorithm execution failed:\n{err_msg}")

def main():
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    from gui.rs_fonts import load_fonts
    load_fonts()
    load_stylesheet(app)

    # Pre-warm transform cache on main thread to prevent pyproj segfaults
    # This MUST happen before any background rendering starts
    from core.qgstransformcache import transform_cache
    transform_cache().warmup()

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
