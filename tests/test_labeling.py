"""Tests for core.labeling -- QgsPalLayerSettings and QgsVectorLayerLabelProvider."""

import sys

from PySide6.QtWidgets import QApplication

_app = QApplication.instance() or QApplication(sys.argv)

from core.labeling.qgspallabeling import QgsPalLayerSettings
from core.labeling.qgsvectorlayerlabelprovider import QgsVectorLayerLabelProvider


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
