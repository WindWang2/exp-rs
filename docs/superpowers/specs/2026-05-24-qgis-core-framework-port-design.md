# QGIS Core Framework Port — Design Specification

## Overview

Complete port of QGIS's core framework to Python/PySide6, using Python-idiomatic backends (numpy, Shapely/GEOS, rasterio/GDAL, pyproj/PROJ). The application targets remote sensing (Orfeo Toolbox algorithms) and GIS operations (GDAL tools) built on top of a QGIS-identical foundation.

## Architecture

### Layered Dependency Model (13 Tiers)

```
Tier 13: Decorations     (ScaleBar, Grid, NorthArrow)
Tier 12: Labeling        (PAL Engine, LabelProvider)
Tier 11: Project         (QgsProject, RelationManager)
Tier 10: GUI             (QgsMapCanvas, MapTools, Identify)
Tier 9:  MapRenderer     (ParallelJob, SequentialJob, Cache)
Tier 8:  Symbology       (Symbol, SymbolLayer, FeatureRenderer)
Tier 7:  Rendering       (MapSettings, MapToPixel, RenderContext, ExpressionContext)
Tier 6:  Layers          (QgsMapLayer, QgsVectorLayer, QgsRasterLayer, MapLayerStore)
Tier 5:  Providers       (DataProvider, VectorDataProvider, RasterDataProvider, MemoryProvider)
Tier 4:  CRS             (CoordinateReferenceSystem, CoordinateTransform, TransformContext)
Tier 3:  DataModel       (Feature, Field, Fields, FeatureRequest, FeatureIterator)
Tier 2:  Geometry        (QgsGeometry wrapping Shapely, QgsWkbTypes)
Tier 1:  Primitives      (QgsPointXY, QgsRectangle, QgsVector, QgsUnitTypes)
```

### Key Design Decisions

1. **Geometry**: `QgsGeometry` wraps Shapely (GEOS) objects. No port of 44 QgsAbstractGeometry subclasses. QGIS API preserved.
2. **Raster data**: `QgsRasterBlock` uses `numpy.ndarray`. `QgsRasterDataProvider` uses rasterio.
3. **CRS**: `QgsCoordinateReferenceSystem` wraps `pyproj.CRS`. `QgsCoordinateTransform` wraps `pyproj.Transformer`.
4. **Parallel rendering**: `QgsMapRendererParallelJob` uses `QThreadPool` + `QRunnable` (Python equivalent of QtConcurrent).
5. **Map Canvas**: `QgsMapCanvas` inherits `QGraphicsView` (matching QGIS), not plain `QWidget`.
6. **Raster Pipe**: 7-stage pipeline (DataProvider → Nuller → Projector → ResampleFilter → BrightnessContrast → HueSaturation → Renderer).
7. **Per-layer compositing**: Each layer renders to its own `QImage`, then `composeImage()` merges with blend modes and opacity.
8. **C++ extensions**: `raster_ops.cpp` retained for performance-critical warp+stretch+compose operations.

### Performance Strategy

Performance is a hard requirement — the application must match QGIS responsiveness. Every subsystem must apply the appropriate concurrency pattern:

| Pattern | Where | Mechanism |
|---|---|---|
| **Multithreading** | Layer rendering, spatial indexing, label placement | `QThreadPool` + `QRunnable` (mirrors QtConcurrent) |
| **Tiled rendering** | Large raster reads (>4096px) | `QgsRasterIterator` splits into 256×256 or 512×512 tiles, each read independently |
| **Async UI** | All I/O-bound operations (file open, provider queries, CRS transforms on large datasets) | Signal/slot across thread boundary; never block the GUI thread |
| **Multiprocessing** | CPU-bound batch operations (OTB algorithms, GDAL warp on large rasters, PCA, statistics) | `ProcessPoolExecutor` — bypasses GIL for true parallelism |
| **GDAL overviews** | Raster display at reduced zoom | Read from `.ovr` pyramids instead of full-res downsampling |
| **LRU caching** | Raster block cache, renderer image cache | `QgsMapRendererCache` (per-layer QImage), `QgsRasterBlock` LRU pool |
| **Preview rendering** | Instant feedback during pan/zoom | 256px low-res preview on main thread (~5ms), full-res in background |

Rules:
- **Never** open a rasterio/fiona handle on the GUI thread for data reads — always delegate to a worker.
- **Never** share a file handle across threads — each thread opens its own.
- **Always** check `_is_canceled` at the start of each tile/layer render loop iteration.
- **Always** use generation counters to discard stale renders.

### Toolbox Sources

```
tools/
├── gis/              ← QGIS Processing Framework operations
├── gdal/             ← GDAL data processing (warp, translate, DEM, etc.)
└── remote_sensing/   ← Orfeo Toolbox (OTB) algorithms
```

---

## Tier 1: Primitives

### QgsPointXY
- 2D point, value type (Q_GADGET equivalent)
- Methods: `x()`, `y()`, `setX()`, `setY()`, `distance()`, `azimuth()`, `isEmpty()`, `isNull()`

### QgsRectangle
- Axis-aligned rectangle, value type
- Methods: `xMinimum()`, `xMaximum()`, `yMinimum()`, `yMaximum()`, `width()`, `height()`, `area()`, `center()`, `contains()`, `intersects()`, `intersect()`, `union()`, `grow()`, `normalize()`, `asWkt()`

### QgsVector
- 2D vector with x/y components
- Methods: `x()`, `y()`, `length()`, `normalized()`, operators

### QgsWkbTypes
- Complete WKB geometry type enumeration
- Methods: `geometryType()`, `isMultiType()`, `hasZ()`, `hasM()`, `flatType()`

