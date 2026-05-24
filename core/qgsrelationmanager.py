"""QgsRelationManager - manages inter-layer relations (e.g. foreign keys).

This module provides:
- **QgsRelation**: describes a single relation between two layers via
  referencing (foreign-key) and referenced (primary-key) field pairs.
- **QgsRelationManager**: a registry that holds a set of :class:`QgsRelation`
  instances and allows lookup by ID or by the layer that participates in the
  relation.

The design mirrors the QGIS ``QgsRelation`` / ``QgsRelationManager`` API as
closely as possible while remaining a lightweight, pure-Python implementation
without QObject dependencies.
"""


class QgsRelation:
    """Describes a relation between two map layers.

    A relation connects a *referencing* layer (child / foreign-key side) to a
    *referenced* layer (parent / primary-key side) through one or more field
    pairs.
    """

    def __init__(self):
        self._id: str = ""
        self._name: str = ""
        self._referencing_layer_id: str = ""
        self._referenced_layer_id: str = ""
        self._field_pairs: list[tuple[str, str]] = []

    # ------------------------------------------------------------------
    # Getters / setters
    # ------------------------------------------------------------------

    def id(self) -> str:
        """Return the unique relation identifier."""
        return self._id

    def setId(self, relation_id: str):
        """Set the unique relation identifier."""
        self._id = relation_id

    def name(self) -> str:
        """Return the human-readable name for this relation."""
        return self._name

    def setName(self, name: str):
        """Set the human-readable name for this relation."""
        self._name = name

    def referencingLayer(self) -> str:
        """Return the layer ID of the referencing (child) layer."""
        return self._referencing_layer_id

    def setReferencingLayer(self, layer_id: str):
        """Set the layer ID of the referencing (child) layer."""
        self._referencing_layer_id = layer_id

    def referencedLayer(self) -> str:
        """Return the layer ID of the referenced (parent) layer."""
        return self._referenced_layer_id

    def setReferencedLayer(self, layer_id: str):
        """Set the layer ID of the referenced (parent) layer."""
        self._referenced_layer_id = layer_id

    # ------------------------------------------------------------------
    # Field pairs
    # ------------------------------------------------------------------

    def addFieldPair(self, referencing_field: str, referenced_field: str):
        """Append a (referencing_field, referenced_field) pair."""
        self._field_pairs.append((referencing_field, referenced_field))

    def fieldPairs(self) -> list[tuple[str, str]]:
        """Return the list of field pair tuples."""
        return list(self._field_pairs)

    # ------------------------------------------------------------------
    # Validation
    # ------------------------------------------------------------------

    def isValid(self) -> bool:
        """Return *True* when the relation has all required attributes."""
        return bool(
            self._id
            and self._referencing_layer_id
            and self._referenced_layer_id
            and self._field_pairs
        )


class QgsRelationManager:
    """A registry of :class:`QgsRelation` instances.

    Relations are keyed by their unique ID.  The manager also supports
    querying which relations involve a given layer, optionally filtered by
    whether the layer plays the referencing or referenced role.
    """

    class LayerRole:
        """Enum-like constants identifying the role a layer plays."""
        ReferencingLayer = 0
        ReferencedLayer = 1

    # Convenience aliases so callers can write QgsRelationManager.ReferencedLayer
    ReferencingLayer = 0
    ReferencedLayer = 1

    def __init__(self):
        self._relations: dict[str, QgsRelation] = {}

    # ------------------------------------------------------------------
    # Mutators
    # ------------------------------------------------------------------

    def addRelation(self, relation: QgsRelation):
        """Register *relation* in the manager, keyed by its ID."""
        self._relations[relation.id()] = relation

    def removeRelation(self, relation_id: str):
        """Remove the relation with the given ID (no-op if not found)."""
        self._relations.pop(relation_id, None)

    def clear(self):
        """Remove all registered relations."""
        self._relations.clear()

    # ------------------------------------------------------------------
    # Query
    # ------------------------------------------------------------------

    def relation(self, relation_id: str) -> QgsRelation | None:
        """Return the relation with the given ID, or *None* if not found."""
        return self._relations.get(relation_id)

    def relations(self) -> dict[str, QgsRelation]:
        """Return a dict of all registered relations keyed by ID."""
        return dict(self._relations)

    def relationsForLayer(self, layer_id: str, role: int | None = None) -> list[QgsRelation]:
        """Return relations that involve *layer_id*.

        Parameters
        ----------
        layer_id:
            The ID of the layer to query.
        role:
            Optional filter — one of ``LayerRole.ReferencingLayer`` or
            ``LayerRole.ReferencedLayer``.  When *None*, any role is accepted.
        """
        results: list[QgsRelation] = []
        for rel in self._relations.values():
            if role is None:
                if rel.referencingLayer() == layer_id or rel.referencedLayer() == layer_id:
                    results.append(rel)
            elif role == self.LayerRole.ReferencingLayer:
                if rel.referencingLayer() == layer_id:
                    results.append(rel)
            elif role == self.LayerRole.ReferencedLayer:
                if rel.referencedLayer() == layer_id:
                    results.append(rel)
        return results
