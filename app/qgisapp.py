import sys
import os
from PySide6.QtWidgets import (QApplication, QMainWindow, QDockWidget, QFileDialog, 
                               QMessageBox, QStatusBar, QToolBar, QTabWidget, QVBoxLayout, QWidget, QPushButton)
from PySide6.QtCore import Qt, QThreadPool, QRunnable, Slot, Signal, QObject, QRectF
from PySide6.QtGui import QAction, QIcon, QKeySequence

# Core engine, GUI, and Analysis package imports
from core.qgsreader import GeospatialReader
from core.logger import log_info, log_error, log_debug, log_warning
from core.raster.qgsrasterlayer import QgsRasterLayer, RasterLayer
from core.vector.qgsvectorlayer import QgsVectorLayer, VectorLayer
from core.qgsproject import QgsProject, GISProject
from gui.qgsmapcanvas import QgsMapCanvas, MapCanvas
from gui.qgsmaptoolpan import QgsMapToolPan, MapToolPan
from gui.layertree.qgslayertreewidget import QgsLayerTreeWidget, LayerTreeWidget
from gui.layertree.qgslayertreebridge import QgsLayerTreeCanvasBridge, LayerTreeCanvasBridge
from gui.toolbox.qgsprocessingtoolbox import QgsProcessingToolbox, ProcessingToolbox
from gui.qgsagentdock import QgsAgentDockWidget, AgentDockWidget
from gui.qgssplash import OnboardingSplashScreen
from gui.prototype_views import (SpectralProfileDialog, ModelBuilderDialog, 
                                 GeorefDialog, ROIEditorDialog, AttributeTableDialog)

from analysis.qgsprocessingregistry import QgsProcessingRegistry, ToolRegistry

