# QGIS Core Framework Port — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port QGIS's complete core framework to Python/PySide6, achieving identical rendering performance through numpy/rasterio/Shapely backends with multithreaded tiled rendering.

**Architecture:** 13-tier dependency model. Each tier only depends on tiers below it. Tiers 1-4 are pure data types with no Qt dependency (except QgsCoordinateReferenceSystem which uses QObject for signals). Tiers 5-13 add providers, layers, rendering, and GUI.

**Tech Stack:** Python 3.13+, PySide6, numpy, rasterio, fiona, shapely, pyproj, GDAL

**Phasing:** This plan is organized into 5 phases. Each phase produces working, testable software.

- **Phase 1** (This document): Foundation — Tiers 1-4 (Primitives, Geometry, Data Model, CRS)
- **Phase 2**: Data Layer — Tiers 5-6 (Providers, Layers)
- **Phase 3**: Rendering Core — Tiers 7-9 (MapSettings, Symbology, Renderer Jobs)
- **Phase 4**: Canvas & Interaction — Tier 10 (MapCanvas, MapTools)
- **Phase 5**: Project & Polish — Tiers 11-13 (Project, Labeling, Decorations)

---

## Phase 1: Foundation (Tiers 1-4)

### File Map

```
core/
├── qgis.py                          # Qgis namespace enums (NEW)
├── qgspointxy.py                    # QgsPointXY (NEW)
├── qgsrectangle.py                  # QgsRectangle (NEW)
├── qgsvector.py                     # QgsVector (NEW)
├── qgswkbtypes.py                   # QgsWkbTypes (NEW)
├── qgsunittypes.py                  # QgsUnitTypes (NEW)
├── qgsgeometry.py                   # QgsGeometry wrapping Shapely (NEW)
├── qgsfield.py                      # QgsField (NEW)
├── qgsfields.py                     # QgsFields (NEW)
├── qgsfeature.py                    # QgsFeature (NEW)
├── qgsfeaturerequest.py             # QgsFeatureRequest (NEW)
├── qgsfeatureiterator.py            # QgsFeatureIterator (NEW)
├── qgsfeaturesource.py              # QgsFeatureSource ABC (NEW)
├── qgsfeaturesink.py                # QgsFeatureSink ABC (NEW)
├── qgscoordinatereferencesystem.py  # QgsCoordinateReferenceSystem (NEW)
├── qgscoordinatetransform.py        # QgsCoordinateTransform (REWRITE)
├── qgscoordinatetransformcontext.py # QgsCoordinateTransformContext (NEW)
tests/
├── test_primitives.py               # Tests for Tier 1 (NEW)
├── test_geometry.py                 # Tests for Tier 2 (NEW)
├── test_data_model.py               # Tests for Tier 3 (NEW)
├── test_crs.py                      # Tests for Tier 4 (NEW)
```

---

### Task 1: Qgis Namespace Enums

**Files:**
- Create: `core/qgis.py`
- Test: `tests/test_primitives.py`

- [ ] **Step 1: Write failing tests for Qgis enums**

```python
# tests/test_primitives.py
from core.qgis import Qgis

def test_geometry_type_enum():
    assert Qgis.GeometryType.Point == 0
    assert Qgis.GeometryType.Line == 1
    assert Qgis.GeometryType.Polygon == 2

def test_layer_type_enum():
    assert Qgis.LayerType.Raster == 0
    assert Qgis.LayerType.Vector == 1

def test_data_type_enum():
    assert Qgis.DataType.Byte == 1
    assert Qgis.DataType.UInt16 == 2
    assert Qgis.DataType.Float32 == 6

def test_distance_unit_enum():
    assert Qgis.DistanceUnit.Meters == 0
    assert Qgis.DistanceUnit.Degrees == 1

def test_raster_layer_type_enum():
    assert Qgis.RasterLayerType.GrayOrUndefined == 0
    assert Qgis.RasterLayerType.Multiband == 1
    assert Qgis.RasterLayerType.Palette == 2
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: FAIL (module not found)

- [ ] **Step 3: Implement Qgis namespace**

```python
# core/qgis.py
import enum

class Qgis:
    class GeometryType(enum.IntEnum):
        Point = 0
        Line = 1
        Polygon = 2
        Unknown = 100
        Null = 101

    class LayerType(enum.IntEnum):
        Raster = 0
        Vector = 1
        Plugin = 2
        Mesh = 3

    class DataType(enum.IntEnum):
        Byte = 1
        UInt16 = 2
        Int16 = 3
        UInt32 = 4
        Int32 = 5
        Float32 = 6
        Float64 = 7
        CInt16 = 8
        CInt32 = 9
        CFloat32 = 10
        CFloat64 = 11
        ARGB32 = 12
        ARGB32_Premultiplied = 13

    class DistanceUnit(enum.IntEnum):
        Meters = 0
        Kilometers = 10
        Degrees = 1
        Feet = 2
        NauticalMiles = 3
        Yards = 4
        Miles = 5
        DegreesMinutesSeconds = 6
        UnknownUnit = 7

    class RasterLayerType(enum.IntEnum):
        GrayOrUndefined = 0
        Multiband = 1
        Palette = 2
        SingleBandColorData = 3
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgis.py tests/test_primitives.py
git commit -m "feat(core): add Qgis namespace enums (Tier 1)"
```

---

### Task 2: QgsPointXY

**Files:**
- Create: `core/qgspointxy.py`
- Modify: `tests/test_primitives.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_primitives.py`:

```python
from core.qgspointxy import QgsPointXY
import math

def test_point_creation():
    p = QgsPointXY(1.0, 2.0)
    assert p.x() == 1.0
    assert p.y() == 2.0

def test_point_default():
    p = QgsPointXY()
    assert p.x() == 0.0
    assert p.y() == 0.0

def test_point_setters():
    p = QgsPointXY()
    p.setX(5.0)
    p.setY(10.0)
    assert p.x() == 5.0
    assert p.y() == 10.0

def test_point_distance():
    p1 = QgsPointXY(0, 0)
    p2 = QgsPointXY(3, 4)
    assert p1.distance(p2) == 5.0

def test_point_is_null():
    p = QgsPointXY()
    assert p.isEmpty()

def test_point_equality():
    p1 = QgsPointXY(1, 2)
    p2 = QgsPointXY(1, 2)
    assert p1 == p2

