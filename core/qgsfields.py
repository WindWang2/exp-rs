"""QgsFields - ordered collection of QgsField objects representing a feature's attribute schema."""

from core.qgsfield import QgsField


class QgsFields:
    """An ordered collection of QgsField objects.

    Represents the attribute schema of a vector layer or feature, maintaining
    the order in which fields were added.
    """

    __slots__ = ('_fields',)

    def __init__(self):
        self._fields: list[QgsField] = []

    def append(self, field: QgsField):
        """Add a field to the end of the collection."""
        self._fields.append(field)

    def count(self) -> int:
        """Return the number of fields in the collection."""
        return len(self._fields)

    def indexOf(self, name: str) -> int:
        """Return the index of the field with the given name, or -1 if not found."""
        for i, f in enumerate(self._fields):
            if f.name() == name:
                return i
        return -1

    def at(self, i: int) -> QgsField:
        """Return the field at index i."""
        return self._fields[i]

    def field(self, name: str) -> QgsField:
        """Return the first field with the given name, or None if not found."""
        for f in self._fields:
            if f.name() == name:
                return f
        return None

    def names(self) -> list[str]:
        """Return a list of all field names in order."""
        return [f.name() for f in self._fields]

    def isEmpty(self) -> bool:
        """Return True if the collection contains no fields."""
        return len(self._fields) == 0

    def extend(self, other: 'QgsFields'):
        """Add all fields from another QgsFields collection to the end."""
        self._fields.extend(other._fields)

    def __len__(self):
        return len(self._fields)

    def __iter__(self):
        return iter(self._fields)

    def __getitem__(self, index):
        return self._fields[index]

    def __repr__(self):
        return f"QgsFields({len(self._fields)} fields)"
