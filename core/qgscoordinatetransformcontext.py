"""QgsCoordinateTransformContext - QGIS-compatible transform context."""
from PySide6.QtCore import QObject


class QgsCoordinateTransformContext(QObject):
    """Stores a set of known source->destination CRS datum transforms."""

    def __init__(self):
        super().__init__()
        self._transforms = {}

    def addSourceDestinationCrs(self, sourceCrs, destCrs, datumTransform=None):
        key = (sourceCrs.authid(), destCrs.authid())
        self._transforms[key] = datumTransform

    def hasTransform(self, sourceCrs, destCrs) -> bool:
        key = (sourceCrs.authid(), destCrs.authid())
        return key in self._transforms

    def removeSourceDestinationCrs(self, sourceCrs, destCrs):
        key = (sourceCrs.authid(), destCrs.authid())
        self._transforms.pop(key, None)

    def sourceDestinationCrsList(self):
        return list(self._transforms.keys())
