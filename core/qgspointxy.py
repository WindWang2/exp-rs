import math


class QgsPointXY:
    __slots__ = ('_x', '_y')

    def __init__(self, x: float = 0.0, y: float = 0.0):
        self._x = float(x)
        self._y = float(y)

    def x(self) -> float:
        return self._x

    def y(self) -> float:
        return self._y

    def setX(self, x: float):
        self._x = float(x)

    def setY(self, y: float):
        self._y = float(y)

    def distance(self, other: 'QgsPointXY') -> float:
        dx = self._x - other._x
        dy = self._y - other._y
        return math.sqrt(dx * dx + dy * dy)

    def distanceSquared(self, other: 'QgsPointXY') -> float:
        dx = self._x - other._x
        dy = self._y - other._y
        return dx * dx + dy * dy

    def azimuth(self, other: 'QgsPointXY') -> float:
        dx = other._x - self._x
        dy = other._y - self._y
        return math.degrees(math.atan2(dx, dy)) % 360.0

    def isEmpty(self) -> bool:
        return self._x == 0.0 and self._y == 0.0

    def isNull(self) -> bool:
        return self.isEmpty()

    def __eq__(self, other):
        if not isinstance(other, QgsPointXY):
            return NotImplemented
        return self._x == other._x and self._y == other._y

    def __hash__(self):
        return hash((self._x, self._y))

    def __repr__(self):
        return f"QgsPointXY({self._x}, {self._y})"

    def __add__(self, other: 'QgsPointXY') -> 'QgsPointXY':
        return QgsPointXY(self._x + other._x, self._y + other._y)

    def __sub__(self, other: 'QgsPointXY') -> 'QgsPointXY':
        return QgsPointXY(self._x - other._x, self._y - other._y)

    def toTuple(self) -> tuple:
        return (self._x, self._y)