def test_point_repr():
    p = QgsPointXY(1.5, 2.5)
    assert "1.5" in repr(p)
    assert "2.5" in repr(p)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_primitives.py::test_point_creation -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsPointXY**

```python
# core/qgspointxy.py
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
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgspointxy.py tests/test_primitives.py
git commit -m "feat(core): add QgsPointXY primitive (Tier 1)"
```

---

### Task 3: QgsRectangle

**Files:**
- Create: `core/qgsrectangle.py`
- Modify: `tests/test_primitives.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_primitives.py`:

```python
from core.qgsrectangle import QgsRectangle

def test_rect_creation():
    r = QgsRectangle(0, 0, 10, 10)
    assert r.xMinimum() == 0.0
    assert r.yMinimum() == 0.0
    assert r.xMaximum() == 10.0
    assert r.yMaximum() == 10.0

def test_rect_dimensions():
    r = QgsRectangle(0, 0, 10, 5)
    assert r.width() == 10.0
    assert r.height() == 5.0
    assert r.area() == 50.0

def test_rect_center():
    r = QgsRectangle(0, 0, 10, 10)
    c = r.center()
    assert c.x() == 5.0
    assert c.y() == 5.0

def test_rect_contains():
    r = QgsRectangle(0, 0, 10, 10)
    from core.qgspointxy import QgsPointXY
    assert r.contains(QgsPointXY(5, 5))
    assert not r.contains(QgsPointXY(15, 5))

def test_rect_intersects():
    r1 = QgsRectangle(0, 0, 10, 10)
    r2 = QgsRectangle(5, 5, 15, 15)
    r3 = QgsRectangle(20, 20, 30, 30)
    assert r1.intersects(r2)
    assert not r1.intersects(r3)

def test_rect_is_empty():
    r = QgsRectangle()
    assert r.isEmpty()

def test_rect_normalize():
    r = QgsRectangle(10, 10, 0, 0)
    r.normalize()
    assert r.xMinimum() == 0.0
    assert r.yMinimum() == 0.0

def test_rect_grow():
    r = QgsRectangle(2, 2, 8, 8)
    r.grow(2)
    assert r.xMinimum() == 0.0
    assert r.xMaximum() == 10.0

def test_rect_union():
    r1 = QgsRectangle(0, 0, 5, 5)
    r2 = QgsRectangle(3, 3, 10, 10)
    u = r1.united(r2)
    assert u.xMinimum() == 0.0
    assert u.xMaximum() == 10.0

def test_rect_from_point():
    from core.qgspointxy import QgsPointXY
    r = QgsRectangle.fromCenterAndSize(QgsPointXY(5, 5), 10, 10)
    assert r.width() == 10.0
    assert r.height() == 10.0
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_primitives.py::test_rect_creation -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsRectangle**

```python
# core/qgsrectangle.py
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
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsrectangle.py tests/test_primitives.py
git commit -m "feat(core): add QgsRectangle primitive (Tier 1)"
```

---

### Task 4: QgsVector and QgsWkbTypes

**Files:**
- Create: `core/qgsvector.py`
- Create: `core/qgswkbtypes.py`
- Modify: `tests/test_primitives.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_primitives.py`:

```python
from core.qgsvector import QgsVector
from core.qgswkbtypes import QgsWkbTypes

def test_vector_creation():
    v = QgsVector(3.0, 4.0)
    assert v.x() == 3.0
    assert v.y() == 4.0

def test_vector_length():
    v = QgsVector(3.0, 4.0)
    assert v.length() == 5.0

def test_vector_normalized():
    v = QgsVector(3.0, 4.0)
    n = v.normalized()
    assert abs(n.x() - 0.6) < 1e-10
    assert abs(n.y() - 0.8) < 1e-10

def test_wkb_types():
    assert QgsWkbTypes.geometryType(QgsWkbTypes.Type.Point) == Qgis.GeometryType.Point
    assert QgsWkbTypes.geometryType(QgsWkbTypes.Type.Polygon) == Qgis.GeometryType.Polygon
    assert QgsWkbTypes.isMultiType(QgsWkbTypes.Type.MultiPoint)
    assert not QgsWkbTypes.isMultiType(QgsWkbTypes.Type.Point)
    assert QgsWkbTypes.hasZ(QgsWkbTypes.Type.PointZ)
    assert not QgsWkbTypes.hasZ(QgsWkbTypes.Type.Point)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_primitives.py::test_vector_creation -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsVector and QgsWkbTypes**

```python
# core/qgsvector.py
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
```

```python
# core/qgswkbtypes.py
import enum
from core.qgis import Qgis

class QgsWkbTypes:
    class Type(enum.IntEnum):
        Unknown = 0
        Point = 1
        LineString = 2
        Polygon = 3
        Triangle = 17
        MultiPoint = 4
        MultiLineString = 5
        MultiPolygon = 6
        GeometryCollection = 7
        CircularString = 8
        CompoundCurve = 9
        CurvePolygon = 10
        MultiCurve = 11
        MultiSurface = 12
        NoGeometry = 100
        PointZ = 1001
        LineStringZ = 1002
        PolygonZ = 1003
        MultiPointZ = 1004
        MultiLineStringZ = 1005
        MultiPolygonZ = 1006
        PointM = 2001
        LineStringM = 2002
        PolygonM = 2003
        PointZM = 3001
        LineStringZM = 3002
        PolygonZM = 3003

    @staticmethod
    def flatType(wkb_type: int) -> int:
        return wkb_type % 1000

    @staticmethod
    def geometryType(wkb_type: int) -> Qgis.GeometryType:
        flat = QgsWkbTypes.flatType(wkb_type)
        if flat in (1, 4, 17):
            return Qgis.GeometryType.Point
        elif flat in (2, 5, 8, 9, 11):
            return Qgis.GeometryType.Line
        elif flat in (3, 6, 10, 12):
            return Qgis.GeometryType.Polygon
        elif flat == 7:
            return Qgis.GeometryType.Unknown
        elif flat == 100:
            return Qgis.GeometryType.Null
        return Qgis.GeometryType.Unknown

    @staticmethod
    def isMultiType(wkb_type: int) -> bool:
        flat = QgsWkbTypes.flatType(wkb_type)
        return flat in (4, 5, 6, 7, 11, 12)

    @staticmethod
    def hasZ(wkb_type: int) -> bool:
        return wkb_type >= 1000 and wkb_type < 2000

    @staticmethod
    def hasM(wkb_type: int) -> bool:
        return (wkb_type >= 2000 and wkb_type < 3000) or wkb_type >= 3000
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsvector.py core/qgswkbtypes.py tests/test_primitives.py
git commit -m "feat(core): add QgsVector and QgsWkbTypes (Tier 1)"
```

