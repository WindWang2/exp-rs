"""QgsRasterLayer — Raster map layer backed by a QgsRasterPipe pipeline."""

from core.qgsmaplayer import QgsMapLayer
from core.raster.qgsrasterpipe import QgsRasterPipe
from core.raster.qgsrasterdataprovider import QgsRasterDataProvider
from core.qgsrectangle import QgsRectangle


class QgsRasterLayer(QgsMapLayer):
    """Map layer for displaying raster data.

    Internally owns a QgsRasterPipe that holds the DataProvider and
    (optionally) a Renderer.  Backward-compatible: ``layer.provider``
    still works as a property that delegates to ``layer.pipe().provider()``.

    Supports advanced rendering (Multiband Color, Singleband Gray,
    Singleband Pseudocolor) and On-The-Fly (OTF) reprojection.
    """

    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)

        # --- Build the pipeline ---
        self._pipe = QgsRasterPipe()
        self._provider = QgsRasterDataProvider(uri)
        self._pipe.set(self._provider)

        # --- Layer metadata from provider ---
        ext = self._provider.extent()  # QgsRectangle
        self.crs = self._provider.reader.metadata.get("crs")

        # Register CRS with transform cache on main thread (prevents segfaults)
        if self.crs:
            from core.qgstransformcache import transform_cache
            transform_cache().register_layer_crs(self.crs)

        # Raw extent in native projection (QgsRectangle)
        self.raw_extent = QgsRectangle(
            ext.xMinimum(), ext.yMinimum(),
            ext.xMaximum(), ext.yMaximum(),
        )

        # --- Styling attributes ---
        band_count = self._provider.reader.metadata.get("count", 1)
        self.render_type = "multiband" if band_count >= 3 else "grayscale"
        self.red_band = 1
        self.green_band = 2 if band_count >= 2 else 1
        self.blue_band = 3 if band_count >= 3 else 1
        self.gray_band = 1
        self.pseudocolor_band = 1
        self.color_ramp = "viridis"

        # QGIS-aligned Contrast Stretch options
        self.contrast_enhancement = "stretch_to_min_max"
        self.min_max_limits_method = "cumulative_cut"
        self.cumulative_cut_lower = 2.0
        self.cumulative_cut_upper = 98.0
        self.std_dev_factor = 2.0
        self.user_min = None
        self.user_max = None

        # --- Create and attach the appropriate renderer to the pipe ---
        self._create_and_attach_renderer()

        # --- C++ layer for QgsMapCanvas integration ---
        self._qgs_layer_cached = None  # Lazy-loaded C++ QgsRasterLayer

    # ------------------------------------------------------------------
    # C++ Integration
    # ------------------------------------------------------------------

    @property
    def _qgs_layer(self):
        """
        Get the C++ QgsRasterLayer for use with QgsMapCanvas.

        This is lazy-created when first accessed.
        """
        if self._qgs_layer_cached is None:
            import _antigravity_core as core
            # Create C++ QgsRasterLayer using the file URI
            # C++ signature: QgsRasterLayer(path: str, name: str = 'raster')
            self._qgs_layer_cached = core.QgsRasterLayer(
                self._provider._uri,  # file path
                self.name             # layer name
            )
        return self._qgs_layer_cached

    # ------------------------------------------------------------------
    # Pipeline access
    # ------------------------------------------------------------------

    def pipe(self) -> QgsRasterPipe:
        """Return the raster processing pipeline."""
        return self._pipe

    # Backward-compatible provider accessor (property)
    @property
    def provider(self):
        """Backward-compatible accessor — delegates to the pipe's provider."""
        return self._pipe.provider()

    # ------------------------------------------------------------------
    # Extent
    # ------------------------------------------------------------------

    @property
    def extent(self) -> QgsRectangle:
        """Returns the extent reprojected into the current Project CRS."""
        from core.qgsproject import QgsProject
        return self.extentInCrs(QgsProject.instance().crs())

    def extentInCrs(self, dest_crs: str) -> QgsRectangle:
        """Returns the layer extent reprojected into *dest_crs*."""
        ext = self._provider.extent()  # QgsRectangle
        raw_ext = QgsRectangle(
            ext.xMinimum(), ext.yMinimum(),
            ext.xMaximum(), ext.yMaximum(),
        )
        # If layer has no CRS, treat it as being in the destination CRS
        layer_crs = self.crs if self.crs else dest_crs
        if layer_crs and dest_crs and layer_crs != dest_crs:
            from core.qgstransformcache import transform_cache
            try:
                transformer = transform_cache().get_transform(self.crs, dest_crs)
                if transformer and transformer.isValid():
                    xmin, ymin, xmax, ymax = transformer.transform_bounds(
                        ext.xMinimum(), ext.yMinimum(),
                        ext.xMaximum(), ext.yMaximum()
                    )
                    return QgsRectangle(xmin, ymin, xmax, ymax)
            except Exception as e:
                print(f"Error reprojecting raster extent of {self.name} to {dest_crs}: {e}")
                return raw_ext
        return raw_ext

    # ------------------------------------------------------------------
    # Map renderer
    # ------------------------------------------------------------------

    def createMapRenderer(self, settings):
        """Creates a thread-safe RasterLayerRenderer backed by the pipe."""
        from core.raster.qgsrasterlayerrenderer import QgsRasterLayerRenderer
        return QgsRasterLayerRenderer(self, settings)

    # ------------------------------------------------------------------
    # Internal: renderer creation
    # ------------------------------------------------------------------

    def _create_and_attach_renderer(self):
        """Create the appropriate renderer based on render_type and
        attach it to the pipe."""
        from core.raster.qgsrasterrenderer import (
            QgsMultiBandColorRenderer,
            QgsSingleBandGrayRenderer,
            QgsSingleBandPseudoColorRenderer,
        )

        if self.render_type == "multiband":
            renderer = QgsMultiBandColorRenderer(self)
        elif self.render_type == "pseudocolor":
            renderer = QgsSingleBandPseudoColorRenderer(self)
        else:
            renderer = QgsSingleBandGrayRenderer(self)

        self._pipe.setRenderer(renderer)


RasterLayer = QgsRasterLayer
