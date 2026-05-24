"""QgsField - describes a single attribute field (column) in a feature's schema."""


class QgsField:
    """Describes a single attribute field such as its name, type, length and precision.

    Mirrors the QGIS QgsField API for schema metadata on feature attributes.
    """

    __slots__ = ('_name', '_type', '_type_name', '_length', '_precision', '_comment', '_alias')

    def __init__(self, name: str = "", type: type = str, type_name: str = "",
                 length: int = 0, precision: int = 0, comment: str = "", alias: str = ""):
        self._name = name
        self._type = type
        self._type_name = type_name or type.__name__
        self._length = length
        self._precision = precision
        self._comment = comment
        self._alias = alias

    def name(self) -> str:
        """Return the name of this field."""
        return self._name

    def type(self) -> type:
        """Return the Python type of this field."""
        return self._type

    def typeName(self) -> str:
        """Return the type name string (e.g. 'int', 'str', 'Real')."""
        return self._type_name

    def length(self) -> int:
        """Return the maximum length of this field (0 means unlimited)."""
        return self._length

    def precision(self) -> int:
        """Return the numeric precision (number of decimal places)."""
        return self._precision

    def comment(self) -> str:
        """Return the comment/description for this field."""
        return self._comment

    def alias(self) -> str:
        """Return the human-friendly alias for this field."""
        return self._alias

    def isNumeric(self) -> bool:
        """Return True if the field type is numeric (int or float)."""
        return self._type in (int, float)

    def __eq__(self, other):
        if not isinstance(other, QgsField):
            return NotImplemented
        return self._name == other._name and self._type == other._type

    def __repr__(self):
        return f"QgsField({self._name}, {self._type.__name__})"
