import threading

from PySide6.QtCore import QRunnable, Signal, QObject, Qt as QtCore_Qt
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
        self._done_event = threading.Event()  # Signals when run() has fully completed

        # Create ONE QgsRenderContext for this entire render pass.
        # All layer renderers will share this context instead of each
        # creating their own, matching QGIS's design.
        self.render_context = QgsRenderContext.fromMapSettings(settings)

        # Instantiate thread-safe decoupled layer renderers on the main GUI thread!
        # This mirrors the design of QgsMapRendererJob in QGIS C++.
        self.renderers = []
        for layer in settings.layers:
            if self._is_canceled:
                break
            if layer.visible:
                renderer = layer.createMapRenderer(settings)
                if renderer:
                    self.renderers.append(renderer)

    def run(self):
        try:
            # Create output buffer
            if not self.settings.output_size or self._is_canceled:
                return

            image = QImage(self.settings.output_size, QImage.Format_ARGB32)
            if image.isNull():
                from core.logger import log_error
                log_error("MapRendererJob: Failed to create output QImage")
                return
            image.fill(Qt.transparent)

            if self._is_canceled:
                return

            painter = QPainter(image)
            if not painter.isActive():
                from core.logger import log_error
                log_error("MapRendererJob: Failed to create QPainter")
                return
            painter.setRenderHint(QPainter.Antialiasing)

            # Set the painter on the render context for this pass
            self.render_context.setPainter(painter)

            for renderer in self.renderers:
                if self._is_canceled:
                    break
                try:
                    painter.save()
                    renderer.render(painter, self.settings, self.render_context)
                    painter.restore()
                except Exception as e:
                    from core.logger import log_error
                    log_error(f"MapRendererJob: Error rendering layer {renderer.layerId()}: {e}")
                    painter.restore()  # Ensure restore even on error

            painter.end()

            if not self._is_canceled:
                # Keep a reference so the QImage data stays alive until the
                # main thread has consumed it via the signal.
                self._last_image = image
                self.signals.finished.emit(image)
        except Exception as e:
            from core.logger import log_error
            log_error(f"MapRendererJob: Fatal error in render thread: {e}")
        finally:
            self._done_event.set()

    def cancel(self):
        self._is_canceled = True

    def waitForFinished(self, timeout_ms: int = 2000) -> bool:
        """Block until run() has completed.  Returns True if it finished
        within *timeout_ms*, False on timeout."""
        return self._done_event.wait(timeout=timeout_ms / 1000.0)


MapRendererJob = QgsMapRendererJob