### QgsUnitTypes
- Distance/area/render/angle unit enums and conversion

---

## Tier 2: Geometry System

### QgsGeometry
Wraps Shapely `BaseGeometry`. Preserves QGIS API.

```python
class QgsGeometry:
    _geom: shapely.geometry.base.BaseGeometry

    # Properties
    def type(self) -> Qgis.GeometryType
    def wkbType(self) -> QgsWkbTypes.Type
    def isEmpty(self) -> bool
    def isNull(self) -> bool
    def area(self) -> float
    def length(self) -> float
    def centroid(self) -> QgsGeometry
    def boundingBox(self) -> QgsRectangle

    # Spatial operations (delegated to Shapely/GEOS)
    def buffer(self, distance: float, segments: int = 8) -> QgsGeometry
    def simplify(self, tolerance: float) -> QgsGeometry
    def makeValid(self) -> QgsGeometry
    def contains(self, other: QgsGeometry) -> bool
    def intersects(self, other: QgsGeometry) -> bool
    def disjoint(self, other: QgsGeometry) -> bool
    def within(self, other: QgsGeometry) -> bool
    def crosses(self, other: QgsGeometry) -> bool
    def touches(self, other: QgsGeometry) -> bool
    def overlaps(self, other: QgsGeometry) -> bool
    def combine(self, other: QgsGeometry) -> QgsGeometry
    def difference(self, other: QgsGeometry) -> QgsGeometry
    def intersection(self, other: QgsGeometry) -> QgsGeometry
    def symDifference(self, other: QgsGeometry) -> QgsGeometry

    # Transform
    def transform(self, ct: QgsCoordinateTransform) -> QgsGeometry

    # I/O
    def asWkt(self) -> str
    def asWkb(self) -> bytes
    @staticmethod
    def fromWkt(wkt: str) -> QgsGeometry
    @staticmethod
    def fromWkb(wkb: bytes) -> QgsGeometry
    @staticmethod
    def fromPointXY(point: QgsPointXY) -> QgsGeometry
    @staticmethod
    def fromPolyline(points: list[QgsPointXY]) -> QgsGeometry
    @staticmethod
    def fromPolygon(rings: list[list[QgsPointXY]]) -> QgsGeometry
```

---

## Tier 3: Data Model

### QgsField
```python
class QgsField:
    _name: str
    _type: QVariant.Type  # Python type
    _type_name: str
    _length: int
    _precision: int
    _comment: str
    _alias: str

    def name(self) -> str
    def type(self) -> type
    def typeName(self) -> str
    def length(self) -> int
    def precision(self) -> int
    def isNumeric(self) -> bool
```

### QgsFields
```python
class QgsFields:
    _fields: list[QgsField]

    def append(self, field: QgsField)
    def count(self) -> int
    def indexOf(self, name: str) -> int
    def at(self, i: int) -> QgsField
    def field(self, name: str) -> QgsField
    def names(self) -> list[str]
    def isEmpty(self) -> bool
    def extend(self, other: QgsFields)
```

### QgsFeature
```python
class QgsFeature:
    _id: int
    _fields: QgsFields
    _attributes: list
    _geometry: QgsGeometry

    def id(self) -> int
    def setId(self, id: int)
    def fields(self) -> QgsFields
    def setFields(self, fields: QgsFields)
    def attributes(self) -> list
    def attribute(self, index_or_name) -> Any
    def setAttribute(self, index_or_name, value) -> bool
    def geometry(self) -> QgsGeometry
    def setGeometry(self, geom: QgsGeometry)
    def isValid(self) -> bool
    def attributeCount(self) -> int
```

### QgsFeatureRequest
```python
class QgsFeatureRequest:
    class FilterType(enum):
        NoFilter, FilterRect, FilterFid, FilterFids, FilterExpression

    _filter_type: FilterType
    _filter_rect: QgsRectangle
    _filter_fid: int
    _filter_fids: set[int]
    _subset_attributes: list[str]
    _destination_crs: QgsCoordinateReferenceSystem
    _limit: int

    def setFilterRect(self, rect: QgsRectangle) -> QgsFeatureRequest
    def setFilterFids(self, fids: set[int]) -> QgsFeatureRequest
    def setSubsetOfAttributes(self, attrs: list[str]) -> QgsFeatureRequest
    def setDestinationCrs(self, crs: QgsCoordinateReferenceSystem, context: QgsCoordinateTransformContext) -> QgsFeatureRequest
    def setLimit(self, limit: int) -> QgsFeatureRequest
```

### QgsFeatureIterator
```python
class QgsAbstractFeatureIterator(ABC):
    def nextFeature(self) -> tuple[bool, QgsFeature]
    def rewind(self)
    def close(self)
    def isValid(self) -> bool

class QgsFeatureIterator:
    _iterator: QgsAbstractFeatureIterator

    def __iter__(self) -> QgsFeatureIterator
    def __next__(self) -> QgsFeature
    def nextFeature(self) -> tuple[bool, QgsFeature]
    def close(self)
```

### QgsFeatureSource / QgsFeatureSink
```python
class QgsFeatureSource(ABC):
    def getFeatures(self, request: QgsFeatureRequest = None) -> QgsFeatureIterator
    def sourceName(self) -> str
    def sourceCrs(self) -> QgsCoordinateReferenceSystem
    def fields(self) -> QgsFields
    def wkbType(self) -> QgsWkbTypes.Type
    def featureCount(self) -> int
    def sourceExtent(self) -> QgsRectangle

class QgsFeatureSink(ABC):
    def addFeature(self, feature: QgsFeature) -> bool
    def addFeatures(self, features: list[QgsFeature]) -> bool
    def finalize(self) -> bool
```

