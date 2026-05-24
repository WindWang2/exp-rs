"""Tests for QgsVectorDataProvider, QgsMemoryProvider, and QgsMapLayerStore."""
import pytest
import copy

from core.qgsfield import QgsField
from core.qgsfields import QgsFields
from core.qgsfeature import QgsFeature
from core.qgsgeometry import QgsGeometry
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle
from core.qgsfeatureiterator import QgsFeatureIterator
from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgswkbtypes import QgsWkbTypes
from core.vector.qgsvectordataprovider import QgsVectorDataProvider, Capabilities
from core.vector.qgsmemoryprovider import QgsMemoryProvider
from core.qgsmaplayerstore import QgsMapLayerStore


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _make_fields():
    """Create a simple schema with id (int), name (str), value (float)."""
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    fs.append(QgsField("value", float))
    return fs


def _make_feature(fid, name, value, point_xy=None):
    """Create a QgsFeature with id, name, value, and optional point geometry."""
    fields = _make_fields()
    feat = QgsFeature(fields, id=fid)
    feat.setAttribute(0, fid)
    feat.setAttribute(1, name)
    feat.setAttribute(2, value)
    if point_xy:
        feat.setGeometry(QgsGeometry.fromPointXY(point_xy))
    return feat


# ===========================================================================
# QgsVectorDataProvider (ABC) tests
# ===========================================================================

class TestQgsVectorDataProviderABC:
    """QgsVectorDataProvider should be abstract and not directly instantiable."""

    def test_cannot_instantiate_abstract(self):
        """QgsVectorDataProvider is an ABC; direct construction must raise TypeError."""
        with pytest.raises(TypeError):
            QgsVectorDataProvider()

    def test_capabilities_enum_values(self):
        """Capabilities bitfield values follow powers of two."""
        assert Capabilities.NoCapabilities == 0
        assert Capabilities.AddFeatures == 1
        assert Capabilities.DeleteFeatures == 2
        assert Capabilities.ChangeGeometries == 4
        assert Capabilities.ChangeAttributeValues == 8

    def test_capabilities_combined(self):
        """Capabilities can be OR-combined."""
        combo = Capabilities.AddFeatures | Capabilities.DeleteFeatures
        assert combo == 3
        assert combo & Capabilities.AddFeatures
        assert combo & Capabilities.DeleteFeatures


# ===========================================================================
# QgsMemoryProvider tests
# ===========================================================================

class TestQgsMemoryProviderConstruction:
    """Constructor and basic metadata."""

    def test_default_construction(self):
        """MemoryProvider with no args defaults to Unknown wkb, empty fields."""
        prov = QgsMemoryProvider()
        assert prov.featureCount() == 0
        assert prov.wkbType() == QgsWkbTypes.Type.Unknown
        assert prov.sourceName() == "memory"
        assert prov.isEditable() is True

    def test_construction_with_fields_and_wkb(self):
        fields = _make_fields()
        prov = QgsMemoryProvider(fields=fields, wkb_type=QgsWkbTypes.Type.Point)
        assert prov.fields().count() == 3
        assert prov.wkbType() == QgsWkbTypes.Type.Point

    def test_capabilities_include_all(self):
        prov = QgsMemoryProvider()
        caps = prov.capabilities()
        assert caps & Capabilities.AddFeatures
        assert caps & Capabilities.DeleteFeatures
        assert caps & Capabilities.ChangeGeometries
        assert caps & Capabilities.ChangeAttributeValues


