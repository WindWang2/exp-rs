from PySide6.QtCore import QRectF
from core.qgsmaplayerrenderer import QgsMapLayerRenderer


class _DictFeatureAdapter:
    """Lightweight adapter exposing QgsFeature-like interface over a dict feature.

    The label provider needs ``attribute(name)`` and ``geometry()`` which the
    raw dict does not provide.  This avoids allocating full QgsFeature objects.
    """

    __slots__ = ('_feat', '_geom')

    def __init__(self, feat: dict):
        self._feat = feat
        # Build a QgsGeometry from the shapely shape so the label provider
        # can call .centroid(), .boundingBox(), etc.
        from core.qgsgeometry import QgsGeometry
        shp = feat.get("shape")
        if shp is not None:
            self._geom = QgsGeometry.fromWkt(shp.wkt)
        else:
            self._geom = QgsGeometry()

    def attribute(self, name):
        return self._feat.get("properties", {}).get(name)

    def geometry(self):
        return self._geom

class QgsVectorLayerRenderer(QgsMapLayerRenderer):
    """
    Thread-safe, decoupled drawing class for Vector Layers matching QgsVectorLayerRenderer.
    Draws geometry shapes and outline symbols in background rendering threads.
    """
    def __init__(self, layer, settings, labeling=None):
        super().__init__(layer.id)
        self.crs = layer.crs
        self.extent = layer.extent  # Dynamic property evaluated on main thread!
        self.opacity = layer.opacity
        self.visible = layer.visible
        self.file_path = layer.provider.reader.file_path
        self.renderer = layer.renderer
        self._labeling = labeling  # QgsPalLayerSettings or None

    def labeling(self):
        """Return the labeling settings passed to this renderer (or None)."""
        return self._labeling

    def render(self, painter, settings, renderContext=None):
        if not self.visible or painter is None:
            return

        # If no render context was provided, create one from settings
        # (backward compatibility with callers that don't pass a context).
        if renderContext is None:
            from core.qgsrendercontext import QgsRenderContext
            renderContext = QgsRenderContext.fromMapSettings(settings)
            renderContext.setPainter(painter)

        from providers.ogr.qgsvectordataprovider import OGRDataProvider
        # Strict thread isolation: instantiate a fresh provider inside the render thread
        provider = OGRDataProvider(self.file_path)

        # 1. Determine extent to fetch
        view_extent = settings.extent if settings.extent else self.extent
        dest_crs = settings.destination_crs

        # 2. Setup OTF coordinate transformers
        if self.crs and dest_crs and self.crs != dest_crs:
            from core.qgscoordinatetransform import QgsCoordinateTransform
            transformer = QgsCoordinateTransform(self.crs, dest_crs)
        else:
            transformer = None

        # 3. Inverse transform viewport bounds to layer native projection for subset query
        if transformer:
            try:
                xmin, ymin, xmax, ymax = transformer.inverse_transform_bounds(
                    view_extent.left(),
                    view_extent.top() - view_extent.height(),
                    view_extent.right(),
                    view_extent.top()
                )
                ext_dict = {"left": xmin, "right": xmax, "top": ymax, "bottom": ymin}
            except Exception as e:
                print(f"Error inverse projecting viewport bounds: {e}")
                ext_dict = {
                    "left": view_extent.left(),
                    "right": view_extent.right(),
                    "top": view_extent.top(),
                    "bottom": view_extent.top() - view_extent.height()
                }
        else:
            ext_dict = {
                "left": view_extent.left(),
                "right": view_extent.right(),
                "top": view_extent.top(),
                "bottom": view_extent.top() - view_extent.height()
            }

        features = provider.get_features(ext_dict)

        # 4. Apply opacity
        old_opacity = painter.opacity()
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity * self.opacity)

        # 5. Render features -- pass renderContext to the feature renderer
        for feature in features:
            if transformer:
                try:
                    # Project geometry shape on-the-fly to match canvas coordinates
                    projected_shape = transformer.transform_geometry(feature.get("shape"))
                    # Create a temporary projected feature so as not to modify the cached feature
                    projected_feature = {
                        "id": feature.get("id"),
                        "properties": feature.get("properties"),
                        "shape": projected_shape
                    }
                    self.renderer.render_feature(projected_feature, painter, settings, renderContext)
                except Exception as e:
                    print(f"Error drawing projected vector feature in thread: {e}")
                    self.renderer.render_feature(feature, painter, settings, renderContext)
            else:
                self.renderer.render_feature(feature, painter, settings, renderContext)

        # 6. Render labels (after all features, so labels draw on top)
        if self._labeling is not None and self._labeling.enabled:
            from core.labeling.qgsvectorlayerlabelprovider import QgsVectorLayerLabelProvider
            label_provider = QgsVectorLayerLabelProvider(self._labeling)
            for feature in features:
                try:
                    adapter = _DictFeatureAdapter(feature)
                    label_provider.renderLabels(adapter, painter, renderContext)
                except Exception as e:
                    print(f"Error rendering label for feature: {e}")

        # 7. Restore opacity
        if self.opacity < 1.0:
            painter.setOpacity(old_opacity)


VectorLayerRenderer = QgsVectorLayerRenderer