---

## Tier 4: Coordinate Reference System

### QgsCoordinateReferenceSystem
```python
class QgsCoordinateReferenceSystem(QObject):
    _crs: pyproj.CRS

    def isValid(self) -> bool
    def authid(self) -> str          # "EPSG:4326"
    def description(self) -> str
    def toWkt(self) -> str
    def toProj(self) -> str
    def isGeographic(self) -> bool
    def mapUnits(self) -> Qgis.DistanceUnit

    @staticmethod
    def fromEpsg(id: int) -> QgsCoordinateReferenceSystem
    @staticmethod
    def fromWkt(wkt: str) -> QgsCoordinateReferenceSystem
    @staticmethod
    def fromProj(proj: str) -> QgsCoordinateReferenceSystem
    @staticmethod
    def fromOgcWmsCrs(ogc: str) -> QgsCoordinateReferenceSystem
    @staticmethod
    def fromSrid(srid: int) -> QgsCoordinateReferenceSystem
```

### QgsCoordinateTransform
```python
class QgsCoordinateTransform:
    _source_crs: QgsCoordinateReferenceSystem
    _dest_crs: QgsCoordinateReferenceSystem
    _transformer: pyproj.Transformer
    _context: QgsCoordinateTransformContext

    def transform(self, point: QgsPointXY) -> QgsPointXY
    def transformRect(self, rect: QgsRectangle) -> QgsRectangle
    def transformPolygon(self, polygon: QPolygonF) -> QPolygonF
    def sourceCrs(self) -> QgsCoordinateReferenceSystem
    def destinationCrs(self) -> QgsCoordinateReferenceSystem
    def isShortCircuited(self) -> bool  # src == dest, no-op
    def isValid(self) -> bool
```

### QgsCoordinateTransformContext
```python
class QgsCoordinateTransformContext(QObject):
    _transforms: dict[tuple[str, str], tuple[int, int]]  # (src_auth, dst_auth) -> (src_datum, dst_datum)

    def addSourceDestinationCrs(self, sourceCrs: QgsCoordinateReferenceSystem, destCrs: QgsCoordinateReferenceSystem, datumTransform: tuple[int, int] = None)
    def hasTransform(self, sourceCrs: QgsCoordinateReferenceSystem, destCrs: QgsCoordinateReferenceSystem) -> bool
```

---

## Tier 5: Data Providers

### QgsDataProvider
```python
class QgsDataProvider(QObject, ABC):
    _uri: str
    _crs: QgsCoordinateReferenceSystem

    def uri(self) -> str
    def crs(self) -> QgsCoordinateReferenceSystem
    def extent(self) -> QgsRectangle
    def isValid(self) -> bool
    def name(self) -> str  # "gdal", "ogr", "memory"
```

### QgsRasterDataProvider (QgsRasterInterface)
```python
class QgsRasterDataProvider(QgsDataProvider, QgsRasterInterface):
    """rasterio back-end. Implements QgsRasterInterface.block()."""
    def block(self, band_no: int, extent: QgsRectangle, width: int, height: int, feedback=None) -> QgsRasterBlock
    def bandCount(self) -> int
    def dataType(self, band_no: int) -> Qgis.DataType
    def sourceDataType(self, band_no: int) -> Qgis.DataType
    def xSize(self) -> int
    def ySize(self) -> int
    def noDataValue(self, band_no: int) -> float
    def extent(self) -> QgsRectangle
    def crs(self) -> QgsCoordinateReferenceSystem
```

### QgsVectorDataProvider
```python
class QgsVectorDataProvider(QgsDataProvider, QgsFeatureSource, QgsFeatureSink):
    """fiona/ogr back-end."""

    class Capability(enum.IntFlag):
        NoCapabilities = 0
        AddFeatures = 1
        DeleteFeatures = 2
        ChangeAttributeValues = 4
        AddAttributes = 8
        DeleteAttributes = 16
        ChangeGeometries = 32
        CreateSpatialIndex = 64

    def getFeatures(self, request: QgsFeatureRequest = None) -> QgsFeatureIterator
    def fields(self) -> QgsFields
    def wkbType(self) -> QgsWkbTypes.Type
    def featureCount(self) -> int
    def capabilities(self) -> Capability
    def addFeatures(self, features: list[QgsFeature]) -> tuple[bool, list[QgsFeature]]
    def deleteFeatures(self, fids: list[int]) -> bool
    def changeGeometryValues(self, geometries: dict[int, QgsGeometry]) -> bool
    def changeAttributeValues(self, attrs: dict[int, dict[int, Any]]) -> bool
```

### QgsMemoryProvider
```python
class QgsMemoryProvider(QgsVectorDataProvider):
    """In-memory feature storage."""
    _features: dict[int, QgsFeature]
    _fields: QgsFields
    _wkb_type: QgsWkbTypes.Type
    _crs: QgsCoordinateReferenceSystem
    _next_id: int
```

---

## Tier 6: Layers

### QgsMapLayer
```python
class QgsMapLayer(QObject, ABC):
    _id: str
    _name: str
    _crs: QgsCoordinateReferenceSystem
    _opacity: float
    _visible: bool

    def id(self) -> str
    def name(self) -> str
    def setName(self, name: str)
    def crs(self) -> QgsCoordinateReferenceSystem
    def setCrs(self, crs: QgsCoordinateReferenceSystem)
    def type(self) -> Qgis.LayerType
    def extent(self) -> QgsRectangle
    def opacity(self) -> float
    def setOpacity(self, opacity: float)
    def isVisible(self) -> bool
    def setVisible(self, visible: bool)
    def dataProvider(self) -> QgsDataProvider
    def createMapRenderer(self, context: QgsRenderContext) -> QgsMapLayerRenderer
```