---

### Task 5: QgsUnitTypes

**Files:**
- Create: `core/qgsunittypes.py`
- Modify: `tests/test_primitives.py`

- [ ] **Step 1: Write failing tests**

```python
# Append to tests/test_primitives.py
from core.qgsunittypes import QgsUnitTypes

def test_distance_unit_conversion():
    # 1 degree ≈ 111319.490793 m at equator
    result = QgsUnitTypes.fromUnitToUnitFactor(Qgis.DistanceUnit.Degrees, Qgis.DistanceUnit.Meters)
    assert result > 100000  # approximate

def test_area_unit_enum():
    assert QgsUnitTypes.AreaUnit.SquareMeters == 0
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_primitives.py::test_distance_unit_conversion -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsUnitTypes**

```python
# core/qgsunittypes.py
import enum
from core.qgis import Qgis

class QgsUnitTypes:
    class AreaUnit(enum.IntEnum):
        SquareMeters = 0
        SquareKilometers = 1
        SquareFeet = 2
        SquareYards = 3
        SquareMiles = 4
        Hectares = 5
        Acres = 6

    # Conversion factors to meters
    _DISTANCE_TO_METERS = {
        Qgis.DistanceUnit.Meters: 1.0,
        Qgis.DistanceUnit.Kilometers: 1000.0,
        Qgis.DistanceUnit.Feet: 0.3048,
        Qgis.DistanceUnit.Yards: 0.9144,
        Qgis.DistanceUnit.Miles: 1609.344,
        Qgis.DistanceUnit.NauticalMiles: 1852.0,
    }

    @staticmethod
    def fromUnitToUnitFactor(from_unit: Qgis.DistanceUnit, to_unit: Qgis.DistanceUnit) -> float:
        if from_unit == to_unit:
            return 1.0
        from_factor = QgsUnitTypes._DISTANCE_TO_METERS.get(from_unit, 1.0)
        to_factor = QgsUnitTypes._DISTANCE_TO_METERS.get(to_unit, 1.0)
        return from_factor / to_factor

    @staticmethod
    def toAbbreviatedString(unit: Qgis.DistanceUnit) -> str:
        abbreviations = {
            Qgis.DistanceUnit.Meters: "m",
            Qgis.DistanceUnit.Kilometers: "km",
            Qgis.DistanceUnit.Feet: "ft",
            Qgis.DistanceUnit.Degrees: "deg",
        }
        return abbreviations.get(unit, "")
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_primitives.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsunittypes.py tests/test_primitives.py
git commit -m "feat(core): add QgsUnitTypes (Tier 1)"
```

---

### Task 6: QgsGeometry (wrapping Shapely)

**Files:**
- Create: `core/qgsgeometry.py`
- Test: `tests/test_geometry.py`

- [ ] **Step 1: Write failing tests**

```python
# tests/test_geometry.py
from core.qgsgeometry import QgsGeometry
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle

def test_geometry_from_point():
    g = QgsGeometry.fromPointXY(QgsPointXY(1, 2))
    assert not g.isNull()
    assert not g.isEmpty()

def test_geometry_from_wkt():
    g = QgsGeometry.fromWkt("POINT(1 2)")
    assert not g.isNull()

def test_geometry_area():
    g = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    assert g.area() == 100.0

def test_geometry_length():
    g = QgsGeometry.fromWkt("LINESTRING(0 0, 3 4)")
    assert g.length() == 5.0

def test_geometry_centroid():
    g = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    c = g.centroid()
    assert c.asPoint().x() == 5.0
    assert c.asPoint().y() == 5.0

def test_geometry_bbox():
    g = QgsGeometry.fromWkt("POINT(5 10)")
    bb = g.boundingBox()
    assert bb.xMinimum() == 5.0
    assert bb.yMaximum() == 10.0

def test_geometry_buffer():
    g = QgsGeometry.fromWkt("POINT(0 0)")
    b = g.buffer(1.0, 8)
    assert b.area() > 0

def test_geometry_contains():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POINT(5 5)")
    assert g1.contains(g2)

def test_geometry_intersects():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 5, 15 5, 15 15, 5 15, 5 5))")
    assert g1.intersects(g2)

def test_geometry_combine():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 5 0, 5 5, 0 5, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 0, 10 0, 10 5, 5 5, 5 0))")
    u = g1.combine(g2)
    assert u.area() == 50.0

def test_geometry_difference():
    g1 = QgsGeometry.fromWkt("POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))")
    g2 = QgsGeometry.fromWkt("POLYGON((5 5, 15 5, 15 15, 5 15, 5 5))")
    d = g1.difference(g2)
    assert d.area() < g1.area()

def test_geometry_as_wkt():
    g = QgsGeometry.fromWkt("POINT(1 2)")
    wkt = g.asWkt()
    assert "POINT" in wkt

def test_geometry_is_empty():
    g = QgsGeometry()
    assert g.isNull()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_geometry.py -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsGeometry**