def load_stylesheet(app):
    """Loads the global Slate Light theme from resources/styles.qss."""
    # Since this file is in app/ folder, the root resources folder is at parent level.
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    style_path = os.path.join(root_dir, "resources", "styles.qss")
    try:
        if os.path.exists(style_path):
            with open(style_path, "r") as f:
                app.setStyleSheet(f.read())
    except (IOError, OSError) as e:
        print(f"Warning: Could not load stylesheet: {e}")

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
        
        self.registry = QgsProcessingRegistry()
        self.threadpool = QThreadPool.globalInstance()
        
        # 1. Initialize Central Map Canvas
        self.canvas = QgsMapCanvas(self)
        self.setCentralWidget(self.canvas)
        
        # Setup the QGIS-aligned synchronization bridge
        self.bridge = QgsLayerTreeCanvasBridge(QgsProject.instance().layerTreeRoot(), self.canvas)
        
        # Set default map tool
        self.pan_tool = MapToolPan(self.canvas)
        self.canvas.setMapTool(self.pan_tool)
        
        # 2. Setup Layer Tree Manager (QGIS C++ emulations)
        self.layer_tree_widget = QgsLayerTreeWidget(self)
        self.layer_model = self.layer_tree_widget.model
        self.layer_view = self.layer_tree_widget.view
        
        # Sync layer tree events to canvas overlays
        self.layer_tree_widget.zoom_to_layer_requested.connect(self._zoom_to_layer)
        self.layer_tree_widget.remove_layer_requested.connect(self._remove_layer)
        self.layer_tree_widget.properties_requested.connect(self._show_layer_properties)
        self.layer_tree_widget.add_raster_requested.connect(self._open_raster_dialog)
        self.layer_tree_widget.add_vector_requested.connect(self._open_vector_dialog)
        
        # Dockable Layer Manager panel
        self.layer_dock = QDockWidget("LAYERS PANEL", self)
        self.layer_dock.setWidget(self.layer_tree_widget)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.layer_dock)
        
        # 3. Setup Processing Toolbox
        self.toolbox = QgsProcessingToolbox(self)
        self.toolbox.tool_triggered.connect(self._execute_tool)
        
        # Dockable Toolbox panel
        self.toolbox_dock = QDockWidget("PROCESSING TOOLBOX", self)
        self.toolbox_dock.setWidget(self.toolbox)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.toolbox_dock)
        self.tabifyDockWidget(self.layer_dock, self.toolbox_dock)
        self.layer_dock.raise_() # Show layers tree on start
        
        # 4. Setup AI Agent Conversational Dock
        self.agent_dock = QgsAgentDockWidget(self)
        self.agent_dock.tool_execution_requested.connect(self._execute_tool)
        self.addDockWidget(Qt.RightDockWidgetArea, self.agent_dock)
        
        # 4.1 Setup Log Messages Dock (Operation Logs)
        from gui.log_dock import LogDockWidget
        self.log_dock = LogDockWidget(self)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.log_dock)
        
        # 5. Build Toolbar and Menus
        self._create_menus_and_toolbars()
        
        # 6. Status Bar coordinate feedback and CRS Selector button
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.canvas.coordinates_changed.connect(self._update_coordinates_feedback)
        
        self.crs_button = QPushButton(f"CRS: {self.canvas._canvas_crs}")
        self.crs_button.setFlat(True)
        self.crs_button.setStyleSheet("""
            QPushButton {
                font-weight: bold;
                color: #1f6feb;
                border: none;
                padding: 2px 8px;
                font-size: 11px;
            }
            QPushButton:hover {
                text-decoration: underline;
                background-color: #e2e6ea;
                border-radius: 3px;
            }
        """)
        self.crs_button.clicked.connect(self._show_project_crs_dialog)
        self.status.addPermanentWidget(self.crs_button)
        
        # Listen to project CRS changes
        QgsProject.instance().crsChanged.connect(self._on_project_crs_changed)
        
        # Store loaded layers metadata: {layer_id: {"name": str, "type": str, "path": str, "extent": QRectF}}
        self.loaded_layers = {}
        
        # Auto-load synthetic crop dataset if supplied
        if sample_path and os.path.exists(sample_path):
            # Give short delay for canvas to boot and paint
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, lambda: self._load_file(sample_path))

    def _create_menus_and_toolbars(self):
        # Menu Bar
        menubar = self.menuBar()
        file_menu = menubar.addMenu("File (文件)")
        edit_menu = menubar.addMenu("Edit (编辑)")
        view_menu = menubar.addMenu("View (视图)")
        
        # View dock toggle actions (matches QGIS panel visibility controller)
        view_menu.addAction(self.layer_dock.toggleViewAction())
        view_menu.addAction(self.toolbox_dock.toggleViewAction())
        view_menu.addAction(self.agent_dock.toggleViewAction())
        view_menu.addAction(self.log_dock.toggleViewAction())
        
        rs_menu = menubar.addMenu("Remote Sensing (遥感分析)")
        help_menu = menubar.addMenu("Help (帮助)")
        
        # File operations
        open_project_act = QAction("📁 Open Project...", self)
        open_project_act.setShortcut(QKeySequence.Open)
        open_project_act.triggered.connect(self._open_project_dialog)
        file_menu.addAction(open_project_act)
        
        save_project_act = QAction("💾 Save Project", self)
        save_project_act.setShortcut(QKeySequence.Save)
        save_project_act.triggered.connect(self._save_project_dialog)
        file_menu.addAction(save_project_act)
        
        file_menu.addSeparator()
        
        # File loading actions
        load_raster_act = QAction("📁 Add Raster...", self)
        load_raster_act.triggered.connect(self._open_raster_dialog)
        file_menu.addAction(load_raster_act)
        
        load_vector_act = QAction("📁 Add Vector...", self)
        load_vector_act.triggered.connect(self._open_vector_dialog)
        file_menu.addAction(load_vector_act)
        
        # Remote Sensing / Analysis actions from Prototype Artboards
        spec_act = QAction("📊 Spectral Profile...", self)
        spec_act.triggered.connect(self._show_spectral_profile)
        rs_menu.addAction(spec_act)
        
        roi_act = QAction("✏️ ROI Editor...", self)
        roi_act.triggered.connect(self._show_roi_editor)
        rs_menu.addAction(roi_act)
        
        model_act = QAction("🔗 Model Builder...", self)
        model_act.triggered.connect(self._show_model_builder)
        rs_menu.addAction(model_act)
        
        gcp_act = QAction("📐 GCP Georeferencer...", self)
        gcp_act.triggered.connect(self._show_gcp_georef)
        rs_menu.addAction(gcp_act)
        
        attr_act = QAction("📋 Attribute Table...", self)
        attr_act.triggered.connect(self._show_attribute_table)
        rs_menu.addAction(attr_act)
        
        # Build main toolbar
        toolbar = QToolBar("Main Navigation Toolbar")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        
        # Group 1: File Operations
        toolbar.addAction(open_project_act)
        toolbar.addAction(save_project_act)
        toolbar.addAction(load_raster_act)
        toolbar.addAction(load_vector_act)
        toolbar.addSeparator()
        
        # Group 2: Navigation Map Tools
        pan_act = QAction("✋ Pan", self)
        pan_act.setCheckable(True)
        pan_act.setChecked(True)
        pan_act.triggered.connect(self._activate_pan_tool)
        toolbar.addAction(pan_act)
        self.pan_action = pan_act
        
        zoom_in_act = QAction("🔍+ Zoom In", self)
        zoom_in_act.triggered.connect(lambda: self.canvas.zoomIn() if hasattr(self.canvas, 'zoomIn') else None)
        toolbar.addAction(zoom_in_act)
        
        zoom_out_act = QAction("🔍- Zoom Out", self)
        zoom_out_act.triggered.connect(lambda: self.canvas.zoomOut() if hasattr(self.canvas, 'zoomOut') else None)
        toolbar.addAction(zoom_out_act)
        
        zoom_all_act = QAction("🗺️ Zoom Full", self)
        zoom_all_act.triggered.connect(self._zoom_to_all)
        toolbar.addAction(zoom_all_act)
        toolbar.addSeparator()
        
        # Group 3: High-Fidelity Prototype Tools
        toolbar.addAction(spec_act)
        toolbar.addAction(roi_act)
        toolbar.addAction(model_act)
        toolbar.addAction(gcp_act)
        toolbar.addAction(attr_act)

    def _activate_pan_tool(self):
        log_info("Map Canvas: Map pan tool activated (🤚 Pan)")
        self.canvas.set_map_tool(self.pan_tool)
        self.pan_action.setChecked(True)

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

    def _update_coordinates_feedback(self, x: float, y: float):
        """Smart coordinates display formatting based on whether Project CRS is geographic or projected."""
        crs = self.canvas._canvas_crs
        display_crs = crs
        if len(display_crs) > 15:
            if display_crs.startswith("+proj="):
                parts = display_crs.split()
                proj_type = ""
                for part in parts:
                    if part.startswith("+proj="):
                        proj_type = part.split("=")[1].upper()
                        break
                display_crs = f"PROJ: {proj_type}" if proj_type else "Custom"
            else:
                display_crs = display_crs[:12] + "..."
                
        if crs in ["EPSG:4326", "EPSG:4490"]:
            self.status.showMessage(f"Canvas coordinate: Lon = {x:.6f}°, Lat = {y:.6f}° ({display_crs})")
        else:
            self.status.showMessage(f"Canvas coordinate: X = {x:.2f} m, Y = {y:.2f} m ({display_crs})")

    def _show_project_crs_dialog(self):
        """Opens our custom ProjectCrsDialog to switch the active Project CRS."""
        log_info("Main Window: Requesting project CRS change...")
        from gui.qgsprojectcrsdialog import QgsProjectCrsDialog
        dialog = QgsProjectCrsDialog(self.canvas._canvas_crs, self)
        if dialog.exec() == QgsProjectCrsDialog.Accepted:
            log_info(f"Main Window: User selected new project CRS: '{dialog.selected_crs}'")
            QgsProject.instance().setCrs(dialog.selected_crs)

    def _on_project_crs_changed(self, crs_str: str):
        """Slot triggered when the Project CRS is updated. Synchronizes UI buttons."""
        log_info(f"Project Settings: Project CRS changed to '{crs_str}' - reprojecting map canvas view")
        display_crs = crs_str
        if len(display_crs) > 15:
            if display_crs.startswith("+proj="):
                parts = display_crs.split()
                proj_type = ""
                for part in parts:
                    if part.startswith("+proj="):
                        proj_type = part.split("=")[1].upper()
                        break
                display_crs = f"PROJ: {proj_type}..." if proj_type else "PROJ4..."
            else:
                display_crs = display_crs[:12] + "..."
                
        self.crs_button.setText(f"CRS: {display_crs}")
        self.crs_button.setToolTip(crs_str) # Show full CRS info on hover!
        self.status.showMessage(f"Project CRS switched to {crs_str}", 4000)

    def _save_project_dialog(self):
        """Opens file dialog to save the active workspace structure to JSON project formats."""
        log_info("Main Window: Requesting to save active project...")
        file_path, _ = QFileDialog.getSaveFileName(self, "Save GIS Project", "", "GIS Project (*.json *.qgs);;All Files (*)")
        if file_path:
            try:
                log_info(f"Project IO: Saving project configuration to: '{file_path}'")
                QgsProject.instance().saveProject(file_path)
                self.status.showMessage(f"Successfully saved project to {os.path.basename(file_path)}", 4000)
                log_info(f"Project IO: Successfully saved project to: '{file_path}'")
            except Exception as e:
                log_error(f"Project IO: Failed to save project to '{file_path}'. Error: {e}")
                QMessageBox.critical(self, "Save Error", f"Failed to save project: {e}")

    def _open_project_dialog(self):
        """Opens file dialog to load a workspace structure, rebuilding layers and tree hierarchies."""
        log_info("Main Window: Requesting to open project file...")
        file_path, _ = QFileDialog.getOpenFileName(self, "Open GIS Project", "", "GIS Project (*.json *.qgs);;All Files (*)")
        if file_path:
            try:
                log_info(f"Project IO: Loading project configuration from: '{file_path}'")
                QgsProject.instance().loadProject(file_path)
                
                # Rebuild local loaded_layers metadata
                self.loaded_layers.clear()
                for lid, layer in QgsProject.instance().mapLayers().items():
                    self.loaded_layers[lid] = {
                        "name": layer.name,
                        "type": "raster" if hasattr(layer, "provider") and hasattr(layer.provider, "reader") and layer.provider.reader.is_raster else "vector",
                        "path": layer.provider.reader.file_path,
                        "extent": layer.extent
                    }
                
                self.layer_model.layoutChanged.emit()
                self._zoom_to_all()
                self.status.showMessage(f"Successfully loaded project: {os.path.basename(file_path)}", 4000)
                log_info(f"Project IO: Successfully loaded and rebuilt project from: '{file_path}' ({len(self.loaded_layers)} layers loaded)")
            except Exception as e:
                log_error(f"Project IO: Failed to load project from '{file_path}'. Error: {e}")
                QMessageBox.critical(self, "Load Error", f"Failed to load project: {e}")

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
        log_info(f"Main Window: Requesting load of spatial file: '{file_path}'")
        try:
            reader = GeospatialReader(file_path)
            basename = os.path.basename(file_path)
            layer_id = f"layer_{len(self.loaded_layers) + 1}_{os.path.splitext(basename)[0]}"
            
            # Map layer in GUI tree model
            layer_type = "raster" if reader.is_raster else "vector"
            
            # Create and add layer to canvas
            if reader.is_raster:
                layer = QgsRasterLayer(layer_id, basename, file_path)
            else:
                layer = QgsVectorLayer(layer_id, basename, file_path)
                
            self.canvas.add_layer(layer)
            extent = layer.extent
                
            self.loaded_layers[layer_id] = {
                "name": basename,
                "type": layer_type,
                "path": file_path,
                "extent": extent
            }
            
            self._zoom_to_layer(layer_id)
            log_info(f"Main Window: Successfully loaded {layer_type} layer '{basename}' (ID: {layer_id})")
            self.status.showMessage(f"Successfully loaded {layer_type} layer: {basename}", 4000)
            
        except Exception as e:
            log_error(f"Main Window: Failed to load spatial file '{file_path}'. Error: {e}")
            QMessageBox.critical(self, "Loading Error", f"Failed to load spatial file: {e}")

    @Slot(str)
    def _zoom_to_layer(self, layer_id: str):
        if layer_id in self.loaded_layers:
            log_info(f"Main Window: Zooming to layer extent for ID: {layer_id}")
            extent = self.loaded_layers[layer_id]["extent"]
            self.canvas.setExtent(extent)

    @Slot(str)
    def _remove_layer(self, layer_id: str):
        """Removes layer from both layers database, view model, and map canvas."""
        if layer_id in self.loaded_layers:
            log_info(f"Main Window: Removing layer: {layer_id}")
            self.canvas.remove_layer(layer_id)
            self.layer_model.remove_layer_item(layer_id)
            del self.loaded_layers[layer_id]
            self.status.showMessage(f"Removed layer: {layer_id}", 3000)

    @Slot(str)
    def _show_layer_properties(self, layer_id: str):
        """Finds active layer from canvas and shows the dynamic styling and metadata dialog."""
        layer = None
        for l in self.canvas.layers():
            if l.id == layer_id:
                layer = l
                break
        if not layer:
            log_warning(f"Main Window: Failed to find layer {layer_id} for properties dialog")
            return
            
        log_info(f"Main Window: Launching layer properties dialog for layer: {layer_id} ('{layer.name}')")
        from gui.qgspropertiesdialog import QgsPropertiesDialog
        dialog = QgsPropertiesDialog(layer, self)
        if dialog.exec() == QgsPropertiesDialog.Accepted:
            log_info(f"Main Window: Layer properties accepted for layer: {layer_id} (New Name: '{layer.name}', Opacity: {layer.opacity})")
            # Update local layer database name
            if layer_id in self.loaded_layers:
                self.loaded_layers[layer_id]["name"] = layer.name
                
            self.layer_model.layoutChanged.emit()
            self.canvas.refresh()

    def _zoom_to_all(self):
        """Fits viewport around consolidated bounds of all layers."""
        log_info("Map Canvas: Zooming to consolidated bounds of all loaded layers")
        if not self.loaded_layers:
            return
            
        # Combine QRectF bounds
        from core.qgsrectangle import QgsRectangle
        union_rect = QgsRectangle()
        for layer in self.loaded_layers.values():
            union_rect = union_rect.united(layer["extent"])
            
        if not union_rect.isEmpty():
            self.canvas.setExtent(union_rect)

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

# Backward-compatibility alias
QgsMainWindow = MainWindow

def main():
    # Make sure analysis registers all standard tools on load
    import analysis
    
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
