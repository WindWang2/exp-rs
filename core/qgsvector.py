import math


class QgsVector:
    __slots__ = ('_x', '_y')

    def __init__(self, x: float = 0.0, y: float = 0.0):
        self._x = float(x)
        self._y = float(y)

    def x(self) -> float:
        return self._x

    def y(self) -> float:
        return self._y

    def length(self) -> float:
        return math.sqrt(self._x * self._x + self._y * self._y)

    def normalized(self) -> 'QgsVector':
        l = self.length()
        if l == 0:
            return QgsVector(0, 0)
        return QgsVector(self._x / l, self._y / l)

    def __add__(self, other: 'QgsVector') -> 'QgsVector':
        return QgsVector(self._x + other._x, self._y + other._y)

    def __sub__(self, other: 'QgsVector') -> 'QgsVector':
        return QgsVector(self._x - other._x, self._y - other._y)

    def __mul__(self, scalar: float) -> 'QgsVector':
        return QgsVector(self._x * scalar, self._y * scalar)

    def __neg__(self) -> 'QgsVector':
        return QgsVector(-self._x, -self._y)

    def __eq__(self, other):
        if not isinstance(other, QgsVector):
            return NotImplemented
        return self._x == other._x and self._y == other._y

    def __repr__(self):
        return f"QgsVector({self._x}, {self._y})"