```python
# core/qgsgeometry.py
import shapely
import shapely.wkt
import shapely.ops
from shapely.geometry import shape as shapely_shape, mapping
from shapely.geometry.base import BaseGeometry

from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle

class QgsGeometry:
    __slots__ = ('_geom',)

    def __init__(self, geom: BaseGeometry = None):
        self._geom = geom

    def isNull(self) -> bool:
        return self._geom is None

    def isEmpty(self) -> bool:
        if self._geom is None:
            return True
        return self._geom.is_empty

    def type(self):
        from core.qgis import Qgis
        if self._geom is None:
            return Qgis.GeometryType.Null
        gt = self._geom.geom_type
        if gt in ('Point', 'MultiPoint'):
            return Qgis.GeometryType.Point
        elif gt in ('LineString', 'MultiLineString', 'LinearRing'):
            return Qgis.GeometryType.Line
        elif gt in ('Polygon', 'MultiPolygon'):
            return Qgis.GeometryType.Polygon
        return Qgis.GeometryType.Unknown

    def area(self) -> float:
        if self._geom is None:
            return 0.0
        return self._geom.area

    def length(self) -> float:
        if self._geom is None:
            return 0.0
        return self._geom.length

    def centroid(self) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.centroid)

    def boundingBox(self) -> QgsRectangle:
        if self._geom is None:
            return QgsRectangle()
        b = self._geom.bounds  # (minx, miny, maxx, maxy)
        return QgsRectangle(b[0], b[1], b[2], b[3])

    def buffer(self, distance: float, segments: int = 8) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.buffer(distance, segments))

    def simplify(self, tolerance: float) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.simplify(tolerance))

    def makeValid(self) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        return QgsGeometry(shapely.validation.make_valid(self._geom))

    def contains(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.contains(other._geom)

    def intersects(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.intersects(other._geom)

    def disjoint(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return True
        return self._geom.disjoint(other._geom)

    def within(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.within(other._geom)

    def crosses(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.crosses(other._geom)

    def touches(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.touches(other._geom)

    def overlaps(self, other: 'QgsGeometry') -> bool:
        if self._geom is None or other._geom is None:
            return False
        return self._geom.overlaps(other._geom)

    def combine(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None:
            return other
        if other._geom is None:
            return self
        return QgsGeometry(self._geom.union(other._geom))

    def difference(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        if other._geom is None:
            return self
        return QgsGeometry(self._geom.difference(other._geom))

    def intersection(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None or other._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.intersection(other._geom))

    def symDifference(self, other: 'QgsGeometry') -> 'QgsGeometry':
        if self._geom is None or other._geom is None:
            return QgsGeometry()
        return QgsGeometry(self._geom.symmetric_difference(other._geom))

    def transform(self, ct) -> 'QgsGeometry':
        if self._geom is None:
            return QgsGeometry()
        from shapely.ops import transform as shapely_transform
        return QgsGeometry(shapely_transform(ct.transform, self._geom))

    def asWkt(self) -> str:
        if self._geom is None:
            return ""
        return self._geom.wkt

    def asWkb(self) -> bytes:
        if self._geom is None:
            return b""
        return shapely.to_wkb(self._geom)

    def asPoint(self) -> QgsPointXY:
        if self._geom is None:
            return QgsPointXY()
        return QgsPointXY(self._geom.x, self._geom.y)

    def asPolygon(self) -> list:
        if self._geom is None:
            return []
        if self._geom.geom_type == 'Polygon':
            return [[QgsPointXY(x, y) for x, y in self._geom.exterior.coords]]
        return []

    @staticmethod
    def fromWkt(wkt: str) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely.wkt.loads(wkt))
        except Exception:
            return QgsGeometry()

    @staticmethod
    def fromWkb(wkb: bytes) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely.from_wkb(wkb))
        except Exception:
            return QgsGeometry()

    @staticmethod
    def fromPointXY(point: QgsPointXY) -> 'QgsGeometry':
        from shapely.geometry import Point
        return QgsGeometry(Point(point.x(), point.y()))

    @staticmethod
    def fromPolyline(points: list) -> 'QgsGeometry':
        from shapely.geometry import LineString
        return QgsGeometry(LineString([(p.x(), p.y()) for p in points]))

    @staticmethod
    def fromPolygon(rings: list) -> 'QgsGeometry':
        from shapely.geometry import Polygon
        exterior = [(p.x(), p.y()) for p in rings[0]]
        holes = [[(p.x(), p.y()) for p in ring] for ring in rings[1:]] if len(rings) > 1 else []
        return QgsGeometry(Polygon(exterior, holes))

    @staticmethod
    def fromJson(geojson: dict) -> 'QgsGeometry':
        try:
            return QgsGeometry(shapely_shape(geojson))
        except Exception:
            return QgsGeometry()

    def __eq__(self, other):
        if not isinstance(other, QgsGeometry):
            return NotImplemented
        if self._geom is None and other._geom is None:
            return True
        if self._geom is None or other._geom is None:
            return False
        return self._geom.equals(other._geom)

    def __repr__(self):
        if self._geom is None:
            return "QgsGeometry(NULL)"
        return f"QgsGeometry({self._geom.geom_type})"
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_geometry.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsgeometry.py tests/test_geometry.py
git commit -m "feat(core): add QgsGeometry wrapping Shapely/GEOS (Tier 2)"
```

---

### Task 7: QgsField and QgsFields

**Files:**
- Create: `core/qgsfield.py`
- Create: `core/qgsfields.py`
- Test: `tests/test_data_model.py`

- [ ] **Step 1: Write failing tests**

```python
# tests/test_data_model.py
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

def test_fields_names():
    fs = QgsFields()
    fs.append(QgsField("id", int))
    fs.append(QgsField("name", str))
    assert fs.names() == ["id", "name"]
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_data_model.py::test_field_creation -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsField and QgsFields**

```python
# core/qgsfield.py
class QgsField:
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
        return self._name

    def type(self) -> type:
        return self._type

    def typeName(self) -> str:
        return self._type_name

    def length(self) -> int:
        return self._length

    def precision(self) -> int:
        return self._precision

    def comment(self) -> str:
        return self._comment

    def alias(self) -> str:
        return self._alias

    def isNumeric(self) -> bool:
        return self._type in (int, float)

    def __eq__(self, other):
        if not isinstance(other, QgsField):
            return NotImplemented
        return self._name == other._name and self._type == other._type

    def __repr__(self):
        return f"QgsField({self._name}, {self._type.__name__})"
```

```python
# core/qgsfields.py
from core.qgsfield import QgsField