### QgsRasterLayer
```python
class QgsRasterLayer(QgsMapLayer):
    _pipe: QgsRasterPipe
    _provider: QgsRasterDataProvider

    def pipe(self) -> QgsRasterPipe
    def renderer(self) -> QgsRasterRenderer
    def setRenderer(self, renderer: QgsRasterRenderer)
    def resampleFilter(self) -> QgsRasterResampleFilter
    def bandCount(self) -> int
    def bandName(self, band: int) -> str
    def rasterType(self) -> Qgis.RasterLayerType
```

### QgsVectorLayer
```python
class QgsVectorLayer(QgsMapLayer):
    _provider: QgsVectorDataProvider
    _renderer: QgsFeatureRenderer
    _labeling: QgsAbstractVectorLayerLabeling
    _edit_buffer: QgsVectorLayerEditBuffer
    _selected_fids: set[int]
    _display_expression: str

    def dataProvider(self) -> QgsVectorDataProvider
    def featureRenderer(self) -> QgsFeatureRenderer
    def setRenderer(self, renderer: QgsFeatureRenderer)
    def labeling(self) -> QgsAbstractVectorLayerLabeling
    def setLabeling(self, labeling: QgsAbstractVectorLayerLabeling)
    def fields(self) -> QgsFields
    def getFeatures(self, request: QgsFeatureRequest = None) -> QgsFeatureIterator
    def selectedFeatureIds(self) -> set[int]
    def selectByIds(self, fids: set[int])
    def deselect(self, fids: set[int])
    def selectedFeatureCount(self) -> int

    # Editing
    def startEditing(self) -> bool
    def commitChanges(self) -> bool
    def rollBack(self) -> bool
    def isEditable(self) -> bool
    def isModified(self) -> bool
    def addFeature(self, feature: QgsFeature) -> bool
    def deleteFeature(self, fid: int) -> bool
    def changeGeometry(self, fid: int, geom: QgsGeometry) -> bool
    def changeAttributeValue(self, fid: int, field: int, value: Any) -> bool
```

### QgsMapLayerStore
```python
class QgsMapLayerStore(QObject):
    _layers: dict[str, QgsMapLayer]

    def addMapLayer(self, layer: QgsMapLayer) -> QgsMapLayer
    def addMapLayers(self, layers: list[QgsMapLayer]) -> list[QgsMapLayer]
    def removeMapLayer(self, layer_id: str) -> bool
    def removeMapLayers(self, layer_ids: list[str])
    def mapLayer(self, layer_id: str) -> QgsMapLayer
    def mapLayers(self) -> dict[str, QgsMapLayer]
    def count(self) -> int

    # Signals
    layersAdded = Signal(list)
    layersRemoved = Signal(list)
```

---

## Tier 7: Rendering Infrastructure

### QgsMapSettings
```python
class QgsMapSettings:
    _extent: QgsRectangle
    _output_size: QSize
    _layers: list[QgsMapLayer]
    _dest_crs: QgsCoordinateReferenceSystem
    _dpi: float
    _rotation: float
    _device_pixel_ratio: float

    # Derived (computed by updateDerived())
    _map_to_pixel: QgsMapToPixel
    _visible_extent: QgsRectangle
    _map_units_per_pixel: float
    _scale: float

    def extent(self) -> QgsRectangle
    def setExtent(self, extent: QgsRectangle)
    def visibleExtent(self) -> QgsRectangle
    def outputSize(self) -> QSize
    def setOutputSize(self, size: QSize)
    def layers(self) -> list[QgsMapLayer]
    def setLayers(self, layers: list[QgsMapLayer])
    def destinationCrs(self) -> QgsCoordinateReferenceSystem
    def setDestinationCrs(self, crs: QgsCoordinateReferenceSystem)
    def mapToPixel(self) -> QgsMapToPixel
    def mapUnitsPerPixel(self) -> float
    def scale(self) -> float
    def setDpi(self, dpi: float)
    def setRotation(self, rotation: float)
    def layerTransform(self, layer: QgsMapLayer) -> QgsCoordinateTransform

    def updateDerived(self):
        """Recalculate mapToPixel, visibleExtent, mapUnitsPerPixel, scale."""
```

### QgsMapToPixel
```python
class QgsMapToPixel:
    _map_units_per_pixel: float
    _center_x: float
    _center_y: float
    _width: int
    _height: int
    _rotation: float
    _matrix: QTransform

    def transform(self, point: QgsPointXY) -> QgsPointXY
    def transformInPlace(self, x: float, y: float) -> tuple[float, float]
    def toMapCoordinates(self, x: int, y: int) -> QgsPointXY
    def mapUnitsPerPixel(self) -> float
    def mapRotation(self) -> float
    def setMapRotation(self, rotation: float)
    def isValid(self) -> bool
```

### QgsRenderContext
```python
class QgsRenderContext:
    _painter: QPainter
    _map_to_pixel: QgsMapToPixel
    _expression_context: QgsExpressionContext
    _coordinate_transform: QgsCoordinateTransform
    _renderer_scale: float
    _flags: RenderFlags

    @staticmethod
    def fromMapSettings(settings: QgsMapSettings) -> QgsRenderContext
    def painter(self) -> QPainter
    def setPainter(self, painter: QPainter)
    def mapToPixel(self) -> QgsMapToPixel
    def expressionContext(self) -> QgsExpressionContext
    def setExpressionContext(self, context: QgsExpressionContext)
    def coordinateTransform(self) -> QgsCoordinateTransform
    def setCoordinateTransform(self, ct: QgsCoordinateTransform)
    def rendererScale(self) -> float
    def setRendererScale(self, scale: float)
    def destinationCrs(self) -> QgsCoordinateReferenceSystem
```

