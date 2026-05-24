import os
import pytest
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QModelIndex, Qt
from PySide6.QtGui import QImage, QPainter
from core.qgsproject import GISProject
from core.layertree import LayerTreeGroup, LayerTreeLayer
from core.raster.qgsrasterlayer import RasterLayer
from core.vector.qgsvectorlayer import VectorLayer
from gui.qgsmapcanvas import MapCanvas
from gui.layertree.qgslayertreebridge import LayerTreeCanvasBridge
from gui.layertree import LayerTreeModel

@pytest.fixture(scope="module")
def app():
    return QApplication.instance() or QApplication([])

@pytest.fixture(autouse=True)
def cleanup_project():
    """Ensure GISProject is cleared before/after every test."""
    GISProject.instance().clear()
    yield
    GISProject.instance().clear()

def test_gis_project_singleton_and_signals():
    proj = GISProject.instance()
    assert proj is not None
    assert GISProject.instance() is proj
    
    # Mock layer
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    layer = RasterLayer("l1", "Crops", raster_path)
    
    added_layers = []
    removed_layers = []
    
    proj.layersAdded.connect(lambda lst: added_layers.extend(lst))
    proj.layersRemoved.connect(lambda lst: removed_layers.extend(lst))
    
    # Add layer
    proj.addMapLayers([layer])
    assert proj.mapLayer("l1") == layer
    assert len(added_layers) == 1
    assert added_layers[0] == layer
    
    # Remove layer
    proj.removeMapLayers(["l1"])
    assert proj.mapLayer("l1") is None
    assert len(removed_layers) == 1
    assert removed_layers[0] == layer

def test_layer_tree_hierarchy():
    proj = GISProject.instance()
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    l1 = RasterLayer("l1", "Crops", raster_path)
    proj.addMapLayers([l1])
    
    root = proj.layerTreeRoot()
    assert root.nodeType() == "group"
    assert root.name == "root"
    
    # Add sub-group
    group = LayerTreeGroup("Environment")
    root.addChildNode(group)
    assert group.parent() == root
    assert len(root.children()) == 1
    
    # Add layer into sub-group
    layer_node = group.addLayer(l1)
    assert layer_node.nodeType() == "layer"
    assert layer_node.layer_id == "l1"
    assert layer_node.parent() == group
    assert layer_node.layer() == l1
    
    # Check dynamic name sync
    l1.name = "Sentinel crops"
    assert layer_node.name == "Sentinel crops"
    
    # Visibility inheritance check
    group.visible = False
    assert layer_node.visible is True # Individual node visible is still True
    # But effective visibility is computed dynamically in bridge
    
    # Remove node
    group.removeChildNode(layer_node)
    assert len(group.children()) == 0
    assert layer_node.parent() is None

def test_layer_tree_canvas_bridge(app):
    proj = GISProject.instance()
    canvas = MapCanvas()
    bridge = LayerTreeCanvasBridge(proj.layerTreeRoot(), canvas)
    
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    l1 = RasterLayer("l1", "Crops", raster_path)
    proj.addMapLayers([l1])
    
    root = proj.layerTreeRoot()
    
    # 1. Add layer to tree: should instantly update canvas
    layer_node = root.addLayer(l1)
    assert len(canvas.layers()) == 1
    assert canvas.layers()[0] == l1
    assert l1.visible is True
    
    # 2. Toggle tree node visibility: should sync to layer and refresh canvas
    layer_node.visible = False
    assert l1.visible is False
    
    # 3. Create nested group and move layer
    group = LayerTreeGroup("GroupA")
    root.addChildNode(group)
    
    root.removeChildNode(layer_node)
    group.addChildNode(layer_node)
    assert len(canvas.layers()) == 1
    assert canvas.layers()[0] == l1
    
    # 4. Hide whole group: should set effective layer visibility to False
    group.visible = False
    assert l1.visible is False

def test_layer_tree_model_abstract_item(app):
    proj = GISProject.instance()
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    l1 = RasterLayer("l1", "Crops", raster_path)
    proj.addMapLayers([l1])
    
    root = proj.layerTreeRoot()
    group = LayerTreeGroup("Folder")
    root.addChildNode(group)
    layer_node = group.addLayer(l1)
    
    model = LayerTreeModel()
    
    # Row Count Check
    assert model.rowCount(QModelIndex()) == 1 # Only group is top level
    
    # Parent index
    group_idx = model.index(0, 0, QModelIndex())
    assert group_idx.isValid()
    assert group_idx.internalPointer() == group
    assert model.rowCount(group_idx) == 1
    
    # Child index
    layer_idx = model.index(0, 0, group_idx)
    assert layer_idx.isValid()
    assert layer_idx.internalPointer() == layer_node
    
    # Data retrieve
    assert model.data(layer_idx, Qt.DisplayRole) == "Crops"
    assert model.data(layer_idx, Qt.CheckStateRole) == Qt.Checked
    
    # Set visible check state
    model.setData(layer_idx, Qt.Unchecked, Qt.CheckStateRole)
    assert layer_node.visible is False
    assert model.data(layer_idx, Qt.CheckStateRole) == Qt.Unchecked