class QgsFields:
    __slots__ = ('_fields',)

    def __init__(self):
        self._fields: list[QgsField] = []

    def append(self, field: QgsField):
        self._fields.append(field)

    def count(self) -> int:
        return len(self._fields)

    def indexOf(self, name: str) -> int:
        for i, f in enumerate(self._fields):
            if f.name() == name:
                return i
        return -1

    def at(self, i: int) -> QgsField:
        return self._fields[i]

    def field(self, name: str) -> QgsField:
        for f in self._fields:
            if f.name() == name:
                return f
        return None

    def names(self) -> list[str]:
        return [f.name() for f in self._fields]

    def isEmpty(self) -> bool:
        return len(self._fields) == 0

    def extend(self, other: 'QgsFields'):
        self._fields.extend(other._fields)

    def __len__(self):
        return len(self._fields)

    def __iter__(self):
        return iter(self._fields)

    def __getitem__(self, index):
        return self._fields[index]

    def __repr__(self):
        return f"QgsFields({len(self._fields)} fields)"
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_data_model.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsfield.py core/qgsfields.py tests/test_data_model.py
git commit -m "feat(core): add QgsField and QgsFields (Tier 3)"
```

---

### Task 8: QgsFeature

**Files:**
- Create: `core/qgsfeature.py`
- Modify: `tests/test_data_model.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_data_model.py`:

```python
from core.qgsfeature import QgsFeature
from core.qgsgeometry import QgsGeometry
from core.qgspointxy import QgsPointXY

def test_feature_creation():
    f = QgsFeature()
    assert f.id() == 0

def test_feature_with_fields():
    from core.qgsfields import QgsFields
    from core.qgsfield import QgsField
    fields = QgsFields()
    fields.append(QgsField("id", int))
    fields.append(QgsField("name", str))
    f = QgsFeature(fields)
    assert f.fields().count() == 2

def test_feature_attributes():
    from core.qgsfields import QgsFields
    from core.qgsfield import QgsField
    fields = QgsFields()
    fields.append(QgsField("id", int))
    fields.append(QgsField("name", str))
    f = QgsFeature(fields)
    f.setAttribute(0, 1)
    f.setAttribute(1, "test")
    assert f.attribute(0) == 1
    assert f.attribute("name") == "test"

def test_feature_geometry():
    f = QgsFeature()
    g = QgsGeometry.fromPointXY(QgsPointXY(1, 2))
    f.setGeometry(g)
    assert not f.geometry().isNull()

def test_feature_id():
    f = QgsFeature()
    f.setId(42)
    assert f.id() == 42
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_data_model.py::test_feature_creation -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsFeature**

```python
# core/qgsfeature.py
from core.qgsfields import QgsFields
from core.qgsgeometry import QgsGeometry

class QgsFeature:
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
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_data_model.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsfeature.py tests/test_data_model.py
git commit -m "feat(core): add QgsFeature (Tier 3)"
```

---

### Task 9: QgsFeatureRequest and QgsFeatureIterator

**Files:**
- Create: `core/qgsfeaturerequest.py`
- Create: `core/qgsfeatureiterator.py`
- Modify: `tests/test_data_model.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_data_model.py`:

```python
from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgsfeatureiterator import QgsFeatureIterator, QgsAbstractFeatureIterator
from core.qgsrectangle import QgsRectangle

def test_feature_request_no_filter():
    r = QgsFeatureRequest()
    assert r.filterType() == QgsFeatureRequest.FilterType.NoFilter

def test_feature_request_filter_rect():
    r = QgsFeatureRequest()
    r.setFilterRect(QgsRectangle(0, 0, 10, 10))
    assert r.filterType() == QgsFeatureRequest.FilterType.FilterRect

def test_feature_request_filter_fid():
    r = QgsFeatureRequest()
    r.setFilterFid(42)
    assert r.filterType() == QgsFeatureRequest.FilterType.FilterFid

def test_feature_request_subset():
    r = QgsFeatureRequest()
    r.setSubsetOfAttributes(["id", "name"])
    assert r.subsetOfAttributes() == ["id", "name"]

def test_feature_iterator_from_list():
    from core.qgsfeature import QgsFeature
    feats = [QgsFeature(id=i) for i in range(5)]
    it = QgsFeatureIterator(feats)
    collected = list(it)
    assert len(collected) == 5
    assert collected[0].id() == 0
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_data_model.py::test_feature_request_no_filter -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsFeatureRequest and QgsFeatureIterator**

```python
# core/qgsfeaturerequest.py
import enum

class QgsFeatureRequest:
    class FilterType(enum.IntEnum):
        NoFilter = 0
        FilterRect = 1
        FilterFid = 2
        FilterFids = 3
        FilterExpression = 4

    __slots__ = ('_filter_type', '_filter_rect', '_filter_fid', '_filter_fids',
                 '_subset_attributes', '_limit')

    def __init__(self):
        self._filter_type = self.FilterType.NoFilter
        self._filter_rect = None
        self._filter_fid = None
        self._filter_fids = None
        self._subset_attributes = None
        self._limit = -1

    def filterType(self) -> FilterType:
        return self._filter_type

    def filterRect(self):
        return self._filter_rect

    def setFilterRect(self, rect) -> 'QgsFeatureRequest':
        self._filter_type = self.FilterType.FilterRect
        self._filter_rect = rect
        return self

    def setFilterFid(self, fid: int) -> 'QgsFeatureRequest':
        self._filter_type = self.FilterType.FilterFid
        self._filter_fid = fid
        return self

    def setFilterFids(self, fids: set) -> 'QgsFeatureRequest':
        self._filter_type = self.FilterType.FilterFids
        self._filter_fids = fids
        return self

    def subsetOfAttributes(self) -> list:
        return self._subset_attributes

    def setSubsetOfAttributes(self, attrs: list) -> 'QgsFeatureRequest':
        self._subset_attributes = attrs
        return self

    def limit(self) -> int:
        return self._limit

    def setLimit(self, limit: int) -> 'QgsFeatureRequest':
        self._limit = limit
        return self
```

```python
# core/qgsfeatureiterator.py
from abc import ABC, abstractmethod
from core.qgsfeature import QgsFeature
from core.qgsfeaturerequest import QgsFeatureRequest

class QgsAbstractFeatureIterator(ABC):
    @abstractmethod
    def nextFeature(self) -> tuple:
        """Returns (True, feature) or (False, None) when exhausted."""
        pass

    def rewind(self):
        pass

    def close(self):
        pass

    def isValid(self) -> bool:
        return True

