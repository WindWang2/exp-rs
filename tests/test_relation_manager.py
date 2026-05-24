import sys
from PySide6.QtWidgets import QApplication
_app = QApplication.instance() or QApplication(sys.argv)

from core.qgsrelationmanager import QgsRelation, QgsRelationManager

def test_relation_create():
    rel = QgsRelation()
    rel.setId("rel1")
    rel.setName("Cities in Country")
    rel.setReferencingLayer("cities_layer_id")
    rel.setReferencedLayer("countries_layer_id")
    rel.addFieldPair("country_id", "id")
    assert rel.id() == "rel1"
    assert rel.name() == "Cities in Country"

def test_relation_manager_add():
    mgr = QgsRelationManager()
    rel = QgsRelation()
    rel.setId("rel1")
    mgr.addRelation(rel)
    assert mgr.relation("rel1") is rel

def test_relation_manager_relations_for_layer():
    mgr = QgsRelationManager()
    rel1 = QgsRelation()
    rel1.setId("rel1")
    rel1.setReferencingLayer("layer_a")
    rel1.setReferencedLayer("layer_b")
    mgr.addRelation(rel1)

    rel2 = QgsRelation()
    rel2.setId("rel2")
    rel2.setReferencingLayer("layer_c")
    rel2.setReferencedLayer("layer_a")
    mgr.addRelation(rel2)

    # layer_a is referenced by rel2
    relations = mgr.relationsForLayer("layer_a", QgsRelationManager.ReferencedLayer)
    assert len(relations) == 1
    assert relations[0].id() == "rel2"

def test_relation_manager_remove():
    mgr = QgsRelationManager()
    rel = QgsRelation()
    rel.setId("rel1")
    mgr.addRelation(rel)
    mgr.removeRelation("rel1")
    assert mgr.relation("rel1") is None

def test_relation_validity():
    rel = QgsRelation()
    assert not rel.isValid()
    rel.setId("rel1")
    rel.setReferencingLayer("a")
    rel.setReferencedLayer("b")
    rel.addFieldPair("fk", "pk")
    assert rel.isValid()
