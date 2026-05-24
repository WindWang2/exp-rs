from PySide6.QtGui import QTransform
from PySide6.QtCore import QSize

from core.qgsrectangle import QgsRectangle

class QgsMapSettings:
    def __init__(self):
        self.layers = []
        self.extent = None  # QgsRectangle in world coordinates (GIS: Y increases UP)
        self.output_size = None  # Device coordinates (Qt: Y increases DOWN)
        self.destination_crs = "EPSG:3857"

    def worldToDevice(self) -> QTransform:
        """
        Calculates the transformation matrix from world coordinates to device coordinates.
        GIS coordinates: Y increases upwards.
        Device coordinates: Y increases downwards.
        """
        if self.extent is None or self.output_size is None:
            return QTransform()

        if self.extent.isEmpty() or not self.output_size.isValid():
            return QTransform()

        world_width = self.extent.width()
        world_height = self.extent.height()
        device_width = self.output_size.width()
        device_height = self.output_size.height()

        s_x = device_width / world_width
        s_y = device_height / world_height

        # QgsRectangle: top() = yMaximum (GIS convention, max Y)
        # Map world top-left (left, top) → device (0, 0)
        transform = QTransform()
        transform.scale(s_x, -s_y)
        transform.translate(-self.extent.left(), -self.extent.top())

        return transform

    def deviceToWorld(self) -> QTransform:
        """
        Calculates the transformation matrix from device coordinates to world coordinates.
        """
        transform, success = self.worldToDevice().inverted()
        if success:
            return transform
        return QTransform()


MapSettings = QgsMapSettings
