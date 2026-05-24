# Phase 1 - Tier 1: Core Primitives
from core.qgis import Qgis
from core.qgspointxy import QgsPointXY
from core.qgsrectangle import QgsRectangle
from core.qgsvector import QgsVector
from core.qgswkbtypes import QgsWkbTypes
from core.qgsunittypes import QgsUnitTypes

# Phase 1 - Tier 2: Geometry
from core.qgsgeometry import QgsGeometry

# Phase 1 - Tier 3: Data Model
from core.qgsfield import QgsField
from core.qgsfields import QgsFields
from core.qgsfeature import QgsFeature
from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgsfeatureiterator import QgsFeatureIterator
from core.qgsfeaturesource import QgsFeatureSource
from core.qgsfeaturesink import QgsFeatureSink

# Phase 1 - Tier 4: CRS and Transform
from core.qgscoordinatereferencesystem import QgsCoordinateReferenceSystem
from core.qgscoordinatetransform import QgsCoordinateTransform, CRSTransformer
from core.qgscoordinatetransformcontext import QgsCoordinateTransformContext

# Backward-compat: legacy imports still used by existing code
from core.qgsreader import GeospatialReader
from core.qgsproject import QgsProject, GISProject
from core.qgsmaplayer import QgsMapLayer, MapLayer
from core.qgsmapsettings import QgsMapSettings, MapSettings
from core.qgsmaptopixel import QgsMapToPixel
from core.qgsrendercontext import QgsRenderContext, RenderFlag
from core.qgsmaplayerrenderer import QgsMapLayerRenderer, MapLayerRenderer
from core.qgsdataprovider import QgsDataProvider, DataProvider
from core.qgsmaplayerstore import QgsMapLayerStore

# Phase 2 - Relations
from core.qgsrelationmanager import QgsRelation, QgsRelationManager

# Phase 2 - Symbology
from core.symbology import (
    QgsSymbolLayer,
    QgsSimpleFillSymbolLayer,
    QgsSimpleLineSymbolLayer,
    QgsSimpleMarkerSymbolLayer,
    QgsSymbol,
)
