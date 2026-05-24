from core.qgspointxy import QgsPointXY


class QgsRectangle:
    __slots__ = ('_xmin', '_ymin', '_xmax', '_ymax')

    def __init__(self, xmin: float = 0.0, ymin: float = 0.0, xmax: float = 0.0, ymax: float = 0.0):
        self._xmin = float(xmin)
        self._ymin = float(ymin)
        self._xmax = float(xmax)
        self._ymax = float(ymax)

    def xMinimum(self) -> float:
        return self._xmin

    def xMaximum(self) -> float:
        return self._xmax

    def yMinimum(self) -> float:
        return self._ymin

    def yMaximum(self) -> float:
        return self._ymax

    def width(self) -> float:
        return self._xmax - self._xmin

    def height(self) -> float:
        return self._ymax - self._ymin

    def area(self) -> float:
        return self.width() * self.height()

    def center(self) -> QgsPointXY:
        return QgsPointXY(
            (self._xmin + self._xmax) / 2.0,
            (self._ymin + self._ymax) / 2.0
        )

    def isEmpty(self) -> bool:
        return self._xmin == 0.0 and self._ymin == 0.0 and self._xmax == 0.0 and self._ymax == 0.0

    def isNull(self) -> bool:
        return self.isEmpty()

    def contains(self, point: QgsPointXY) -> bool:
        return (self._xmin <= point.x() <= self._xmax and
                self._ymin <= point.y() <= self._ymax)

    def containsRect(self, rect: 'QgsRectangle') -> bool:
        return (self._xmin <= rect._xmin and self._xmax >= rect._xmax and
                self._ymin <= rect._ymin and self._ymax >= rect._ymax)

    def intersects(self, rect: 'QgsRectangle') -> bool:
        return not (rect._xmin > self._xmax or rect._xmax < self._xmin or
                    rect._ymin > self._ymax or rect._ymax < self._ymin)

    def intersect(self, rect: 'QgsRectangle') -> 'QgsRectangle':
        if not self.intersects(rect):
            return QgsRectangle()
        return QgsRectangle(
            max(self._xmin, rect._xmin),
            max(self._ymin, rect._ymin),
            min(self._xmax, rect._xmax),
            min(self._ymax, rect._ymax)
        )

    def united(self, rect: 'QgsRectangle') -> 'QgsRectangle':
        return QgsRectangle(
            min(self._xmin, rect._xmin),
            min(self._ymin, rect._ymin),
            max(self._xmax, rect._xmax),
            max(self._ymax, rect._ymax)
        )

    def grow(self, delta: float):
        self._xmin -= delta
        self._ymin -= delta
        self._xmax += delta
        self._ymax += delta

    def normalize(self):
        if self._xmin > self._xmax:
            self._xmin, self._xmax = self._xmax, self._xmin
        if self._ymin > self._ymax:
            self._ymin, self._ymax = self._ymax, self._ymin

    def asWkt(self) -> str:
        return (f"POLYGON(({self._xmin} {self._ymin}, {self._xmax} {self._ymin}, "
                f"{self._xmax} {self._ymax}, {self._xmin} {self._ymax}, {self._xmin} {self._ymin}))")

    @staticmethod
    def fromCenterAndSize(center: QgsPointXY, width: float, height: float) -> 'QgsRectangle':
        hw = width / 2.0
        hh = height / 2.0
        return QgsRectangle(center.x() - hw, center.y() - hh, center.x() + hw, center.y() + hh)

    def __eq__(self, other):
        if not isinstance(other, QgsRectangle):
            return NotImplemented
        return (self._xmin == other._xmin and self._ymin == other._ymin and
                self._xmax == other._xmax and self._ymax == other._ymax)

    def __repr__(self):
        return f"QgsRectangle({self._xmin}, {self._ymin}, {self._xmax}, {self._ymax})"
