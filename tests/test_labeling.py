"""Tests for core.labeling -- QgsPalLayerSettings and QgsVectorLayerLabelProvider."""

import json
import os
import sys

import pytest
from PySide6.QtWidgets import QApplication

_app = QApplication.instance() or QApplication(sys.argv)

from core.labeling.qgspallabeling import QgsPalLayerSettings
from core.labeling.qgsvectorlayerlabelprovider import QgsVectorLayerLabelProvider


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def vector_layer_path(tmp_path):
    """Create a minimal GeoJSON file for testing QgsVectorLayer."""
    geojson = {
        "type": "FeatureCollection",
        "features": [
            {
                "type": "Feature",
                "geometry": {"type": "Point", "coordinates": [10.0, 20.0]},
                "properties": {"name": "Alpha", "label": "A"},
            },
            {
                "type": "Feature",
                "geometry": {"type": "Point", "coordinates": [30.0, 40.0]},
                "properties": {"name": "Beta", "label": "B"},
            },
        ],
    }
    path = tmp_path / "test.geojson"
    path.write_text(json.dumps(geojson))
    return str(path)


# ---------------------------------------------------------------------------
# QgsPalLayerSettings
# ---------------------------------------------------------------------------

def test_pal_settings_defaults():
    settings = QgsPalLayerSettings()
    assert settings.fieldName == ""
    assert settings.enabled is False


def test_pal_settings_field():
    settings = QgsPalLayerSettings()
    settings.fieldName = "name"
    settings.enabled = True
    assert settings.fieldName == "name"


def test_pal_settings_font():
    from PySide6.QtGui import QFont

    settings = QgsPalLayerSettings()
    settings.font = QFont("Arial", 12)
    assert settings.font.pointSize() == 12


# ---------------------------------------------------------------------------
# QgsVectorLayerLabelProvider
# ---------------------------------------------------------------------------

def test_label_provider_create():
    settings = QgsPalLayerSettings()
    settings.fieldName = "name"
    settings.enabled = True
    provider = QgsVectorLayerLabelProvider(settings)
    assert provider.settings() is settings


def test_label_provider_not_enabled():
    settings = QgsPalLayerSettings()
    provider = QgsVectorLayerLabelProvider(settings)
    assert provider.isActive() is False


# ---------------------------------------------------------------------------
# Integration tests: wiring labeling into QgsVectorLayer
# ---------------------------------------------------------------------------

def test_vector_layer_labeling_settings(vector_layer_path):
    from core.vector.qgsvectorlayer import QgsVectorLayer

    layer = QgsVectorLayer("test_id", "Test", vector_layer_path)
    settings = QgsPalLayerSettings()
    settings.fieldName = "name"
    settings.enabled = True
    layer.setLabeling(settings)
    assert layer.labeling() is settings
    assert layer.labeling().enabled is True


def test_vector_layer_labeling_default_none(vector_layer_path):
    from core.vector.qgsvectorlayer import QgsVectorLayer

    layer = QgsVectorLayer("test_id", "Test", vector_layer_path)
    assert layer.labeling() is None


def test_vector_layer_labeling_replace(vector_layer_path):
    from core.vector.qgsvectorlayer import QgsVectorLayer

    layer = QgsVectorLayer("test_id", "Test", vector_layer_path)
    s1 = QgsPalLayerSettings()
    s1.fieldName = "name"
    layer.setLabeling(s1)
    s2 = QgsPalLayerSettings()
    s2.fieldName = "label"
    layer.setLabeling(s2)
    assert layer.labeling() is s2
    assert layer.labeling().fieldName == "label"


def test_vector_layer_renderer_receives_labeling(vector_layer_path):
    """Verify createMapRenderer passes labeling settings to the renderer."""
    from core.vector.qgsvectorlayer import QgsVectorLayer

    layer = QgsVectorLayer("test_id", "Test", vector_layer_path)
    settings = QgsPalLayerSettings()
    settings.fieldName = "name"
    settings.enabled = True
    layer.setLabeling(settings)

    from core.qgsmapsettings import QgsMapSettings
    map_settings = QgsMapSettings()
    renderer = layer.createMapRenderer(map_settings)
    assert renderer.labeling() is settings


def test_vector_layer_renderer_no_labeling(vector_layer_path):
    """When no labeling is set, renderer.labeling() is None."""
    from core.vector.qgsvectorlayer import QgsVectorLayer

    layer = QgsVectorLayer("test_id", "Test", vector_layer_path)

    from core.qgsmapsettings import QgsMapSettings
    map_settings = QgsMapSettings()
    renderer = layer.createMapRenderer(map_settings)
    assert renderer.labeling() is None
