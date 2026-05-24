"""QgsFeatureRequest - defines filtering criteria for feature iteration."""

import enum


class QgsFeatureRequest:
    """Encapsulates filtering parameters for feature queries.

    Mirrors the QGIS QgsFeatureRequest API, supporting filter-by-rect,
    filter-by-FID, filter-by-FIDs, and subset-of-attributes selection.
    """

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

    def filterType(self) -> 'QgsFeatureRequest.FilterType':
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

    def filterFid(self):
        return self._filter_fid

    def setFilterFids(self, fids: set) -> 'QgsFeatureRequest':
        self._filter_type = self.FilterType.FilterFids
        self._filter_fids = fids
        return self

    def filterFids(self):
        return self._filter_fids

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
