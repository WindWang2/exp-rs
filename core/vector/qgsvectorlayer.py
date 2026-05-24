from core.qgsmaplayer import QgsMapLayer, MapLayer
from providers.ogr.qgsvectordataprovider import OGRDataProvider
from core.vector.qgsvectorrenderer import QgsSingleSymbolRenderer, SingleSymbolRenderer
from PySide6.QtCore import QRectF

class QgsVectorLayer(QgsMapLayer):
    """
    Map layer for displaying vector data.
    Supports On-The-Fly (OTF) projection of geometry boundaries and coordinates.
    """
    def __init__(self, layer_id: str, name: str, uri: str):
        super().__init__(layer_id, name)
        self.provider = OGRDataProvider(uri)
        self.renderer = QgsSingleSymbolRenderer()
        self._labeling = None  # QgsPalLayerSettings or None
        ext = self.provider.extent()
        self.crs = self.provider.reader.metadata.get("crs")

        # Calculate raw extent in native projection coordinates
        self.raw_extent = QRectF(ext["left"], ext["top"], ext["right"] - ext["left"], ext["top"] - ext["bottom"])

    @property
    def extent(self) -> QRectF:
        """Returns the extent of the layer reprojected into the current Project CRS."""
        from core.qgsproject import QgsProject
        return self.extentInCrs(QgsProject.instance().crs())

    def extentInCrs(self, dest_crs: str) -> QRectF:
        """Returns the layer extent reprojected into the target CRS."""
        ext = self.provider.extent()
        raw_ext = QRectF(ext["left"], ext["top"], ext["right"] - ext["left"], ext["top"] - ext["bottom"])
        if self.crs and dest_crs and self.crs != dest_crs:
            from core.qgscoordinatetransform import QgsCoordinateTransform
            try:
                transformer = QgsCoordinateTransform(self.crs, dest_crs)
                xmin, ymin, xmax, ymax = transformer.transform_bounds(ext["left"], ext["bottom"], ext["right"], ext["top"])
                return QRectF(xmin, ymax, xmax - xmin, ymax - ymin)
            except Exception as e:
                print(f"Error reprojecting vector extent of {self.name} to {dest_crs}: {e}")
                return raw_ext
        return raw_ext

    def set_renderer(self, renderer):
        """
        Sets a new renderer for the layer.
        """
        self.renderer = renderer

    def setLabeling(self, settings):
        """Set the PAL labeling settings for this layer.

        Parameters
        ----------
        settings : QgsPalLayerSettings or None
            The labeling configuration. Pass None to disable labeling.
        """
        self._labeling = settings

    def labeling(self):
        """Return the current PAL labeling settings (or None if disabled).

        Returns
        -------
        QgsPalLayerSettings or None
        """
        return self._labeling

    def createMapRenderer(self, settings):
        """Creates a decoupled, thread-safe VectorLayerRenderer instance for background rendering."""
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer
        return QgsVectorLayerRenderer(self, settings, labeling=self._labeling)


VectorLayer = QgsVectorLayer
