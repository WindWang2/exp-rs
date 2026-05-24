"""Tests for QgsCoordinateReferenceSystem (Tier 4 pyproj wrapper)."""
import pytest
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem


def test_crs_from_epsg():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert crs.isValid()
    assert crs.authid() == "EPSG:4326"


def test_crs_from_wkt():
    wkt = 'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]]'
    crs = QgsCoordinateReferenceSystem.fromWkt(wkt)
    assert crs.isValid()


def test_crs_is_geographic():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert crs.isGeographic()


def test_crs_is_projected():
    crs = QgsCoordinateReferenceSystem.fromEpsg(3857)
    assert not crs.isGeographic()


def test_crs_description():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert len(crs.description()) > 0


def test_crs_to_wkt():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    wkt = crs.toWkt()
    assert "WGS" in wkt or "EPSG" in wkt.upper()


def test_crs_invalid():
    crs = QgsCoordinateReferenceSystem.fromEpsg(999999)
    assert not crs.isValid()
    assert crs.authid() == ""
    assert crs.description() == ""
    assert crs.toWkt() == ""
    assert crs.isGeographic() is False


def test_crs_default_invalid():
    crs = QgsCoordinateReferenceSystem()
    assert not crs.isValid()


def test_crs_from_proj():
    crs = QgsCoordinateReferenceSystem.fromProj("+proj=longlat +datum=WGS84 +no_defs")
    assert crs.isValid()
    assert crs.isGeographic()


def test_crs_equality():
    crs1 = QgsCoordinateReferenceSystem.fromEpsg(4326)
    crs2 = QgsCoordinateReferenceSystem.fromEpsg(4326)
    crs3 = QgsCoordinateReferenceSystem.fromEpsg(3857)
    assert crs1 == crs2
    assert crs1 != crs3


def test_crs_repr():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert "EPSG:4326" in repr(crs)


def test_crs_from_srid():
    crs = QgsCoordinateReferenceSystem.fromSrid(4326)
    assert crs.isValid()
    assert crs.authid() == "EPSG:4326"


def test_crs_from_ogc_wms_crs():
    crs = QgsCoordinateReferenceSystem.fromOgcWmsCrs("EPSG:4326")
    assert crs.isValid()


def test_crs_map_units_geographic():
    from core.qgis import Qgis
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert crs.mapUnits() == Qgis.DistanceUnit.Degrees


def test_crs_map_units_projected():
    from core.qgis import Qgis
    crs = QgsCoordinateReferenceSystem.fromEpsg(3857)
    assert crs.mapUnits() == Qgis.DistanceUnit.Meters


def test_crs_map_units_invalid():
    from core.qgis import Qgis
    crs = QgsCoordinateReferenceSystem()
    assert crs.mapUnits() == Qgis.DistanceUnit.UnknownUnit
