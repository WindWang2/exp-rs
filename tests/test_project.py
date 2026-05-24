import sys
from PySide6.QtWidgets import QApplication
_app = QApplication.instance() or QApplication(sys.argv)

from core.qgsproject import QgsProject
from core.qgsmaplayer import QgsMapLayer
from PySide6.QtCore import QRectF


class _StubLayer(QgsMapLayer):
    """Minimal concrete QgsMapLayer for testing (no real data)."""

    def __init__(self, layer_id: str, name: str):
        super().__init__(layer_id, name)
        self._extent = QRectF(0, 0, 1, 1)

    @property
    def extent(self):
        return self._extent

    def createMapRenderer(self, settings):
        return None


def test_project_singleton():
    p1 = QgsProject.instance()
    p2 = QgsProject.instance()
    assert p1 is p2


def test_project_uses_layer_store():
    """QgsProject should delegate to QgsMapLayerStore internally."""
    project = QgsProject.instance()
    assert hasattr(project, 'layerStore')
    store = project.layerStore()
    assert store is not None


def test_project_add_layer():
    project = QgsProject.instance()
    project.clear()
    layer = _StubLayer("test1", "Test Layer")
    project.addMapLayers([layer])
    assert project.mapLayer(layer.id) is layer


def test_project_remove_layer():
    project = QgsProject.instance()
    project.clear()
    layer = _StubLayer("test2", "Test Layer")
    project.addMapLayers([layer])
    project.removeMapLayers([layer.id])
    assert project.mapLayer(layer.id) is None


def test_project_layer_store_sync():
    """Layer store should stay in sync with project."""
    project = QgsProject.instance()
    project.clear()
    layer = _StubLayer("test3", "Test Layer")
    project.addMapLayers([layer])
    store = project.layerStore()
    assert store.mapLayer(layer.id) is layer


def test_project_clear():
    project = QgsProject.instance()
    project.clear()
    assert project.mapLayers() == {}
