"""Tests for QgsField and QgsFields (Tier 3 data model)."""
from core.qgsfield import QgsField
from core.qgsfields import QgsFields


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