def test_gis_project_crs_and_canvas_sync(app):
    proj = GISProject.instance()
    assert proj.crs() == "EPSG:3857" # default
    
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    l1 = RasterLayer("l1", "Crops", raster_path)
    l1.crs = "EPSG:32650"
    
    crs_signals = []
    proj.crsChanged.connect(lambda c: crs_signals.append(c))
    
    # Adding a layer with a CRS should automatically set the Project CRS to that layer's CRS!
    proj.addMapLayers([l1])
    assert proj.crs() == "EPSG:32650"
    assert len(crs_signals) == 1
    assert crs_signals[0] == l1.crs
    
    # Test setting CRS explicitly
    proj.setCrs("EPSG:4326")
    assert proj.crs() == "EPSG:4326"
    assert len(crs_signals) == 2
    assert crs_signals[1] == "EPSG:4326"


def test_project_save_load_serialization(app, tmp_path):
    proj = GISProject.instance()
    raster_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "data", "sample_crops.tif")
    l1 = RasterLayer("l1", "Crops", raster_path)
    l1.opacity = 0.75
    l1.render_type = "pseudocolor"
    l1.color_ramp = "magma"
    
    proj.addMapLayers([l1])
    
    # Build tree structure
    root = proj.layerTreeRoot()
    group = LayerTreeGroup("FolderGroup")
    root.addChildNode(group)
    group.addLayer(l1)
    
    proj.setCrs("EPSG:32650")
    
    # Save project to temp path
    proj_file = str(tmp_path / "test_project.json")
    proj.saveProject(proj_file)
    assert os.path.exists(proj_file)
    
    # Reset project and load back
    proj.clear()
    assert proj.crs() == "EPSG:3857"
    assert len(proj.mapLayers()) == 0
    assert len(root.children()) == 0
    
    proj.loadProject(proj_file)
    
    # Assert restored values
    assert proj.crs() == "EPSG:32650"
    assert len(proj.mapLayers()) == 1
    restored_layer = proj.mapLayer("l1")
    assert restored_layer is not None
    assert restored_layer.opacity == 0.75
    assert restored_layer.render_type == "pseudocolor"
    assert restored_layer.color_ramp == "magma"
    
    # Check tree structure restoration
    assert len(root.children()) == 1
    restored_group = root.children()[0]
    assert restored_group.nodeType() == "group"
    assert restored_group.name == "FolderGroup"
    assert len(restored_group.children()) == 1
    restored_layer_node = restored_group.children()[0]
    assert restored_layer_node.nodeType() == "layer"
    assert restored_layer_node.layer_id == "l1"


def test_qgis_aligned_class_names():
    from core.qgsproject import QgsProject, GISProject
    from core.qgscoordinatetransform import QgsCoordinateTransform, CRSTransformer
    from core.layertree import (
        QgsLayerTreeNode, LayerTreeNode,
        QgsLayerTreeGroup, LayerTreeGroup,
        QgsLayerTreeLayer, LayerTreeLayer
    )
    from core.qgsmaplayer import QgsMapLayer, MapLayer
    from core.qgsmaplayerrenderer import QgsMapLayerRenderer, MapLayerRenderer
    from core.qgsmapsettings import QgsMapSettings, MapSettings
    from core.raster.qgsrasterlayer import QgsRasterLayer, RasterLayer
    from core.raster.qgsrasterlayerrenderer import QgsRasterLayerRenderer, RasterLayerRenderer
    from core.vector.qgsvectorlayer import QgsVectorLayer, VectorLayer
    from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer, VectorLayerRenderer
    from gui.qgsmapcanvas import QgsMapCanvas, MapCanvas
    from gui import QgsMapTool, MapTool, QgsMapToolPan, MapToolPan

    assert QgsProject is GISProject
    assert QgsCoordinateTransform is CRSTransformer
    assert QgsLayerTreeNode is LayerTreeNode
    assert QgsLayerTreeGroup is LayerTreeGroup
    assert QgsLayerTreeLayer is LayerTreeLayer
    assert QgsMapLayer is MapLayer
    assert QgsMapLayerRenderer is MapLayerRenderer
    assert QgsMapSettings is MapSettings
    assert QgsRasterLayer is RasterLayer
    assert QgsRasterLayerRenderer is RasterLayerRenderer
    assert QgsVectorLayer is VectorLayer
    assert QgsVectorLayerRenderer is VectorLayerRenderer
    assert QgsMapCanvas is MapCanvas
    assert QgsMapTool is MapTool
    assert QgsMapToolPan is MapToolPan