class QgsFeatureIterator:
    __slots__ = ('_features', '_index', '_closed')

    def __init__(self, features_or_iterator):
        if isinstance(features_or_iterator, list):
            self._features = features_or_iterator
            self._index = 0
        elif isinstance(features_or_iterator, QgsAbstractFeatureIterator):
            self._features = None
            self._index = 0
        else:
            self._features = []
            self._index = 0
        self._closed = False

    def __iter__(self):
        return self

    def __next__(self) -> QgsFeature:
        if self._closed:
            raise StopIteration
        if self._features is not None:
            if self._index >= len(self._features):
                raise StopIteration
            feat = self._features[self._index]
            self._index += 1
            return feat
        raise StopIteration

    def nextFeature(self) -> tuple:
        try:
            feat = self.__next__()
            return (True, feat)
        except StopIteration:
            return (False, None)

    def close(self):
        self._closed = True
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_data_model.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsfeaturerequest.py core/qgsfeatureiterator.py tests/test_data_model.py
git commit -m "feat(core): add QgsFeatureRequest and QgsFeatureIterator (Tier 3)"
```

---

### Task 10: QgsFeatureSource and QgsFeatureSink ABCs

**Files:**
- Create: `core/qgsfeaturesource.py`
- Create: `core/qgsfeaturesink.py`
- Modify: `tests/test_data_model.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_data_model.py`:

```python
from core.qgsfeaturesource import QgsFeatureSource
from core.qgsfeaturesink import QgsFeatureSink

def test_feature_source_is_abstract():
    # Cannot instantiate abstract class
    import pytest
    with pytest.raises(TypeError):
        QgsFeatureSink()

def test_concrete_feature_source():
    """Test a concrete implementation of QgsFeatureSource."""
    class MySource(QgsFeatureSource):
        def getFeatures(self, request=None):
            return QgsFeatureIterator([])
        def sourceName(self):
            return "test"
        def fields(self):
            return QgsFields()
        def wkbType(self):
            return 0
        def featureCount(self):
            return 0
        def sourceExtent(self):
            return QgsRectangle()
        def sourceCrs(self):
            return None

    s = MySource()
    assert s.sourceName() == "test"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_data_model.py::test_feature_source_is_abstract -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsFeatureSource and QgsFeatureSink**

```python
# core/qgsfeaturesource.py
from abc import ABC, abstractmethod

class QgsFeatureSource(ABC):
    @abstractmethod
    def getFeatures(self, request=None):
        pass

    @abstractmethod
    def sourceName(self) -> str:
        pass

    def sourceCrs(self):
        return None

    @abstractmethod
    def fields(self):
        pass

    @abstractmethod
    def wkbType(self) -> int:
        return 0

    @abstractmethod
    def featureCount(self) -> int:
        return 0

    @abstractmethod
    def sourceExtent(self):
        pass
```

```python
# core/qgsfeaturesink.py
from abc import ABC, abstractmethod

class QgsFeatureSink(ABC):
    @abstractmethod
    def addFeature(self, feature) -> bool:
        pass

    def addFeatures(self, features: list) -> bool:
        success = True
        for f in features:
            if not self.addFeature(f):
                success = False
        return success

    def finalize(self) -> bool:
        return True
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_data_model.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgsfeaturesource.py core/qgsfeaturesink.py tests/test_data_model.py
git commit -m "feat(core): add QgsFeatureSource and QgsFeatureSink ABCs (Tier 3)"
```

---

### Task 11: QgsCoordinateReferenceSystem

**Files:**
- Create: `core/qgscoordinatereferencesystem.py`
- Test: `tests/test_crs.py`

- [ ] **Step 1: Write failing tests**

```python
# tests/test_crs.py
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem

def test_crs_from_epsg():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert crs.isValid()
    assert crs.authid() == "EPSG:4326"

def test_crs_from_wkt():
    wkt = 'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]]'
    crs = QgsCoordinateReferenceSystem.fromWkt(wkt)
    assert crs.isValid()

def test_crs_is_geographic():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert crs.isGeographic()

def test_crs_is_projected():
    crs = QgsCoordinateReferenceSystem.fromEpsg(3857)
    assert not crs.isGeographic()

def test_crs_description():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    assert len(crs.description()) > 0

def test_crs_to_wkt():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    wkt = crs.toWkt()
    assert "WGS" in wkt or "EPSG" in wkt.upper()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_crs.py::test_crs_from_epsg -v`
Expected: FAIL

- [ ] **Step 3: Implement QgsCoordinateReferenceSystem**

```python
# core/qgscoordinatereferencesystem.py
from PySide6.QtCore import QObject, Signal
from pyproj import CRS

class QgsCoordinateReferenceSystem(QObject):
    crsChanged = Signal()

    def __init__(self, srs: str = ""):
        super().__init__()
        if srs:
            try:
                self._crs = CRS.from_user_input(srs)
            except Exception:
                self._crs = None
        else:
            self._crs = None

    def isValid(self) -> bool:
        return self._crs is not None

    def authid(self) -> str:
        if self._crs is None:
            return ""
        # Returns "EPSG:4326" style
        code = self._crs.to_authority(None, None)
        if code:
            return f"{code[0]}:{code[1]}"
        return ""

    def description(self) -> str:
        if self._crs is None:
            return ""
        return self._crs.name

    def toWkt(self) -> str:
        if self._crs is None:
            return ""
        return self._crs.to_wkt()

    def toProj(self) -> str:
        if self._crs is None:
            return ""
        return self._crs.to_proj4()

    def isGeographic(self) -> bool:
        if self._crs is None:
            return False
        return self._crs.is_geographic

    def mapUnits(self):
        from core.qgis import Qgis
        if self._crs is None:
            return Qgis.DistanceUnit.UnknownUnit
        if self._crs.is_geographic:
            return Qgis.DistanceUnit.Degrees
        return Qgis.DistanceUnit.Meters

    def pyproj_crs(self) -> CRS:
        return self._crs

    @staticmethod
    def fromEpsg(epsg_id: int) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        try:
            crs._crs = CRS.from_epsg(epsg_id)
        except Exception:
            crs._crs = None
        return crs

    @staticmethod
    def fromWkt(wkt: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        try:
            crs._crs = CRS.from_wkt(wkt)
        except Exception:
            crs._crs = None
        return crs

    @staticmethod
    def fromProj(proj: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        try:
            crs._crs = CRS.from_proj4(proj)
        except Exception:
            crs._crs = None
        return crs

    @staticmethod
    def fromOgcWmsCrs(ogc: str) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        try:
            crs._crs = CRS.from_user_input(ogc)
        except Exception:
            crs._crs = None
        return crs

    @staticmethod
    def fromSrid(srid: int) -> 'QgsCoordinateReferenceSystem':
        crs = QgsCoordinateReferenceSystem()
        try:
            crs._crs = CRS.from_user_input(f"urn:ogc:def:crs:EPSG::{srid}")
        except Exception:
            crs._crs = None
        return crs

    def __eq__(self, other):
        if not isinstance(other, QgsCoordinateReferenceSystem):
            return NotImplemented
        if self._crs is None and other._crs is None:
            return True
        if self._crs is None or other._crs is None:
            return False
        return self._crs == other._crs

    def __repr__(self):
        return f"QgsCoordinateReferenceSystem({self.authid()})"
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_crs.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgscoordinatereferencesystem.py tests/test_crs.py
git commit -m "feat(core): add QgsCoordinateReferenceSystem wrapping pyproj (Tier 4)"
```

