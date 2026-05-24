"""QgsVectorDataProvider - abstract base class for vector data providers.

Extends QgsFeatureSource and QgsFeatureSink to define the full contract
for reading and writing vector features (QGIS-style).
"""

import enum
from abc import abstractmethod

from core.qgsfeaturesource import QgsFeatureSource
from core.qgsfeaturesink import QgsFeatureSink


class Capabilities(enum.IntFlag):
    """Bitfield describing what write operations a provider supports."""
    NoCapabilities = 0
    AddFeatures = 1
    DeleteFeatures = 2
    ChangeGeometries = 4
    ChangeAttributeValues = 8


class QgsVectorDataProvider(QgsFeatureSource, QgsFeatureSink):
    """Abstract base class for vector data providers.

    Combines QgsFeatureSource (read) and QgsFeatureSink (write) with
    editing lifecycle methods.  Concrete subclasses must implement all
    abstract methods.
    """

    # ---- Abstract read methods (from QgsFeatureSource) ----

    @abstractmethod
    def getFeatures(self, request=None):
        """Return a QgsFeatureIterator, optionally filtered by *request*."""
        pass

    @abstractmethod
    def featureCount(self) -> int:
        pass

    @abstractmethod
    def fields(self):
        """Return the QgsFields schema."""
        pass

    @abstractmethod
    def wkbType(self) -> int:
        pass

    @abstractmethod
    def sourceName(self) -> str:
        pass

    @abstractmethod
    def sourceExtent(self):
        """Return a QgsRectangle covering all features."""
        pass

    def sourceCrs(self):
        return None

    # ---- Abstract write methods (from QgsFeatureSink + extensions) ----

    @abstractmethod
    def addFeature(self, feature) -> bool:
        pass

    @abstractmethod
    def deleteFeature(self, fid) -> bool:
        pass

    @abstractmethod
    def changeGeometry(self, fid, geometry) -> bool:
        pass

    @abstractmethod
    def changeAttributeValues(self, fid, attribute_map: dict) -> bool:
        pass

    # ---- Abstract editing lifecycle ----

    @abstractmethod
    def startEditing(self) -> bool:
        pass

    @abstractmethod
    def commitChanges(self) -> bool:
        pass

    @abstractmethod
    def rollback(self) -> bool:
        pass

    @abstractmethod
    def isEditable(self) -> bool:
        pass

    # ---- Concrete methods ----

    def capabilities(self) -> int:
        """Return a Capabilities bitfield.  Override to restrict."""
        return (Capabilities.AddFeatures
                | Capabilities.DeleteFeatures
                | Capabilities.ChangeGeometries
                | Capabilities.ChangeAttributeValues)

    def clone(self) -> 'QgsVectorDataProvider':
        """Return a deep copy of this provider.  Override if needed."""
        import copy
        return copy.deepcopy(self)
