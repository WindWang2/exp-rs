from PySide6.QtGui import QTransform
from PySide6.QtCore import QRectF, QSize

class MapSettings:
    def __init__(self):
        self.layers = []
        self.extent = QRectF() # World coordinates (GIS: Y increases UP)
        self.output_size = QSize() # Device coordinates (Qt: Y increases DOWN)
        self.destination_crs = "EPSG:3857"

    def worldToDevice(self) -> QTransform:
        """
        Calculates the transformation matrix from world coordinates to device coordinates.
        GIS coordinates: Y increases upwards.
        Device coordinates: Y increases downwards.
        """
        if not self.extent or not self.output_size or self.extent.isEmpty() or not self.output_size.isValid():
            return QTransform()
            
        world_width = self.extent.width()
        world_height = self.extent.height()
        device_width = self.output_size.width()
        device_height = self.output_size.height()
        
        # Scaling factors
        s_x = device_width / world_width
        s_y = device_height / world_height
        
        # We want to map world top-left (extent.left, extent.top) to device (0, 0).
        # And world bottom-right (extent.right, extent.bottom) to device (device_width, device_height).
        
        transform = QTransform()
        # 1. Scale. Note negative s_y because GIS Y is inverted compared to Qt
        transform.scale(s_x, -s_y)
        # 2. Translate world (left, top) to origin (0,0)
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