---

## Tier 8: Symbology

### QgsSymbol / QgsSymbolLayer
```python
class QgsSymbol(ABC):
    class Type(enum):
        Marker, Line, Fill

    _symbol_layers: list[QgsSymbolLayer]
    _opacity: float
    _blend_mode: QPainter.CompositionMode
    _color: QColor

    def type(self) -> QgsSymbol.Type
    def renderFeature(self, feature: QgsFeature, context: QgsRenderContext, layer: int = -1, selected: bool = False)
    def startRender(self, context: QgsRenderContext)
    def stopRender(self, context: QgsRenderContext)
    def symbolLayer(self, layer: int) -> QgsSymbolLayer
    def symbolLayerCount(self) -> int
    def appendSymbolLayer(self, layer: QgsSymbolLayer) -> bool
    def insertSymbolLayer(self, index: int, layer: QgsSymbolLayer) -> bool
    def removeSymbolLayer(self, index: int) -> bool
    def clone(self) -> QgsSymbol
    def opacity(self) -> float
    def setOpacity(self, opacity: float)
    def color(self) -> QColor
    def setColor(self, color: QColor)

class QgsMarkerSymbol(QgsSymbol):
    @staticmethod
    def createSimple(properties: dict) -> QgsMarkerSymbol
    def angle(self) -> float
    def setAngle(self, angle: float)
    def size(self) -> float
    def setSize(self, size: float)
    def renderPoint(self, point: QPointF, feature: QgsFeature, context: QgsRenderContext, layer: int = -1, selected: bool = False)

class QgsLineSymbol(QgsSymbol):
    @staticmethod
    def createSimple(properties: dict) -> QgsLineSymbol
    def width(self) -> float
    def setWidth(self, width: float)
    def renderPolyline(self, points: QPolygonF, feature: QgsFeature, context: QgsRenderContext, layer: int = -1, selected: bool = False)

class QgsFillSymbol(QgsSymbol):
    @staticmethod
    def createSimple(properties: dict) -> QgsFillSymbol
    def renderPolygon(self, ring: QPolygonF, holes: list[QPolygonF], feature: QgsFeature, context: QgsRenderContext, layer: int = -1, selected: bool = False)

class QgsSymbolLayer(ABC):
    _fill_color: QColor
    _stroke_color: QColor
    _stroke_width: float

    def startRender(self, context: QgsRenderContext)
    def stopRender(self, context: QgsRenderContext)
    def renderPoint(self, point: QPointF, context: QgsRenderContext)
    def renderPolyline(self, points: QPolygonF, context: QgsRenderContext)
    def renderPolygon(self, ring: QPolygonF, holes: list[QPolygonF], context: QgsRenderContext)
    def clone(self) -> QgsSymbolLayer
    def properties(self) -> dict

class QgsSimpleMarkerSymbolLayer(QgsSymbolLayer):
    class Shape(enum):
        Circle, Square, Triangle, Diamond, Cross, Star, Hexagon
    _shape: Shape
    _size: float
    _angle: float
    _color: QColor
    _stroke_color: QColor
    _stroke_width: float

class QgsSimpleLineSymbolLayer(QgsSymbolLayer):
    _color: QColor
    _width: float
    _pen_style: Qt.PenStyle
    _use_custom_dash: bool
    _custom_dash_vector: list[float]

class QgsSimpleFillSymbolLayer(QgsSymbolLayer):
    _fill_color: QColor
    _stroke_color: QColor
    _stroke_width: float
    _brush_style: Qt.BrushStyle
    _pen_style: Qt.PenStyle
```

### QgsFeatureRenderer
```python
class QgsFeatureRenderer(ABC):
    def symbolForFeature(self, feature: QgsFeature, context: QgsRenderContext) -> QgsSymbol
    def originalSymbolForFeature(self, feature: QgsFeature, context: QgsRenderContext) -> QgsSymbol
    def startRender(self, context: QgsRenderContext)
    def stopRender(self, context: QgsRenderContext)
    def usedAttributes(self, context: QgsRenderContext) -> set[str]
    def type(self) -> str
    def clone(self) -> QgsFeatureRenderer
    def symbols(self, context: QgsRenderContext) -> list[QgsSymbol]
    def legendSymbolItems(self) -> list

    @staticmethod
    def create(element: QDomElement) -> QgsFeatureRenderer  # factory from XML
    def save(self, doc: QDomDocument) -> QDomElement

class QgsSingleSymbolRenderer(QgsFeatureRenderer):
    _symbol: QgsSymbol

    @staticmethod
    def create() -> QgsSingleSymbolRenderer
    def symbol(self) -> QgsSymbol
    def setSymbol(self, symbol: QgsSymbol)

class QgsCategorizedSymbolRenderer(QgsFeatureRenderer):
    _categories: list[QgsRendererCategory]
    _attribute_name: str
    _default_symbol: QgsSymbol

    def categories(self) -> list[QgsRendererCategory]
    def addCategory(self, category: QgsRendererCategory)
    def updateCategories(self, categories: list[QgsRendererCategory])
    def classAttribute(self) -> str
    def setClassAttribute(self, attr: str)

class QgsGraduatedSymbolRenderer(QgsFeatureRenderer):
    _ranges: list[QgsRendererRange]
    _attribute_name: str
    _mode: Mode  # EqualInterval, Quantile, NaturalBreaks, StdDev, Pretty
    _default_symbol: QgsSymbol

    def ranges(self) -> list[QgsRendererRange]
    def addRange(self, range: QgsRendererRange)
    def classAttribute(self) -> str
    def setClassAttribute(self, attr: str)
    def mode(self) -> Mode

    @staticmethod
    def createRenderer(layer: QgsVectorLayer, attr: str, classes: int, mode: Mode, symbol: QgsSymbol, ranges: list) -> 'QgsGraduatedSymbolRenderer'
```

