import json
import os
from PySide6.QtCore import QObject, Signal

from core.qgsmaplayerstore import QgsMapLayerStore


class QgsProject(QObject):
    """
    Central Singleton Registry managing all map layers and project settings.
    Decouples data layer lifecycles from direct UI widget manipulation,
    matching the architecture of QgsProject in QGIS.

    Layer storage is delegated to an internal QgsMapLayerStore instance.
    """
    layersAdded = Signal(list)
    layersRemoved = Signal(list)
    crsChanged = Signal(str)
    _instance = None

    def __init__(self):
        super().__init__()
        self._layer_store = QgsMapLayerStore()
        self._crs = None
        from core.layertree import QgsLayerTreeGroup
        self._root_node = QgsLayerTreeGroup("root")

    # ------------------------------------------------------------------
    # Backward-compatible property — _layers now delegates to the store
    # ------------------------------------------------------------------

    @property
    def _layers(self):
        """Direct reference to the store's internal layer dict."""
        return self._layer_store._layers

    # ------------------------------------------------------------------
    # Singleton
    # ------------------------------------------------------------------

    @classmethod
    def instance(cls):
        if cls._instance is None:
            cls._instance = QgsProject()
        return cls._instance

    # ------------------------------------------------------------------
    # Layer Store access
    # ------------------------------------------------------------------

    def layerStore(self) -> QgsMapLayerStore:
        """Returns the project's internal QgsMapLayerStore."""
        return self._layer_store

    # ------------------------------------------------------------------
    # Layer management (delegated to store)
    # ------------------------------------------------------------------

    def addMapLayers(self, layers: list):
        """Registers a list of map layers in the project and emits layersAdded."""
        added = self._layer_store.addMapLayers(layers)
        # QGIS behavior: Set project CRS to first layer's CRS if not set
        for layer in added:
            if self._crs is None and getattr(layer, "crs", None):
                self.setCrs(layer.crs)
        if added:
            self.layersAdded.emit(added)

    def removeMapLayers(self, layer_ids: list):
        """Deregisters map layers by ID and emits layersRemoved."""
        removed = self._layer_store.removeMapLayers(layer_ids)
        if removed:
            self.layersRemoved.emit(removed)

    def mapLayers(self) -> dict:
        """Returns the complete map of registered layers."""
        return self._layer_store.mapLayers()

    def mapLayer(self, layer_id: str):
        """Returns a registered layer by its ID."""
        return self._layer_store.mapLayer(layer_id)
        
    def layerTreeRoot(self):
        """Returns the project's root LayerTreeGroup node."""
        return self._root_node
        
    def crs(self) -> str:
        """Returns the active Project CRS, defaulting to EPSG:3857."""
        return self._crs or "EPSG:3857"
        
    def setCrs(self, crs_str: str):
        """Sets the Project CRS and emits crsChanged if changed."""
        if self._crs != crs_str:
            self._crs = crs_str
            self.crsChanged.emit(crs_str)
        
    def clear(self):
        """Clears all registered layers and resets the layer tree and CRS."""
        self._layer_store.clear()
        self._root_node.clear()
        self._crs = None

    def saveProject(self, file_path: str):
        """Serializes the entire project CRS, layers, styling, and layer tree to a JSON file."""
        # 1. Serialize Layer Styles
        layers_data = {}
        for lid, layer in self._layers.items():
            is_raster = hasattr(layer, "provider") and hasattr(layer.provider, "reader") and layer.provider.reader.is_raster
            layer_info = {
                "type": "raster" if is_raster else "vector",
                "name": layer.name,
                "path": layer.provider.reader.file_path,
                "opacity": layer.opacity,
                "visible": layer.visible
            }
            if is_raster:
                layer_info.update({
                    "render_type": layer.render_type,
                    "red_band": layer.red_band,
                    "green_band": layer.green_band,
                    "blue_band": layer.blue_band,
                    "gray_band": layer.gray_band,
                    "pseudocolor_band": layer.pseudocolor_band,
                    "color_ramp": layer.color_ramp,
                    "contrast_enhancement": layer.contrast_enhancement,
                    "min_max_limits_method": layer.min_max_limits_method,
                    "cumulative_cut_lower": layer.cumulative_cut_lower,
                    "cumulative_cut_upper": layer.cumulative_cut_upper,
                    "std_dev_factor": layer.std_dev_factor,
                    "user_min": layer.user_min,
                    "user_max": layer.user_max
                })
            else:
                from PySide6.QtGui import QColor
                fill = layer.renderer.color()
                stroke = layer.renderer.stroke_color()
                layer_info.update({
                    "fill_color": [fill.red(), fill.green(), fill.blue(), fill.alpha()],
                    "stroke_color": [stroke.red(), stroke.green(), stroke.blue(), stroke.alpha()],
                    "stroke_width": layer.renderer.stroke_width()
                })
            layers_data[lid] = layer_info

        # 2. Serialize Layer Tree Hierarchy
        def serialize_node(node):
            if node.nodeType() == "group":
                return {
                    "type": "group",
                    "name": node.name,
                    "visible": node.visible,
                    "children": [serialize_node(c) for c in node.children()]
                }
            else:
                return {
                    "type": "layer",
                    "layer_id": node.layer_id,
                    "name": node.name,
                    "visible": node.visible
                }

        tree_data = [serialize_node(c) for c in self._root_node.children()]

        # 3. Assemble JSON
        project_data = {
            "crs": self.crs(),
            "layers": layers_data,
            "tree": tree_data
        }

        with open(file_path, "w", encoding="utf-8") as f:
            json.dump(project_data, f, indent=2)

    def loadProject(self, file_path: str):
        """Deserializes project CRS, layers, and layer tree from a JSON file, rebuilding styling."""
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"Project file not found: {file_path}")

        with open(file_path, "r", encoding="utf-8") as f:
            project_data = json.load(f)

        self.clear()

        # 1. Load project CRS
        self.setCrs(project_data.get("crs", "EPSG:3857"))

        # 2. Re-instantiate MapLayers
        from core.raster import QgsRasterLayer
        from core.vector import QgsVectorLayer
        from PySide6.QtGui import QColor

        loaded_layers = []
        for lid, info in project_data.get("layers", {}).items():
            path = info["path"]
            name = info["name"]
            
            if info["type"] == "raster":
                layer = QgsRasterLayer(lid, name, path)
                layer.opacity = info.get("opacity", 1.0)
                layer.visible = info.get("visible", True)
                layer.render_type = info.get("render_type", "multiband")
                layer.red_band = info.get("red_band", 1)
                layer.green_band = info.get("green_band", 2)
                layer.blue_band = info.get("blue_band", 3)
                layer.gray_band = info.get("gray_band", 1)
                layer.pseudocolor_band = info.get("pseudocolor_band", 1)
                layer.color_ramp = info.get("color_ramp", "viridis")
                layer.contrast_enhancement = info.get("contrast_enhancement", "none")
                layer.min_max_limits_method = info.get("min_max_limits_method", "cumulative_cut")
                layer.cumulative_cut_lower = info.get("cumulative_cut_lower", 2.0)
                layer.cumulative_cut_upper = info.get("cumulative_cut_upper", 98.0)
                layer.std_dev_factor = info.get("std_dev_factor", 2.0)
                layer.user_min = info.get("user_min")
                layer.user_max = info.get("user_max")
            else:
                layer = QgsVectorLayer(lid, name, path)
                layer.opacity = info.get("opacity", 1.0)
                layer.visible = info.get("visible", True)
                
                # Restore vector styling
                fc = info.get("fill_color", [100, 150, 240, 200])
                sc = info.get("stroke_color", [50, 50, 50, 255])
                layer.renderer.set_color(QColor(fc[0], fc[1], fc[2], fc[3]))
                layer.renderer.set_stroke_color(QColor(sc[0], sc[1], sc[2], sc[3]))
                layer.renderer.set_stroke_width(info.get("stroke_width", 1))

            self._layers[lid] = layer
            loaded_layers.append(layer)

        # 3. Recursively rebuild tree nodes
        from core.layertree import QgsLayerTreeGroup, QgsLayerTreeLayer
        
        def deserialize_node(node_data):
            if node_data["type"] == "group":
                gp = QgsLayerTreeGroup(node_data["name"])
                gp.visible = node_data.get("visible", True)
                for child in node_data.get("children", []):
                    gp.addChildNode(deserialize_node(child))
                return gp
            else:
                lay_node = QgsLayerTreeLayer(node_data["layer_id"], node_data["name"])
                lay_node.visible = node_data.get("visible", True)
                return lay_node

        for node_data in project_data.get("tree", []):
            self._root_node.addChildNode(deserialize_node(node_data))

        # Notify model and canvas
        self.layersAdded.emit(loaded_layers)


GISProject = QgsProject
