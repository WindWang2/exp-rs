"""QgsRasterLayer — Raster map layer backed by a QgsRasterPipe pipeline."""

from core.qgsmaplayer import QgsMapLayer
from core.raster.qgsrasterpipe import QgsRasterPipe
from core.raster.qgsrasterdataprovider import QgsRasterDataProvider
from PySide6.QtCore import QRectF


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

        # Raw extent in native projection (QRectF)
        self.raw_extent = QRectF(
            ext.xMinimum(), ext.yMaximum(),
            ext.width(), ext.height(),
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
    def extent(self) -> QRectF:
        """Returns the extent reprojected into the current Project CRS."""
        from core.qgsproject import QgsProject
        return self.extentInCrs(QgsProject.instance().crs())

    def extentInCrs(self, dest_crs: str) -> QRectF:
        """Returns the layer extent reprojected into *dest_crs*."""
        ext = self._provider.extent()  # QgsRectangle
        raw_ext = QRectF(
            ext.xMinimum(), ext.yMaximum(),
            ext.width(), ext.height(),
        )
        if self.crs and dest_crs and self.crs != dest_crs:
            from core.qgscoordinatetransform import QgsCoordinateTransform
            try:
                transformer = QgsCoordinateTransform(self.crs, dest_crs)
                xmin, ymin, xmax, ymax = transformer.transform_bounds(
                    ext.xMinimum(), ext.yMinimum(),
                    ext.xMaximum(), ext.yMaximum()
                )
                return QRectF(xmin, ymax, xmax - xmin, ymax - ymin)
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
