"""QgsRasterPipe — 7-stage raster processing pipeline container.

Mirrors the QGIS QgsRasterPipe architecture:
    DataProvider → Nuller → Projector → ResampleFilter → BrightnessContrast → HueSaturation → Renderer

Only DataProvider and Renderer are functional now; the middle 5 stages
are optional passthrough slots (can be None, meaning "skip this stage").
"""

from core.raster.qgsrasterinterface import QgsRasterInterface
from core.raster.qgsrasterblock import QgsRasterBlock


# Ordered list of all pipeline stage types
_PIPE_STAGES = [
    QgsRasterInterface.InterfaceType.Provider,
    QgsRasterInterface.InterfaceType.Nuller,
    QgsRasterInterface.InterfaceType.Reprojector,
    QgsRasterInterface.InterfaceType.Resampler,
    QgsRasterInterface.InterfaceType.Brightness,
    QgsRasterInterface.InterfaceType.HueSaturation,
    QgsRasterInterface.InterfaceType.Renderer,
]


class QgsRasterPipe:
    """A container that holds a chain of QgsRasterInterface objects
    representing the raster processing pipeline.

    The pipe has 7 fixed slots (matching QGIS):
      0  Provider       — data source (required for block())
      1  Nuller         — optional filter
      2  Projector      — optional reprojector
      3  ResampleFilter — optional resampler
      4  Brightness     — optional brightness/contrast
      5  HueSaturation  — optional hue/saturation
      6  Renderer       — styling/renderer (NOT a QgsRasterInterface in our design)

    Only DataProvider and Renderer need to be functional now.
    The middle stages are placeholders for future expansion.
    """

    def __init__(self):
        # Each slot holds an interface object (or None).
        self._interfaces: dict[int, object | None] = {s: None for s in _PIPE_STAGES}
        # Track which stages are enabled.
        self._enabled: dict[int, bool] = {s: False for s in _PIPE_STAGES}

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def set(self, interface) -> bool:
        """Auto-detect the stage type from *interface* and place it in the
        correct slot.  Returns True on success.

        The object must be a QgsRasterInterface (so we can call .type()).
        Renderers are NOT QgsRasterInterface objects — use setRenderer()
        for those.
        """
        if interface is None:
            return False
        if not isinstance(interface, QgsRasterInterface):
            return False

        stage = interface.type()
        if stage not in self._interfaces:
            return False

        self._interfaces[stage] = interface
        self._enabled[stage] = True
        return True

    def setRenderer(self, renderer) -> None:
        """Place a renderer in the Renderer slot.

        Renderers do NOT extend QgsRasterInterface in our design, so they
        cannot be auto-detected by set().  Use this method instead.
        """
        self._interfaces[QgsRasterInterface.InterfaceType.Renderer] = renderer
        if renderer is not None:
            self._enabled[QgsRasterInterface.InterfaceType.Renderer] = True
        else:
            self._enabled[QgsRasterInterface.InterfaceType.Renderer] = False

    def at(self, type: int):
        """Return the interface at the given stage type, or None."""
        return self._interfaces.get(type)

    def provider(self):
        """Shortcut to get the DataProvider."""
        return self._interfaces.get(QgsRasterInterface.InterfaceType.Provider)

    def renderer(self):
        """Shortcut to get the Renderer."""
        return self._interfaces.get(QgsRasterInterface.InterfaceType.Renderer)

    def on(self, type: int) -> bool:
        """Return True if the stage is enabled."""
        return self._enabled.get(type, False)

    def setOn(self, type: int, enabled: bool) -> None:
        """Enable or disable a stage."""
        if type in self._enabled:
            self._enabled[type] = enabled

    def setOff(self, type: int) -> None:
        """Disable a stage."""
        self.setOn(type, False)

    def block(self, band_no: int, extent, width: int, height: int,
              feedback=None) -> QgsRasterBlock:
        """Read from the provider and pass through enabled intermediate stages.

        The Renderer is NOT called here — it is invoked separately by
        QgsRasterLayerRenderer.  This method only runs the data pipeline
        (Provider through the middle filter stages).
        """
        provider = self.provider()
        if provider is None:
            return QgsRasterBlock()
        if not self.on(QgsRasterInterface.InterfaceType.Provider):
            return QgsRasterBlock()

        # Read from provider
        result = provider.block(band_no, extent, width, height, feedback)
        if result.isEmpty():
            return result

        # Pass through enabled intermediate stages (indices 1..5).
        # Currently all middle stages are None (passthrough), so this
        # loop is a no-op placeholder for future filter implementations.
        for stage in _PIPE_STAGES[1:-1]:
            if not self._enabled.get(stage, False):
                continue
            iface = self._interfaces.get(stage)
            if iface is not None and hasattr(iface, 'block'):
                result = iface.block(band_no, extent, width, height, feedback)
                if result.isEmpty():
                    return result

        return result

    def clone(self) -> 'QgsRasterPipe':
        """Return a deep copy of the pipe.

        QgsRasterInterface objects are cloned via their own clone() method.
        The Renderer is shallow-copied (it doesn't extend QgsRasterInterface).
        """
        cloned = QgsRasterPipe()
        for stage in _PIPE_STAGES:
            original = self._interfaces.get(stage)
            if original is None:
                cloned._interfaces[stage] = None
            elif isinstance(original, QgsRasterInterface):
                cloned._interfaces[stage] = original.clone()
            else:
                # Renderer (not a QgsRasterInterface) — shallow copy
                cloned._interfaces[stage] = original
            cloned._enabled[stage] = self._enabled.get(stage, False)
        return cloned
