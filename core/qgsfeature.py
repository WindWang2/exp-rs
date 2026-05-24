"""QgsFeature - core data record holding attributes and geometry."""

from core.qgsfields import QgsFields
from core.qgsgeometry import QgsGeometry


class QgsFeature:
    """Represents a single vector feature with attributes and optional geometry.

    Mirrors the QGIS QgsFeature API for the core data model.
    """

    __slots__ = ('_id', '_fields', '_attributes', '_geometry', '_valid')

    def __init__(self, fields: QgsFields = None, id: int = 0):
        self._id = id
        self._fields = fields if fields else QgsFields()
        self._attributes = [None] * self._fields.count()
        self._geometry = QgsGeometry()
        self._valid = True

    def id(self) -> int:
        return self._id

    def setId(self, id: int):
        self._id = id

    def fields(self) -> QgsFields:
        return self._fields

    def setFields(self, fields: QgsFields):
        self._fields = fields
        if len(self._attributes) != fields.count():
            self._attributes = list(self._attributes) + [None] * max(0, fields.count() - len(self._attributes))

    def attributes(self) -> list:
        return list(self._attributes)

    def attribute(self, index_or_name) -> object:
        if isinstance(index_or_name, str):
            index = self._fields.indexOf(index_or_name)
            if index < 0:
                return None
            return self._attributes[index]
        return self._attributes[index_or_name]

    def setAttribute(self, index_or_name, value) -> bool:
        if isinstance(index_or_name, str):
            index = self._fields.indexOf(index_or_name)
            if index < 0:
                return False
        else:
            index = index_or_name
        if index < 0 or index >= len(self._attributes):
            return False
        self._attributes[index] = value
        return True

    def geometry(self) -> QgsGeometry:
        return self._geometry

    def setGeometry(self, geom: QgsGeometry):
        self._geometry = geom

    def isValid(self) -> bool:
        return self._valid

    def attributeCount(self) -> int:
        return len(self._attributes)

    def __repr__(self):
        return f"QgsFeature(id={self._id}, attrs={len(self._attributes)})"