---

## Tier 9: Map Renderer Jobs

### Per-Layer Render Job
```python
class LayerRenderJob:
    img: QImage                    # per-layer temporary render target
    renderer: QgsMapLayerRenderer  # layer renderer
    layer: QgsMapLayer
    blend_mode: QPainter.CompositionMode
    opacity: float
    cached: bool
```

### QgsMapRendererJob
```python
class QgsMapRendererJob(QObject):
    class Flags(enum):
        ForceVectorRender = 1
        PartialRender = 2

    _settings: QgsMapSettings
    _layer_jobs: list[LayerRenderJob]
    _cache: QgsMapRendererCache
    _is_canceled: bool
    _rendering_time: int

    def start(self)
    def cancel(self)
    def waitForFinished(self)
    def isActive(self) -> bool
    def mapSettings(self) -> QgsMapSettings
    def renderingTime(self) -> int
    def renderedImage(self) -> QImage

    def prepareJobs(self):
        """Create LayerRenderJob for each visible layer via layer.createMapRenderer(context)."""

    @staticmethod
    def composeImage(settings: QgsMapSettings, jobs: list[LayerRenderJob], labelJob=None) -> QImage:
        """Composite all per-layer images with blend modes and opacity into final image."""

    # Signals
    finished = Signal()
    renderingLayersFinished = Signal()
```

### QgsMapRendererParallelJob
```python
class QgsMapRendererParallelJob(QgsMapRendererJob):
    _state: State  # Idle, RenderingLayers, RenderingLabels, Idle

    def start(self):
        self.prepareJobs()
        # Parallel: render all layers simultaneously
        futures = []
        for job in self._layer_jobs:
            future = QThreadPool.globalInstance().start(lambda j=job: self._renderLayerStatic(j))
            futures.append(future)
        # Wait for all, then compose
        self._state = self.State.RenderingLayers

    def _renderLayerStatic(self, job: LayerRenderJob):
        """Called in worker thread per layer."""
        painter = QPainter(job.img)
        context = QgsRenderContext.fromMapSettings(self._settings)
        context.setPainter(painter)
        job.renderer.render(context, self._settings)
        painter.end()
```

### QgsMapRendererCache
```python
class QgsMapRendererCache(QObject):
    _cache: dict[str, QImage]  # layer_id -> cached image
    _cache_extent: QgsRectangle
    _cache_scale: float
    _mutex: QMutex

    def cacheImage(self, layer_id: str) -> QImage
    def setCacheImage(self, layer_id: str, image: QImage)
    def hasCacheImage(self, layer_id: str) -> bool
    def invalidate(self)
    def transformedCacheImage(self, layer_id: str, extent: QgsRectangle, size: QSize) -> QImage
```

---

## Tier 10: Map Canvas & Tools

### QgsMapCanvas
```python
class QgsMapCanvas(QGraphicsView):
    _settings: QgsMapSettings
    _map: QgsMapCanvasMap
    _job: QgsMapRendererJob
    _cache: QgsMapRendererCache
    _tool: QgsMapTool
    _preview_images: list  # pan preview

    def refresh(self):
        """Schedule refresh via QTimer.singleShot(1, self.refreshMap)"""
    def refreshMap(self):
        """Create and start renderer job."""
    def _onRenderFinished(self):
        """Take rendered image, set on canvas map item."""
    def setMapTool(self, tool: QgsMapTool)
    def mapTool(self) -> QgsMapTool
    def mapSettings(self) -> QgsMapSettings
    def extent(self) -> QgsRectangle
    def setExtent(self, rect: QgsRectangle)
    def zoomToFeatureExtent(self, rect: QgsRectangle)
    def setLayers(self, layers: list[QgsMapLayer])

    # Signals
    xyCoordinates = Signal(QgsPointXY)
    renderComplete = Signal(QPainter)
    extentsChanged = Signal()
    layersChanged = Signal()
```

### QgsMapCanvasMap
```python
class QgsMapCanvasMap(QGraphicsItem):
    _content: QImage
    _content_rect: QgsRectangle
    _preview_images: list[tuple[QImage, QgsRectangle]]

    def setContent(self, image: QImage, rect: QgsRectangle)
    def paint(self, painter: QPainter, option, widget)
    def addPreviewImage(self, image: QImage, rect: QgsRectangle)
    def clearPreviewImages(self)
```

