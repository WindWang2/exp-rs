from PySide6.QtCore import Qt, Signal, QObject
from PySide6.QtGui import QCursor

from gui.qgsmaptool import QgsMapTool
from core.qgspointxy import QgsPointXY


class QgsMapToolIdentify(QgsMapTool):
    """Click to identify features at a map point. Emits identifySignal with the clicked QgsPointXY."""

    class _Signals(QObject):
        """QObject holder — QgsMapTool is not a QObject, so signals live here."""
        identifySignal = Signal(QgsPointXY)

    _signals_instance = None

    @classmethod
    def _get_signals(cls):
        if cls._signals_instance is None:
            cls._signals_instance = cls._Signals()
        return cls._signals_instance

    def __init__(self, canvas):
        super().__init__(canvas)
        impl = self._get_signals()
        # Bind the signal directly on *this* instance for convenient access
        self.identifySignal = impl.identifySignal

    def activate(self):
        self._canvas.setCursor(QCursor(Qt.CrossCursor))

    def deactivate(self):
        self._canvas.setCursor(QCursor(Qt.ArrowCursor))

    def canvasPressEvent(self, event):
        if event.button() == Qt.LeftButton:
            map_point = self.toMapCoordinates(event.position().toPoint())
            self.identifySignal.emit(map_point)


MapToolIdentify = QgsMapToolIdentify