---

### Task 12: Rewrite QgsCoordinateTransform

**Files:**
- Rewrite: `core/qgscoordinatetransform.py`
- Create: `core/qgscoordinatetransformcontext.py`
- Modify: `tests/test_crs.py`

- [ ] **Step 1: Write failing tests**

Append to `tests/test_crs.py`:

```python
from core.qgscoordinatetransform import QgsCoordinateTransform
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle

def test_transform_point():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    p = ct.transform(QgsPointXY(0, 0))
    # At (0,0) the transform should be approximately (0, 0) in Web Mercator
    assert abs(p.x()) < 1.0
    assert abs(p.y()) < 1.0

def test_transform_is_short_circuited():
    crs = QgsCoordinateReferenceSystem.fromEpsg(4326)
    ct = QgsCoordinateTransform(crs, crs)
    assert ct.isShortCircuited()

def test_transform_rect():
    src = QgsCoordinateReferenceSystem.fromEpsg(4326)
    dst = QgsCoordinateReferenceSystem.fromEpsg(3857)
    ct = QgsCoordinateTransform(src, dst)
    r = QgsRectangle(-10, -10, 10, 10)
    tr = ct.transformRect(r)
    # Web Mercator extent should be much larger in meters
    assert tr.width() > r.width()

def test_transform_invalid():
    ct = QgsCoordinateTransform(None, None)
    assert not ct.isValid()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `PYTHONPATH=. pytest tests/test_crs.py::test_transform_point -v`
Expected: FAIL

- [ ] **Step 3: Rewrite QgsCoordinateTransform and create QgsCoordinateTransformContext**

```python
# core/qgscoordinatetransform.py
from pyproj import Transformer
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle

class QgsCoordinateTransform:
    __slots__ = ('_source_crs', '_dest_crs', '_transformer', '_inverse_transformer', '_short_circuited')

    def __init__(self, source_crs: QgsCoordinateReferenceSystem = None,
                 dest_crs: QgsCoordinateReferenceSystem = None,
                 context=None):
        self._source_crs = source_crs
        self._dest_crs = dest_crs
        self._transformer = None
        self._inverse_transformer = None
        self._short_circuited = False

        if source_crs is None or dest_crs is None:
            return

        if not source_crs.isValid() or not dest_crs.isValid():
            return

        src_auth = source_crs.authid()
        dst_auth = dest_crs.authid()
        if src_auth == dst_auth:
            self._short_circuited = True
            return

        try:
            self._transformer = Transformer.from_crs(
                source_crs.pyproj_crs(), dest_crs.pyproj_crs(), always_xy=True)
            self._inverse_transformer = Transformer.from_crs(
                dest_crs.pyproj_crs(), source_crs.pyproj_crs(), always_xy=True)
        except Exception:
            self._transformer = None
            self._inverse_transformer = None

    def isValid(self) -> bool:
        return self._short_circuited or self._transformer is not None

    def isShortCircuited(self) -> bool:
        return self._short_circuited

    def sourceCrs(self) -> QgsCoordinateReferenceSystem:
        return self._source_crs

    def destinationCrs(self) -> QgsCoordinateReferenceSystem:
        return self._dest_crs

    def transform(self, point: QgsPointXY) -> QgsPointXY:
        if self._short_circuited:
            return point
        if self._transformer is None:
            return point
        x, y = self._transformer.transform(point.x(), point.y())
        return QgsPointXY(x, y)

    def transform_xy(self, x: float, y: float) -> tuple:
        """Transform raw x,y coordinates. Returns (x', y')."""
        if self._short_circuited:
            return x, y
        if self._transformer is None:
            return x, y
        return self._transformer.transform(x, y)

    def transformInPlace(self, x: float, y: float) -> tuple:
        return self.transform_xy(x, y)

    def transformRect(self, rect: QgsRectangle) -> QgsRectangle:
        if self._short_circuited:
            return rect
        if self._transformer is None:
            return rect
        xs, ys = self._transformer.transform(
            [rect.xMinimum(), rect.xMinimum(), rect.xMaximum(), rect.xMaximum()],
            [rect.yMinimum(), rect.yMaximum(), rect.yMinimum(), rect.yMaximum()]
        )
        return QgsRectangle(min(xs), min(ys), max(xs), max(ys))

    def transformPolygon(self, polygon):
        if self._short_circuited:
            return polygon
        if self._transformer is None:
            return polygon
        from PySide6.QtGui import QPolygonF
        from PySide6.QtCore import QPointF
        result = QPolygonF()
        for p in polygon:
            x, y = self._transformer.transform(p.x(), p.y())
            result.append(QPointF(x, y))
        return result

    def transform_geometry(self, geom_shape):
        if self._short_circuited:
            return geom_shape
        if self._transformer is None or geom_shape is None:
            return geom_shape
        from shapely.ops import transform as shapely_transform
        return shapely_transform(self._transformer.transform, geom_shape)

    # Legacy API compatibility
    def transform_bounds(self, left, bottom, right, top):
        if self._short_circuited:
            return left, bottom, right, top
        if self._transformer is None:
            return left, bottom, right, top
        xs, ys = self._transformer.transform(
            [left, left, right, right],
            [bottom, top, bottom, top]
        )
        return min(xs), min(ys), max(xs), max(ys)

    def transform_point(self, x: float, y: float) -> tuple:
        return self.transform_xy(x, y)

    def inverse_transform_point(self, x: float, y: float) -> tuple:
        if self._short_circuited:
            return x, y
        if self._inverse_transformer is None:
            return x, y
        return self._inverse_transformer.transform(x, y)

    def inverseTransform(self, point: QgsPointXY) -> QgsPointXY:
        if self._short_circuited:
            return point
        if self._inverse_transformer is None:
            return point
        x, y = self._inverse_transformer.transform(point.x(), point.y())
        return QgsPointXY(x, y)

    def inverse_transform_bounds(self, left, bottom, right, top):
        if self._short_circuited:
            return left, bottom, right, top
        if self._inverse_transformer is None:
            return left, bottom, right, top
        xs, ys = self._inverse_transformer.transform(
            [left, left, right, right],
            [bottom, top, bottom, top]
        )
        return min(xs), min(ys), max(xs), max(ys)