### QgsMapTool
```python
class QgsMapTool(QObject):
    class Flag(enum):
        Transient, EditTool, AllowZoomRect, ShowContextMenu

    _canvas: QgsMapCanvas

    def canvasPressEvent(self, event: QgsMapMouseEvent)
    def canvasReleaseEvent(self, event: QgsMapMouseEvent)
    def canvasMoveEvent(self, event: QgsMapMouseEvent)
    def canvasDoubleClickEvent(self, event: QgsMapMouseEvent)
    def wheelEvent(self, event: QgsMapMouseEvent)
    def keyPressEvent(self, event: QKeyEvent)
    def activate(self)
    def deactivate(self)
    def canvas(self) -> QgsMapCanvas
    def setCursor(self, cursor: QCursor)
    def flags(self) -> Flag

class QgsMapToolPan(QgsMapTool):
    """Drag-to-pan with preview images for smooth panning."""
    _dragging: bool
    _pan_start: QPoint

class QgsMapToolZoom(QgsMapTool):
    """Zoom by rubber band rectangle or wheel."""
    _dragging: bool
    _zoom_rect: QRect

class QgsMapToolIdentify(QgsMapTool):
    """Click to identify features at point."""
    def identify(self, x: int, y: int) -> list[IdentifyResult]
    def identifyMapLayer(self, layer: QgsMapLayer, point: QgsPointXY) -> list[IdentifyResult]
```

---

## Tier 11: Project System

### QgsProject
```python
class QgsProject(QObject):
    _instance: QgsProject = None

    _title: str
    _file_name: str
    _home_path: str
    _crs: QgsCoordinateReferenceSystem
    _transform_context: QgsCoordinateTransformContext
    _layer_store: QgsMapLayerStore
    _relation_manager: QgsRelationManager
    _snapping_config: QgsSnappingConfig

    @staticmethod
    def instance() -> QgsProject

    def crs(self) -> QgsCoordinateReferenceSystem
    def setCrs(self, crs: QgsCoordinateReferenceSystem)
    def transformContext(self) -> QgsCoordinateTransformContext
    def setTransformContext(self, context: QgsCoordinateTransformContext)
    def layerStore(self) -> QgsMapLayerStore
    def addMapLayer(self, layer: QgsMapLayer) -> QgsMapLayer
    def addMapLayers(self, layers: list[QgsMapLayer]) -> list[QgsMapLayer]
    def removeMapLayer(self, layer_id: str)
    def removeMapLayers(self, layer_ids: list[str])
    def mapLayers(self) -> dict[str, QgsMapLayer]
    def mapLayer(self, layer_id: str) -> QgsMapLayer
    def relationManager(self) -> QgsRelationManager

    def read(self, file_name: str) -> bool
    def write(self, file_name: str) -> bool
    def clear(self)

    # Signals
    layersAdded = Signal(list)
    layersRemoved = Signal(list)
    crsChanged = Signal()
    readProject = Signal(QDomDocument)
    writeProject = Signal(QDomDocument)
```

---

## Tier 12: Labeling (Simplified Initial)

```python
class QgsLabelingEngine:
    """PAL-based label placement engine."""
    _providers: list[QgsAbstractLabelProvider]

    def addProvider(self, provider: QgsAbstractLabelProvider)
    def process(self, settings: QgsMapSettings, feedback=None)
    def results(self) -> QgsLabelingResults

class QgsPalLayerSettings:
    """Label configuration (simplified from 90+ properties)."""
    _field_name: str
    _is_expression: bool
    _text_format: QgsTextFormat
    _placement: Placement  # Point, Line, PerimeterCurved, Horizontal, Free
    _priority: int
    _obstacle: bool

class QgsVectorLayerLabelProvider(QgsAbstractLabelProvider):
    _layer: QgsVectorLayer
    _settings: QgsPalLayerSettings
```

---

## Tier 13: Decorations

```python
class QgsMapDecoration(ABC):
    def render(self, settings: QgsMapSettings, context: QgsRenderContext)
    def displayName(self) -> str

class QgsScaleBarRenderer(ABC):
    def draw(self, painter: QPainter, settings: QgsScaleBarSettings, context: QgsRenderContext)

class QgsScaleBarSettings:
    _style: str  # "SingleBox", "DoubleBox", "Numeric", "Line"
    _units_per_segment: float
    _num_segments: int
    _segmentSizeMode: SegmentSizeMode
    _height: float
    _line_width: float
```

---

## Target Directory Structure

