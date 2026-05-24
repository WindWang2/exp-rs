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


# --- QgsCoordinateTransform tests ---

from core.qgscoordinatetransform import QgsCoordinateTransform, CRSTransformer
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle


def test_transform_point():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    p = ct.transform(QgsPointXY(0, 0))
    # At (0,0) the transform should be approximately (0, 0) in Web Mercator
    assert abs(p.x()) < 1.0
    assert abs(p.y()) < 1.0


def test_transform_is_short_circuited():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    ct = QgsCoordinateTransform(crs, crs)
    assert ct.isShortCircuited()


def test_transform_rect():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    r = QgsRectangle(-10, -10, 10, 10)
    tr = ct.transformRect(r)
    # Web Mercator extent should be much larger in meters
    assert tr.width() > r.width()


def test_transform_invalid():
    ct = QgsCoordinateTransform(None, None)
    assert not ct.isValid()


def test_crs_transformer_alias():
    """CRSTransformer must be an alias for QgsCoordinateTransform."""
    assert CRSTransformer is QgsCoordinateTransform


def test_transform_inverse():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    p = QgsPointXY(10.0, 20.0)
    projected = ct.transform(p)
    back = ct.inverseTransform(projected)
    assert abs(back.x() - 10.0) < 0.0001
    assert abs(back.y() - 20.0) < 0.0001


def test_transform_legacy_api():
    """Legacy transform_point / transform_bounds still work."""
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    x, y = ct.transform_point(10.0, 20.0)
    assert x > 1_000_000  # meters
    lx, ly, rx, ry = ct.transform_bounds(0, 0, 10, 10)
    assert lx < rx
    assert ly < ry


def test_transform_source_dest_crs():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    assert ct.sourceCrs().authid() == "EPSG:4326"
    assert ct.destinationCrs().authid() == "EPSG:3857"


def test_transform_short_circuit_identity():
    """Short-circuited transform returns the same point."""
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    ct = QgsCoordinateTransform(crs, crs)
    p = QgsPointXY(1.0, 2.0)
    result = ct.transform(p)
    assert result.x() == 1.0
    assert result.y() == 2.0
