from engine.core.display.base.map_settings import MapSettings

def test_map_settings_initialization():
    settings = MapSettings()
    assert settings.layers == []
    assert settings.extent is None
    assert settings.output_size is None
    assert settings.destination_crs == "EPSG:3857"
