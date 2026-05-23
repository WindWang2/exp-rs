import pytest
from engine.core.display.base.map_layer import MapLayer

def test_map_layer_instantiation():
    class ConcreteLayer(MapLayer):
        def draw(self, painter, settings):
            return "drawing"

    layer = ConcreteLayer("test-id", "test-name")
    assert layer.id == "test-id"
    assert layer.name == "test-name"
    assert layer.visible is True
    assert layer.opacity == 1.0
    assert layer.draw(None, None) == "drawing"

def test_map_layer_abstract():
    with pytest.raises(TypeError):
        MapLayer("id", "name")
