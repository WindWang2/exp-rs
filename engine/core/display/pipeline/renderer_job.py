from PySide6.QtCore import QRunnable, Signal, QObject
from PySide6.QtGui import QImage, QPainter, Qt

class JobSignals(QObject):
    finished = Signal(QImage)

class MapRendererJob(QRunnable):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.signals = JobSignals()
        self._is_canceled = False
        
    def run(self):
        # Create output buffer
        if not self.settings.output_size:
            return
            
        image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        image.fill(Qt.transparent)
        
        painter = QPainter(image)
        # Enable antialiasing
        painter.setRenderHint(QPainter.Antialiasing)
        
        for layer in self.settings.layers:
            if self._is_canceled:
                break
            if layer.visible:
                # Save painter state before layer drawing
                painter.save()
                layer.draw(painter, self.settings)
                painter.restore()
        
        painter.end()
        
        if not self._is_canceled:
            self.signals.finished.emit(image)

    def cancel(self):
        self._is_canceled = True
