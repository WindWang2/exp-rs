"""
C++ QgsMapTool wrappers - wrap QGIS C++ map tools for Python.

This replaces the pure-Python map tool implementation with the native
QGIS C++ tools (QgsMapToolPan, QgsMapToolZoom, etc.).
"""

import _antigravity_core as core


class MapTool:
    """
    Base wrapper for C++ QgsMapTool objects.

    Provides a Python interface compatible with the old pure-Python
    MapTool class, wrapping the C++ QgsMapTool implementations.
    """

    def __init__(self, canvas, qgs_tool):
        """
        Initialize with a canvas and a C++ QgsMapTool instance.

        Args:
            canvas: MapCanvas wrapper instance
            qgs_tool: C++ QgsMapTool instance (e.g., QgsMapToolPan)
        """
        self._canvas = canvas
        self._qgs_tool = qgs_tool

    def canvas(self):
        """Return the canvas this tool is attached to."""
        return self._canvas

    def activate(self):
        """Activate the tool."""
        if self._qgs_tool:
            self._qgs_tool.activate()

    def deactivate(self):
        """Deactivate the tool."""
        if self._qgs_tool:
            self._qgs_tool.deactivate()

    @property
    def qgs_tool(self):
        """Access the underlying C++ QgsMapTool for advanced usage."""
        return self._qgs_tool


class MapToolPan(MapTool):
    """
    Wrapper for C++ QgsMapToolPan.

    Provides panning functionality by dragging the mouse.
    """

    def __init__(self, canvas):
        """
        Create a pan tool for the given canvas.

        Args:
            canvas: MapCanvas wrapper instance
        """
        qgs_pan = core.QgsMapToolPan(canvas.qgs_canvas)
        super().__init__(canvas, qgs_pan)

    def activate(self):
        """Activate the pan tool."""
        if self._qgs_tool:
            self._qgs_tool.activate()

    def deactivate(self):
        """Deactivate the pan tool."""
        if self._qgs_tool:
            self._qgs_tool.deactivate()


class MapToolZoom(MapTool):
    """
    Wrapper for C++ QgsMapToolZoom.

    Provides zoom in/out functionality by dragging a rectangle.
    """

    def __init__(self, canvas, zoom_out=False):
        """
        Create a zoom tool for the given canvas.

        Args:
            canvas: MapCanvas wrapper instance
            zoom_out: If True, zoom out; if False, zoom in
        """
        qgs_zoom = core.QgsMapToolZoom(canvas.qgs_canvas, zoom_out)
        super().__init__(canvas, qgs_zoom)

    def activate(self):
        """Activate the zoom tool."""
        if self._qgs_tool:
            self._qgs_tool.activate()

    def deactivate(self):
        """Deactivate the zoom tool."""
        if self._qgs_tool:
            self._qgs_tool.deactivate()
