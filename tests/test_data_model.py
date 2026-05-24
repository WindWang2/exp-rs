"""Tests for QgsField and QgsFields (Tier 3 data model)."""
from core.qgsfield import QgsField
from core.qgsfields import QgsFields
from core.qgsfeatureiterator import QgsFeatureIterator
from core.qgsrectangle import QgsRectangle


def test_field_creation():
    f = QgsField("name", str)
    assert f.name() == "name"
    assert f.type() == str


def test_field_numeric():
    f = QgsField("value", float, "Real", 10, 2)
    assert f.isNumeric()
    assert f.length() == 10
    assert f.precision() == 2


def test_field_type_name_auto():
    f = QgsField("id", int)
    assert f.typeName() == "int"


def test_field_type_name_explicit():
    f = QgsField("id", int, "Integer64")
    assert f.typeName() == "Integer64"


def test_field_comment_and_alias():
    f = QgsField("code", str, "", 4, 0, "Country code", "ISO 3166")
    assert f.comment() == "Country code"
    assert f.alias() == "ISO 3166"


def test_field_equality():
    a = QgsField("id", int)
    b = QgsField("id", int)
    c = QgsField("id", str)
    d = QgsField("other", int)
    assert a == b
    assert a != c
    assert a != d
    assert a != "not a field"


def test_field_repr():
    f = QgsField("name", str)
    assert "name" in repr(f)
    assert "str" in repr(f)


def test_fields_collection():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    assert fs.count() == 2
    assert fs.indexOf("name") == 1


def test_fields_by_name():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    f = fs.field("name")
    assert f.name() == "name"


def test_fields_not_found():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    assert fs.indexOf("missing") == -1
    assert fs.field("missing") is None


def test_fields_names():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    assert fs.names() == ["id", "name"]


def test_fields_is_empty():
    fs = QgsFields()
    assert fs.isEmpty()
    assert len(fs) == 0
    fs.append(QgsField("id", int))
    assert not fs.isEmpty()
    assert len(fs) == 1


def test_fields_at_index():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    assert fs.at(0).name() == "id"
    assert fs.at(1).name() == "name"


def test_fields_extend():
    fs1 = QgsFields()
    fs1.append(QgsField("id", int))
    fs2 = QgsFields()
    fs2.append(QgsField("name", str))
    fs1.extend(fs2)
    assert fs1.count() == 2
    assert fs1.names() == ["id", "name"]


def test_fields_iteration():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    names = [f.name() for f in fs]
    assert names == ["id", "name"]


def test_fields_getitem():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    assert fs[0].name() == "id"
    assert fs[1].name() == "name"


def test_fields_repr():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    r = repr(fs)
    assert "2" in r
    assert "fields" in r


# --- QgsFeature tests ---

from core.qgsfeature import QgsFeature
from core.qgsgeometry import QgsGeometry
from core.qgspointxy import QgsPointXY


def test_feature_creation():
    f = QgsFeature()
    assert f.id() == 0


def test_feature_with_fields():
    fields = QgsFields()
    fields.append(QgsField("id", int))
    fields.append(QgsField("name", str))
    f = QgsFeature(fields)
    assert f.fields().count() == 2


def test_feature_attributes():
    fields = QgsFields()
    fields.append(QgsField("id", int))
    fields.append(QgsField("name", str))
    f = QgsFeature(fields)
    f.setAttribute(0, 1)
    f.setAttribute(1, "test")
    assert f.attribute(0) == 1
    assert f.attribute("name") == "test"


def test_feature_geometry():
    f = QgsFeature()
    g = QgsGeometry.fromPointXY(QgsPointXY(1, 2))
    f.setGeometry(g)
    assert not f.geometry().isNull()


def test_feature_id():
    f = QgsFeature()
    f.setId(42)
    assert f.id() == 42


# --- QgsFeatureRequest tests ---

from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgsfeatureiterator import QgsFeatureIterator, QgsAbstractFeatureIterator
from core.qgsrectangle import QgsRectangle


def test_feature_request_no_filter():
    r = QgsFeatureRequest()
    assert r.filterType() == QgsFeatureRequest.FilterType.NoFilter


def test_feature_request_filter_rect():
    r = QgsFeatureRequest()
    r.setFilterRect(QgsRectangle(0, 0, 10, 10))
    assert r.filterType() == QgsFeatureRequest.FilterType.FilterRect


def test_feature_request_filter_fid():
    r = QgsFeatureRequest()
    r.setFilterFid(42)
    assert r.filterType() == QgsFeatureRequest.FilterType.FilterFid


def test_feature_request_subset():
    r = QgsFeatureRequest()
    r.setSubsetOfAttributes(["id", "name"])
    assert r.subsetOfAttributes() == ["id", "name"]


def test_feature_iterator_from_list():
    feats = [QgsFeature(id=i) for i in range(5)]
    it = QgsFeatureIterator(feats)
    collected = list(it)
    assert len(collected) == 5
    assert collected[0].id() == 0


# --- QgsFeatureSource and QgsFeatureSink tests ---

from core.qgsfeaturesource import QgsFeatureSource
from core.qgsfeaturesink import QgsFeatureSink


def test_feature_source_is_abstract():
    # Cannot instantiate abstract class
    import pytest
    with pytest.raises(TypeError):
        QgsFeatureSink()


def test_concrete_feature_source():
    """Test a concrete implementation of QgsFeatureSource."""
    class MySource(QgsFeatureSource):
        def getFeatures(self, request=None):
            return QgsFeatureIterator([])
        def sourceName(self):
            return "test"
        def fields(self):
            return QgsFields()
        def wkbType(self):
            return 0
        def featureCount(self):
            return 0
        def sourceExtent(self):
            return QgsRectangle()
        def sourceCrs(self):
            return None

    s = MySource()
    assert s.sourceName() == "test"