# Legacy alias
CRSTransformer = QgsCoordinateTransform
```

```python
# core/qgscoordinatetransformcontext.py
from PySide6.QtCore import QObject

class QgsCoordinateTransformContext(QObject):
    def __init__(self):
        super().__init__()
        self._transforms = {}

    def addSourceDestinationCrs(self, sourceCrs, destCrs, datumTransform=None):
        key = (sourceCrs.authid(), destCrs.authid())
        self._transforms[key] = datumTransform

    def hasTransform(self, sourceCrs, destCrs) -> bool:
        key = (sourceCrs.authid(), destCrs.authid())
        return key in self._transforms
```

- [ ] **Step 4: Run tests**

Run: `PYTHONPATH=. pytest tests/test_crs.py -v`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add core/qgscoordinatetransform.py core/qgscoordinatetransformcontext.py tests/test_crs.py
git commit -m "feat(core): rewrite QgsCoordinateTransform with new API (Tier 4)"
```

---

### Task 13: Update core/__init__.py Exports

**Files:**
- Modify: `core/__init__.py`

- [ ] **Step 1: Update exports**

```python
# core/__init__.py
from core.qgis import Qgis
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle
from core.qgsvector import QgsVector
from core.qgswkbtypes import QgsWkbTypes
from core.qgsunittypes import QgsUnitTypes
from core.qgsgeometry import QgsGeometry
from core.qgsfield import QgsField
from core.qgsfields import QgsFields
from core.qgsfeature import QgsFeature
from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgsfeatureiterator import QgsFeatureIterator
from core.qgsfeaturesource import QgsFeatureSource
from core.qgsfeaturesink import QgsFeatureSink
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgscoordinatetransform import QgsCoordinateTransform, CRSTransformer
from core.qgscoordinatetransformcontext import QgsCoordinateTransformContext
```

- [ ] **Step 2: Run all Phase 1 tests**

Run: `PYTHONPATH=. pytest tests/test_primitives.py tests/test_geometry.py tests/test_data_model.py tests/test_crs.py -v`
Expected: ALL PASS

- [ ] **Step 3: Run existing tests to verify no regressions**

Run: `PYTHONPATH=. pytest tests/ -v --ignore=tests/test_properties.py`
Expected: ALL PASS (existing tests should still work with backward-compat aliases)

- [ ] **Step 4: Commit**

```bash
git add core/__init__.py
git commit -m "feat(core): update __init__.py exports for Phase 1 (Tiers 1-4)"
```

---

## Phase 2: Data Layer (Tiers 5-6) — Outline

**Goal:** Implement providers (GDAL/OGR backends) and layer classes.

**Key files to create:**
- `core/qgsrasterinterface.py` — ABC with `block()` method
- `core/raster/qgsrasterblock.py` — numpy.ndarray wrapper
- `core/raster/qgsrasterpipe.py` — 7-stage pipeline
- `core/raster/qgsrasterdataprovider.py` — rasterio backend
- `core/raster/qgsrasterlayer.py` — rewrite with pipe
- `core/vector/qgsvectordataprovider.py` — fiona backend
- `core/vector/qgsmemoryprovider.py` — in-memory features
- `core/vector/qgsvectorlayer.py` — rewrite with edit buffer
- `core/qgsmaplayerstore.py` — layer registry

**Tests:** `test_raster_provider.py`, `test_vector_provider.py`, `test_layers.py`

---

## Phase 3: Rendering Core (Tiers 7-9) — Outline

**Goal:** Implement rendering infrastructure, symbology, and parallel renderer jobs.

**Key files to create:**
- `core/qgsmaptopixel.py` — coordinate transform matrix
- `core/qgsrendercontext.py` — per-render state
- `core/symbology/qgssymbol.py` — symbol hierarchy
- `core/symbology/qgssymbollayer.py` — symbol layer hierarchy
- `core/symbology/qgsrenderer.py` — feature renderers
- `core/maprenderer/qgsmaprendererjob.py` — base job
- `core/maprenderer/qgsmaprendererparalleljob.py` — QThreadPool parallel
- `core/maprenderer/qgsmaprenderercache.py` — per-layer image cache

**Tests:** `test_render_context.py`, `test_symbology.py`, `test_renderer_job.py`

---

## Phase 4: Canvas & Interaction (Tier 10) — Outline

**Goal:** Implement QGraphicsView-based canvas and map tools.

**Key files to create/rewrite:**
- `gui/qgsmapcanvas.py` — rewrite as QGraphicsView
- `gui/qgsmapcanvasmap.py` — QGraphicsItem for rendered image
- `gui/qgsmaptool.py` — rewrite with canvasPressEvent API
- `gui/qgsmaptoolpan.py` — rewrite with preview images
- `gui/qgsmaptoolzoom.py` — rubber band zoom
- `gui/qgsmaptoolidentify.py` — click to identify

**Tests:** Canvas integration tests

---

## Phase 5: Project & Polish (Tiers 11-13) — Outline

**Goal:** Project system, labeling, decorations.

**Key files to create/rewrite:**
- `core/qgsproject.py` — rewrite with new layer store
- `core/qgsrelationmanager.py` — relations
- `core/labeling/qgslabelingengine.py` — PAL engine
- `core/labeling/qgspallabeling.py` — label settings
- `core/scalebar/qgsscalebarrenderer.py` — scale bar
- `core/scalebar/qgsscalebarsettings.py` — scale bar config

**Tests:** `test_project.py`, `test_labeling.py`
