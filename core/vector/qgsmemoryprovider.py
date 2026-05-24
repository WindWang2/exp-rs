"""QgsMemoryProvider - in-memory vector data provider for scratch/editing layers.

Stores features in a plain dict keyed by FID.  Provides a thread-safe
snapshot on iteration (features list is copied).
"""

import copy

from core.qgsfields import QgsFields
from core.qgsfeature import QgsFeature
from core.qgsrectangle import QgsRectangle
from core.qgsfeatureiterator import QgsFeatureIterator
from core.qgsfeaturerequest import QgsFeatureRequest
from core.qgswkbtypes import QgsWkbTypes
from core.vector.qgsvectordataprovider import QgsVectorDataProvider


class QgsMemoryProvider(QgsVectorDataProvider):
    """In-memory vector provider.

    Features are stored in ``self._features: dict[int, QgsFeature]``.
    FIDs are auto-incremented when a feature with id == 0 is added.
    """

    def __init__(self, fields: QgsFields = None, wkb_type: int = QgsWkbTypes.Type.Unknown):
        self._fields: QgsFields = fields if fields is not None else QgsFields()
        self._wkb_type: int = wkb_type
        self._features: dict[int, QgsFeature] = {}
        self._next_fid: int = 1

    # ------------------------------------------------------------------
    # Read interface
    # ------------------------------------------------------------------

    def sourceName(self) -> str:
        return "memory"

    def sourceCrs(self):
        return None

    def fields(self) -> QgsFields:
        return self._fields

    def wkbType(self) -> int:
        return self._wkb_type

    def featureCount(self) -> int:
        return len(self._features)

    def sourceExtent(self) -> QgsRectangle:
        """Compute the bounding box of all features with geometry."""
        xmin = ymin = float('inf')
        xmax = ymax = float('-inf')
        for feat in self._features.values():
            geom = feat.geometry()
            if geom is not None and not geom.isNull():
                bb = geom.boundingBox()
                if not bb.isEmpty():
                    xmin = min(xmin, bb.xMinimum())
                    ymin = min(ymin, bb.yMinimum())
                    xmax = max(xmax, bb.xMaximum())
                    ymax = max(ymax, bb.yMaximum())
        if xmin == float('inf'):
            return QgsRectangle()
        return QgsRectangle(xmin, ymin, xmax, ymax)

    def getFeatures(self, request=None) -> QgsFeatureIterator:
        """Return an iterator over stored features, applying *request* filters.

        A snapshot (list copy) of the matching features is returned so that
        concurrent mutations to the store do not affect the iterator.
        """
        # Start from a snapshot of all features
        result = list(self._features.values())

        if request is not None:
            ft = request.filterType()

            if ft == QgsFeatureRequest.FilterType.FilterFid:
                fid = request.filterFid()
                result = [f for f in result if f.id() == fid]

            elif ft == QgsFeatureRequest.FilterType.FilterFids:
                fids = request.filterFids()
                result = [f for f in result if f.id() in fids]

            elif ft == QgsFeatureRequest.FilterType.FilterRect:
                rect = request.filterRect()
                if rect is not None:
                    filtered = []
                    for f in result:
                        geom = f.geometry()
                        if geom is not None and not geom.isNull():
                            bb = geom.boundingBox()
                            if rect.intersects(bb):
                                filtered.append(f)
                    result = filtered

            # Apply limit if set
            limit = request.limit()
            if limit >= 0:
                result = result[:limit]

        return QgsFeatureIterator(result)

    # ------------------------------------------------------------------
    # Write interface
    # ------------------------------------------------------------------

    def addFeature(self, feature) -> bool:
        """Add a single feature.  Auto-assigns FID if the feature's id is 0."""
        if feature.id() == 0:
            feature.setId(self._next_fid)
            self._next_fid += 1
        else:
            # Keep _next_fid ahead of any manually assigned id
            if feature.id() >= self._next_fid:
                self._next_fid = feature.id() + 1
        self._features[feature.id()] = feature
        return True

    def deleteFeature(self, fid) -> bool:
        """Remove a feature by FID.  Returns False if not found."""
        if fid in self._features:
            del self._features[fid]
            return True
        return False

    def changeGeometry(self, fid, geometry) -> bool:
        """Replace the geometry of the feature with the given FID."""
        feat = self._features.get(fid)
        if feat is None:
            return False
        feat.setGeometry(geometry)
        return True

    def changeAttributeValues(self, fid, attribute_map: dict) -> bool:
        """Update attributes of the feature with the given FID.

        *attribute_map* maps field index (int) -> new value.
        """
        feat = self._features.get(fid)
        if feat is None:
            return False
        ok = True
        for idx, value in attribute_map.items():
            if not feat.setAttribute(idx, value):
                ok = False
        return ok

    # ------------------------------------------------------------------
    # Editing lifecycle
    # ------------------------------------------------------------------

    def startEditing(self) -> bool:
        """Memory provider is always editable; this is a no-op returning True."""
        return True

    def commitChanges(self) -> bool:
        """Memory provider commits are instant (no-op)."""
        return True

    def rollback(self) -> bool:
        """No-op for memory provider (no undo stack yet)."""
        return True

    def isEditable(self) -> bool:
        return True

    # ------------------------------------------------------------------
    # Concrete overrides
    # ------------------------------------------------------------------

    def clone(self) -> 'QgsMemoryProvider':
        """Return a deep copy of this provider including all stored features."""
        new_prov = QgsMemoryProvider(fields=copy.deepcopy(self._fields),
                                     wkb_type=self._wkb_type)
        for fid, feat in self._features.items():
            new_feat = copy.deepcopy(feat)
            new_prov._features[fid] = new_feat
        new_prov._next_fid = self._next_fid
        return new_prov
