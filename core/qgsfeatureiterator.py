"""QgsFeatureIterator - iterable wrapper for traversing features."""

from abc import ABC, abstractmethod

from core.qgsfeature import QgsFeature
from core.qgsfeaturerequest import QgsFeatureRequest


class QgsAbstractFeatureIterator(ABC):
    """Abstract base class for feature iterators.

    Subclasses must implement nextFeature() to provide feature-by-feature
    iteration semantics matching the QGIS C++ API.
    """

    @abstractmethod
    def nextFeature(self) -> tuple:
        """Returns (True, feature) or (False, None) when exhausted."""
        pass

    def rewind(self):
        """Reset iterator to the beginning. Default is no-op."""
        pass

    def close(self):
        """Release resources. Default is no-op."""
        pass

    def isValid(self) -> bool:
        return True


class QgsFeatureIterator:
    """Python iterator over a list of QgsFeature objects.

    Supports both the Python iterator protocol (for/next) and the
    QGIS-style nextFeature() call.
    """

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
        """QGIS-style iteration: returns (True, feature) or (False, None)."""
        try:
            feat = self.__next__()
            return (True, feat)
        except StopIteration:
            return (False, None)

    def close(self):
        self._closed = True