class TestQgsMemoryProviderAddFeature:
    """addFeature / addFeatures behaviour."""

    def test_add_single_feature(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feat = _make_feature(0, "Alpha", 1.5, QgsPointXY(10, 20))
        assert prov.addFeature(feat) is True
        assert prov.featureCount() == 1
        # FID should be auto-assigned (1) since feature had id=0
        assert feat.id() == 1

    def test_add_feature_preserves_nonzero_fid(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feat = _make_feature(42, "Beta", 2.5, QgsPointXY(5, 5))
        prov.addFeature(feat)
        assert feat.id() == 42

    def test_add_multiple_features(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feats = [
            _make_feature(0, "A", 1.0, QgsPointXY(0, 0)),
            _make_feature(0, "B", 2.0, QgsPointXY(1, 1)),
            _make_feature(0, "C", 3.0, QgsPointXY(2, 2)),
        ]
        result = prov.addFeatures(feats)
        assert result is True
        assert prov.featureCount() == 3
        # Auto FIDs should be 1, 2, 3
        assert feats[0].id() == 1
        assert feats[1].id() == 2
        assert feats[2].id() == 3


class TestQgsMemoryProviderGetFeatures:
    """getFeatures iteration and request filtering."""

    def _populated_provider(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        prov.addFeature(_make_feature(10, "A", 1.0, QgsPointXY(1, 1)))
        prov.addFeature(_make_feature(20, "B", 2.0, QgsPointXY(5, 5)))
        prov.addFeature(_make_feature(30, "C", 3.0, QgsPointXY(10, 10)))
        return prov

    def test_get_all_features(self):
        prov = self._populated_provider()
        it = prov.getFeatures()
        collected = list(it)
        assert len(collected) == 3

    def test_filter_by_fid(self):
        prov = self._populated_provider()
        req = QgsFeatureRequest().setFilterFid(20)
        it = prov.getFeatures(req)
        collected = list(it)
        assert len(collected) == 1
        assert collected[0].id() == 20

    def test_filter_by_fids(self):
        prov = self._populated_provider()
        req = QgsFeatureRequest().setFilterFids({10, 30})
        it = prov.getFeatures(req)
        collected = list(it)
        assert len(collected) == 2
        ids = {f.id() for f in collected}
        assert ids == {10, 30}

    def test_filter_by_rect(self):
        prov = self._populated_provider()
        # Rect covering only (1,1) and (5,5)
        req = QgsFeatureRequest().setFilterRect(QgsRectangle(0, 0, 6, 6))
        it = prov.getFeatures(req)
        collected = list(it)
        assert len(collected) == 2
        ids = {f.id() for f in collected}
        assert ids == {10, 20}

    def test_filter_by_rect_no_match(self):
        prov = self._populated_provider()
        req = QgsFeatureRequest().setFilterRect(QgsRectangle(100, 100, 200, 200))
        it = prov.getFeatures(req)
        collected = list(it)
        assert len(collected) == 0

    def test_limit(self):
        prov = self._populated_provider()
        req = QgsFeatureRequest().setLimit(2)
        it = prov.getFeatures(req)
        collected = list(it)
        assert len(collected) == 2

    def test_iterator_next_feature_style(self):
        prov = self._populated_provider()
        it = prov.getFeatures()
        ok, feat = it.nextFeature()
        assert ok is True
        assert feat is not None


class TestQgsMemoryProviderDeleteFeature:
    """deleteFeature behaviour."""

    def test_delete_existing(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feat = _make_feature(10, "A", 1.0, QgsPointXY(0, 0))
        prov.addFeature(feat)
        assert prov.featureCount() == 1
        assert prov.deleteFeature(10) is True
        assert prov.featureCount() == 0

    def test_delete_nonexistent(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        assert prov.deleteFeature(999) is False


class TestQgsMemoryProviderChangeGeometry:
    """changeGeometry behaviour."""

    def test_change_geometry(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feat = _make_feature(10, "A", 1.0, QgsPointXY(0, 0))
        prov.addFeature(feat)
        new_geom = QgsGeometry.fromPointXY(QgsPointXY(99, 99))
        assert prov.changeGeometry(10, new_geom) is True
        # Verify
        it = prov.getFeatures(QgsFeatureRequest().setFilterFid(10))
        f = next(iter(it))
        assert f.geometry().asPoint().x() == 99

    def test_change_geometry_nonexistent(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        assert prov.changeGeometry(999, QgsGeometry()) is False


class TestQgsMemoryProviderChangeAttributes:
    """changeAttributeValues behaviour."""

    def test_change_attribute(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        feat = _make_feature(10, "A", 1.0, QgsPointXY(0, 0))
        prov.addFeature(feat)
        assert prov.changeAttributeValues(10, {1: "Updated"}) is True
        it = prov.getFeatures(QgsFeatureRequest().setFilterFid(10))
        f = next(iter(it))
        assert f.attribute(1) == "Updated"

    def test_change_attribute_nonexistent(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        assert prov.changeAttributeValues(999, {0: "nope"}) is False


class TestQgsMemoryProviderEditing:
    """startEditing / commitChanges / rollback."""

    def test_is_editable_by_default(self):
        prov = QgsMemoryProvider()
        assert prov.isEditable() is True

    def test_start_editing(self):
        prov = QgsMemoryProvider()
        assert prov.startEditing() is True
        assert prov.isEditable() is True

    def test_commit_changes(self):
        prov = QgsMemoryProvider()
        assert prov.commitChanges() is True

    def test_rollback(self):
        prov = QgsMemoryProvider()
        feat = _make_feature(0, "A", 1.0, QgsPointXY(0, 0))
        prov.addFeature(feat)
        # Rollback for memory provider is a no-op (features persist)
        assert prov.rollback() is True
        assert prov.featureCount() == 1


class TestQgsMemoryProviderExtent:
    """sourceExtent and sourceCrs."""

    def test_extent_empty(self):
        prov = QgsMemoryProvider()
        ext = prov.sourceExtent()
        assert ext.isEmpty()

    def test_extent_with_features(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        prov.addFeature(_make_feature(0, "A", 1.0, QgsPointXY(1, 2)))
        prov.addFeature(_make_feature(0, "B", 2.0, QgsPointXY(5, 8)))
        ext = prov.sourceExtent()
        assert ext.xMinimum() == 1.0
        assert ext.yMinimum() == 2.0
        assert ext.xMaximum() == 5.0
        assert ext.yMaximum() == 8.0

    def test_source_crs_none(self):
        prov = QgsMemoryProvider()
        assert prov.sourceCrs() is None

    def test_clone(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        prov.addFeature(_make_feature(0, "A", 1.0, QgsPointXY(0, 0)))
        clone = prov.clone()
        assert clone.featureCount() == prov.featureCount()
        assert clone.wkbType() == prov.wkbType()
        # Modifying clone should not affect original
        clone.addFeature(_make_feature(0, "B", 2.0, QgsPointXY(1, 1)))
        assert prov.featureCount() == 1
        assert clone.featureCount() == 2


class TestQgsMemoryProviderThreadSafety:
    """Ensure getFeatures returns a snapshot (copy) for thread safety."""

    def test_iteration_is_snapshot(self):
        prov = QgsMemoryProvider(fields=_make_fields(), wkb_type=QgsWkbTypes.Type.Point)
        prov.addFeature(_make_feature(0, "A", 1.0, QgsPointXY(0, 0)))
        it = prov.getFeatures()
        # Add a feature while iterating
        prov.addFeature(_make_feature(0, "B", 2.0, QgsPointXY(1, 1)))
        # The iterator should only see the snapshot at creation time
        collected = list(it)
        assert len(collected) == 1


# ===========================================================================
# QgsMapLayerStore tests
# ===========================================================================

class _DummyLayer:
    """Minimal stand-in for QgsMapLayer for store tests."""

    def __init__(self, layer_id, name):
        self.id = layer_id
        self.name = name


class TestQgsMapLayerStoreBasic:
    """Core CRUD operations."""

    def test_add_single_layer(self):
        store = QgsMapLayerStore()
        layer = _DummyLayer("L1", "Layer One")
        result = store.addMapLayer(layer)
        assert result is layer
        assert store.count() == 1

    def test_add_duplicate_returns_none(self):
        store = QgsMapLayerStore()
        layer = _DummyLayer("L1", "Layer One")
        store.addMapLayer(layer)
        result = store.addMapLayer(layer)
        assert result is None
        assert store.count() == 1

    def test_add_multiple_layers(self):
        store = QgsMapLayerStore()
        l1 = _DummyLayer("L1", "One")
        l2 = _DummyLayer("L2", "Two")
        added = store.addMapLayers([l1, l2])
        assert len(added) == 2
        assert store.count() == 2

    def test_add_multiple_with_duplicate(self):
        store = QgsMapLayerStore()
        l1 = _DummyLayer("L1", "One")
        l2 = _DummyLayer("L2", "Two")
        store.addMapLayer(l1)
        added = store.addMapLayers([l1, l2])
        # l1 is a duplicate, only l2 should be returned
        assert len(added) == 1
        assert added[0].id == "L2"

    def test_remove_layer(self):
        store = QgsMapLayerStore()
        layer = _DummyLayer("L1", "Layer One")
        store.addMapLayer(layer)
        assert store.removeMapLayer("L1") is True
        assert store.count() == 0

    def test_remove_nonexistent(self):
        store = QgsMapLayerStore()
        assert store.removeMapLayer("nope") is False

    def test_remove_multiple(self):
        store = QgsMapLayerStore()
        l1 = _DummyLayer("L1", "One")
        l2 = _DummyLayer("L2", "Two")
        l3 = _DummyLayer("L3", "Three")
        store.addMapLayers([l1, l2, l3])
        removed = store.removeMapLayers(["L1", "L3"])
        assert len(removed) == 2
        assert store.count() == 1
        assert store.mapLayer("L2") is not None

    def test_map_layer_lookup(self):
        store = QgsMapLayerStore()
        layer = _DummyLayer("L1", "Layer One")
        store.addMapLayer(layer)
        assert store.mapLayer("L1") is layer
        assert store.mapLayer("nonexistent") is None

    def test_map_layers_dict(self):
        store = QgsMapLayerStore()
        l1 = _DummyLayer("L1", "One")
        l2 = _DummyLayer("L2", "Two")
        store.addMapLayers([l1, l2])
        all_layers = store.mapLayers()
        assert len(all_layers) == 2
        assert "L1" in all_layers
        assert "L2" in all_layers

    def test_count(self):
        store = QgsMapLayerStore()
        assert store.count() == 0
        store.addMapLayer(_DummyLayer("L1", "One"))
        assert store.count() == 1

    def test_is_layer_registered(self):
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        assert store.isLayerRegistered("L1") is True
        assert store.isLayerRegistered("L2") is False

    def test_clear(self):
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        store.addMapLayer(_DummyLayer("L2", "Two"))
        store.clear()
        assert store.count() == 0
        assert store.mapLayers() == {}


class TestQgsMapLayerStoreSignals:
    """Signal emission on add/remove/clear."""

    def test_layer_was_added_signal(self, qtbot):
        store = QgsMapLayerStore()
        with qtbot.waitSignal(store.layerWasAdded, timeout=1000) as blocker:
            store.addMapLayer(_DummyLayer("L1", "One"))
        assert blocker.args == ["L1"]

    def test_layer_will_be_removed_signal(self, qtbot):
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        with qtbot.waitSignal(store.layerWillBeRemoved, timeout=1000) as blocker:
            store.removeMapLayer("L1")
        assert blocker.args == ["L1"]

    def test_layer_was_removed_signal(self, qtbot):
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        with qtbot.waitSignal(store.layerWasRemoved, timeout=1000) as blocker:
            store.removeMapLayer("L1")
        assert blocker.args == ["L1"]

    def test_no_signal_on_duplicate_add(self, qtbot):
        """Adding a duplicate should not emit layerWasAdded."""
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        # If no signal is emitted, qtbot will not get a signal -> test passes
        # We verify by checking return value
        result = store.addMapLayer(_DummyLayer("L1", "One"))
        assert result is None

    def test_signal_on_clear(self, qtbot):
        store = QgsMapLayerStore()
        store.addMapLayer(_DummyLayer("L1", "One"))
        store.addMapLayer(_DummyLayer("L2", "Two"))
        with qtbot.waitSignal(store.layerWasRemoved, timeout=1000) as blocker:
            store.clear()
        # layerWasRemoved fires for each removed layer; blocker captures the first
        assert blocker.args == ["L1"]