```
exp-rs/
├── core/
│   ├── __init__.py
│   ├── qgis.py                          # Qgis namespace enums
│   ├── qgspointxy.py
│   ├── qgsrectangle.py
│   ├── qgswkbtypes.py
│   ├── qgsunittypes.py
│   ├── qgsgeometry.py
│   ├── qgsfield.py
│   ├── qgsfields.py
│   ├── qgsfeature.py
│   ├── qgsfeaturerequest.py
│   ├── qgsfeatureiterator.py
│   ├── qgsfeaturesource.py
│   ├── qgsfeaturesink.py
│   ├── qgscoordinatereferencesystem.py
│   ├── qgscoordinatetransform.py
│   ├── qgscoordinatetransformcontext.py
│   ├── qgsdataprovider.py
│   ├── qgsmaplayer.py
│   ├── qgsmaplayerstore.py
│   ├── qgsmapsettings.py
│   ├── qgsmaptopixel.py
│   ├── qgsrendercontext.py
│   ├── qgsexpressioncontext.py
│   ├── qgsdistancearea.py
│   ├── qgsproject.py
│   ├── qgsrelation.py
│   ├── qgsrelationmanager.py
│   ├── qgsspatialindex.py
│   ├── maprenderer/
│   │   ├── __init__.py
│   │   ├── qgsmaprendererjob.py
│   │   ├── qgsmaprendererparalleljob.py
│   │   ├── qgsmaprenderersequentialjob.py
│   │   └── qgsmaprenderercache.py
│   ├── raster/
│   │   ├── __init__.py
│   │   ├── qgsrasterinterface.py
│   │   ├── qgsrasterblock.py
│   │   ├── qgsrasterpipe.py
│   │   ├── qgsrasterdataprovider.py
│   │   ├── qgsrasterprojector.py
│   │   ├── qgsrasterresamplefilter.py
│   │   ├── qgsrasterrenderer.py
│   │   ├── qgsrasteriterator.py
│   │   ├── qgsrasterdrawer.py
│   │   ├── qgsrasterlayer.py
│   │   └── qgsrasterlayerrenderer.py
│   ├── vector/
│   │   ├── __init__.py
│   │   ├── qgsvectordataprovider.py
│   │   ├── qgsmemoryprovider.py
│   │   ├── qgsvectorlayer.py
│   │   ├── qgsvectorlayerrenderer.py
│   │   ├── qgsvectorlayereditbuffer.py
│   │   └── qgsvectorlayereditutils.py
│   ├── symbology/
│   │   ├── __init__.py
│   │   ├── qgssymbol.py
│   │   ├── qgsmarkersymbol.py
│   │   ├── qgslinesymbol.py
│   │   ├── qgsfillsymbol.py
│   │   ├── qgssymbollayer.py
│   │   ├── qgssimplemarkersymbollayer.py
│   │   ├── qgssimplelinesymbollayer.py
│   │   ├── qgssimplefillsymbollayer.py
│   │   ├── qgsrenderer.py
│   │   ├── qgssinglesymbolrenderer.py
│   │   ├── qgscategorizedsymbolrenderer.py
│   │   ├── qgsgraduatedsymbolrenderer.py
│   │   └── qgsrulebasedrenderer.py
│   ├── labeling/
│   │   ├── __init__.py
│   │   ├── qgslabelingengine.py
│   │   ├── qgspallabeling.py
│   │   └── qgsvectorlayerlabelprovider.py
│   ├── layertree/
│   │   ├── __init__.py
│   │   ├── qgslayertreenode.py
│   │   ├── qgslayertreegroup.py
│   │   └── qgslayertreelayer.py
│   └── scalebar/
│       ├── __init__.py
│       ├── qgsscalebarrenderer.py
│       └── qgsscalebarsettings.py
├── gui/
│   ├── __init__.py
│   ├── qgsmapcanvas.py
│   ├── qgsmapcanvasmap.py
│   ├── qgsmaptool.py
│   ├── qgsmaptoolpan.py
│   ├── qgsmaptoolzoom.py
│   ├── qgsmaptoolidentify.py
│   ├── qgsmaptoolemitpoint.py
│   ├── qgssplash.py
│   ├── qgsagentdock.py
│   ├── qgsprojectcrsdialog.py
│   ├── qgspropertiesdialog.py
│   └── layertree/
│       ├── __init__.py
│       ├── qgslayertreebridge.py
│       ├── qgslayertreemodel.py
│       ├── qgslayertreeview.py
│       └── qgslayertreewidget.py
├── providers/
│   ├── __init__.py
│   ├── gdal/
│   │   ├── __init__.py
│   │   └── qgsgdalprovider.py
│   └── ogr/
│       ├── __init__.py
│       └── qgsogrprovider.py
├── tools/
│   ├── __init__.py
│   ├── registry.py
│   ├── gis/
│   │   ├── __init__.py
│   │   └── ...
│   ├── gdal/
│   │   ├── __init__.py
│   │   └── ...
│   └── remote_sensing/
│       ├── __init__.py
│       └── ...
├── app/
│   ├── __init__.py
│   └── qgisapp.py
├── src/
│   └── raster_ops.cpp
└── tests/
```

---

## Migration Notes

### Files to DELETE (replaced by new QGIS-aligned versions):
- `core/qgsreader.py` → absorbed into `providers/gdal/qgsgdalprovider.py`
- `core/raster/qgsrasterlayerrenderer.py` → replaced by new version with QgsRasterPipe/QgsRasterIterator/QgsRasterDrawer
- `core/raster/qgsrasterrenderer.py` → replaced by QgsRasterRenderer subclasses with block() interface
- `core/vector/qgsvectorrenderer.py` → replaced by symbology/ renderers
- `core/vector/qgsvectorlayerrenderer.py` → replaced by new version with QgsFeatureRenderer
- `core/qgsmapsettings.py` → replaced by new version with QgsMapToPixel
- `core/qgscoordinatetransform.py` → replaced by new version with pyproj.CRS
- `gui/qgsmapcanvas.py` → replaced by QGraphicsView version
- `gui/qgsmaprendererjob.py` → replaced by parallel/sequential job variants
- `gui/qgsmaptool.py` → replaced by QGIS-style canvasPressEvent API
- `gui/qgsmaptoolpan.py` → replaced by QGIS-style API

### Files to KEEP (as-is or with minor changes):
- `agent/executor.py` — no changes needed
- `analysis/` — keep, migrate to `tools/` structure
- `app/qgisapp.py` — update imports, keep application logic
- `gui/qgssplash.py` — keep
- `gui/qgsagentdock.py` — keep
- `gui/qgsprojectcrsdialog.py` — keep
- `gui/qgspropertiesdialog.py` — update to use new API
- `gui/layertree/` — update imports
- `src/raster_ops.cpp` — keep, still used for performance-critical paths
- `tests/` — update imports

### C++ Extensions (raster_ops.cpp):
- `warp_and_compose_rgb()` — used by QgsMultiBandColorRenderer
- `warp_and_stretch_gray()` — used by QgsSingleBandGrayRenderer
- `stretch_and_compose_rgb()` — used for no-warp path
- `stretch_gray()` — used for no-warp path
- `warp_raster_band()` — used by QgsSingleBandPseudoColorRenderer
- `compute_pca()` — used by OTB pansharpening
