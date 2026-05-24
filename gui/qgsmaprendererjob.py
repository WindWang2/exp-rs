from PySide6.QtCore import QRunnable, Signal, QObject
from PySide6.QtGui import QImage, QPainter, Qt

from core.qgsrendercontext import QgsRenderContext


class JobSignals(QObject):
    finished = Signal(QImage)

class QgsMapRendererJob(QRunnable):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.signals = JobSignals()
        self._is_canceled = False

        # Create ONE QgsRenderContext for this entire render pass.
        # All layer renderers will share this context instead of each
        # creating their own, matching QGIS's design.
        self.render_context = QgsRenderContext.fromMapSettings(settings)

        # Instantiate thread-safe decoupled layer renderers on the main GUI thread!
        # This mirrors the design of QgsMapRendererJob in QGIS C++.
        self.renderers = []
        for layer in settings.layers:
            if layer.visible:
                renderer = layer.createMapRenderer(settings)
                if renderer:
                    self.renderers.append(renderer)

    def run(self):
        # Create output buffer
        if not self.settings.output_size:
            return

        image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        image.fill(Qt.transparent)

        painter = QPainter(image)
        painter.setRenderHint(QPainter.Antialiasing)

        # Set the painter on the render context for this pass
        self.render_context.setPainter(painter)

        for renderer in self.renderers:
            if self._is_canceled:
                break
            painter.save()
            renderer.render(painter, self.settings, self.render_context)
            painter.restore()

        painter.end()

        self._last_image = image  # Allow inline access for preview renders

        if not self._is_canceled:
            self.signals.finished.emit(image)

    def cancel(self):
        self._is_canceled = True


MapRendererJob = QgsMapRendererJob
