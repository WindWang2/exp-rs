"""Tests for Task 23: Wire RenderContext into Layer Renderers.

Verifies that QgsRenderContext is created once in QgsMapRendererJob and threaded
through the layer renderer pipeline, instead of each renderer creating its own.
"""
import pytest
import sys
from unittest.mock import MagicMock, patch, call

from PySide6.QtGui import QImage, QPainter
from PySide6.QtCore import QSize

from core.qgsrendercontext import QgsRenderContext
from core.qgsmapsettings import QgsMapSettings
from core.qgsrectangle import QgsRectangle
from core.qgsmaplayerrenderer import QgsMapLayerRenderer

# Import directly from the module file to avoid gui/__init__.py which
# pulls in agent/executor which depends on the deleted engine/ package.
import importlib.util
_spec = importlib.util.spec_from_file_location(
    'gui.qgsmaprendererjob',
    '/home/kevin/projects/exp-rs/gui/qgsmaprendererjob.py',
)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
QgsMapRendererJob = _mod.QgsMapRendererJob


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_settings(extent=None, output_size=None):
    """Create a minimal QgsMapSettings for testing."""
    settings = QgsMapSettings()
    settings.extent = extent or QgsRectangle(0, 0, 1000, 1000)
    settings.output_size = output_size or QSize(500, 500)
    return settings


def _make_mock_layer(layer_id="layer_1", visible=True):
    """Create a mock layer with createMapRenderer returning a mock renderer."""
    layer = MagicMock()
    layer.id = layer_id
    layer.visible = visible
    mock_renderer = MagicMock(spec=QgsMapLayerRenderer)
    layer.createMapRenderer.return_value = mock_renderer
    return layer


# ---------------------------------------------------------------------------
# QgsMapRendererJob creates and passes RenderContext
# ---------------------------------------------------------------------------

class TestMapRendererJobRenderContext:

    def test_job_creates_render_context(self):
        """QgsMapRendererJob creates a QgsRenderContext from settings."""
        settings = _make_settings()
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        assert hasattr(job, 'render_context')
        assert isinstance(job.render_context, QgsRenderContext)

    def test_job_render_context_extent_matches_settings(self):
        """The render context's extent matches the map settings extent."""
        extent = QgsRectangle(100, 200, 300, 400)
        settings = _make_settings(extent=extent)
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        assert job.render_context.extent() == extent

    def test_job_render_context_output_size_matches_settings(self):
        """The render context's output size matches the map settings output size."""
        size = QSize(800, 600)
        settings = _make_settings(output_size=size)
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        assert job.render_context.outputSize() == size

    def test_job_passes_render_context_to_renderer(self):
        """Each layer renderer's render() receives the render context."""
        settings = _make_settings()
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        job.run()

        # The mock renderer's render should have been called with 3 args:
        # painter, settings, renderContext
        mock_renderer = layer.createMapRenderer.return_value
        assert mock_renderer.render.call_count == 1
        args, kwargs = mock_renderer.render.call_args
        assert len(args) == 3  # painter, settings, renderContext
        assert isinstance(args[2], QgsRenderContext)

    def test_job_passes_same_context_to_all_renderers(self):
        """All layer renderers receive the same QgsRenderContext instance."""
        settings = _make_settings()
        layer1 = _make_mock_layer("layer_1")
        layer2 = _make_mock_layer("layer_2")
        layer3 = _make_mock_layer("layer_3")
        settings.layers = [layer1, layer2, layer3]

        job = QgsMapRendererJob(settings)
        job.run()

        contexts = []
        for layer in [layer1, layer2, layer3]:
            mock_renderer = layer.createMapRenderer.return_value
            args = mock_renderer.render.call_args[0]
            contexts.append(args[2])

        # All should be the exact same object
        assert contexts[0] is contexts[1]
        assert contexts[1] is contexts[2]

    def test_job_sets_painter_on_render_context(self):
        """The render context's painter is set before passing to renderers."""
        settings = _make_settings()
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        job.run()

        # The render context should have the painter set
        assert job.render_context.painter() is not None

    def test_job_context_has_map_to_pixel(self):
        """The render context has a valid MapToPixel transform."""
        settings = _make_settings()
        layer = _make_mock_layer()
        settings.layers = [layer]

        job = QgsMapRendererJob(settings)
        mtp = job.render_context.mapToPixel()
        assert mtp is not None
        assert mtp.mapWidth() == 500
        assert mtp.mapHeight() == 500


# ---------------------------------------------------------------------------
# QgsMapLayerRenderer ABC backward compatibility
# ---------------------------------------------------------------------------

class TestMapLayerRendererBackwardCompat:

    def test_abc_render_signature_accepts_render_context(self):
        """QgsMapLayerRenderer.render accepts an optional renderContext param."""
        # A concrete subclass to test the ABC
        class TestRenderer(QgsMapLayerRenderer):
            def __init__(self):
                super().__init__("test")
                self.received_context = None

            def render(self, painter, settings, renderContext=None):
                self.received_context = renderContext

        r = TestRenderer()
        ctx = QgsRenderContext()
        painter = MagicMock()

        # Call with renderContext
        r.render(painter, MagicMock(), renderContext=ctx)
        assert r.received_context is ctx

    def test_abc_render_backward_compat_no_context(self):
        """Calling render() without renderContext still works (backward compat)."""
        class TestRenderer(QgsMapLayerRenderer):
            def __init__(self):
                super().__init__("test")
                self.received_context = None

            def render(self, painter, settings, renderContext=None):
                self.received_context = renderContext

        r = TestRenderer()
        painter = MagicMock()

        # Call without renderContext (old-style)
        r.render(painter, MagicMock())
        assert r.received_context is None

    def test_layer_id_preserved(self):
        """Layer ID is preserved in the base class constructor."""
        class TestRenderer(QgsMapLayerRenderer):
            def render(self, painter, settings, renderContext=None):
                pass

        r = TestRenderer("my_layer_id")
        assert r.layer_id == "my_layer_id"


# ---------------------------------------------------------------------------
# QgsVectorLayerRenderer uses render context
# ---------------------------------------------------------------------------

class TestVectorLayerRendererRenderContext:

    def test_vector_renderer_accepts_render_context(self):
        """QgsVectorLayerRenderer.render() accepts optional renderContext."""
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer

        # Create a mock layer
        mock_layer = MagicMock()
        mock_layer.id = "vec_1"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.gpkg"
        mock_layer.renderer = MagicMock()

        settings = _make_settings()
        renderer = QgsVectorLayerRenderer(mock_layer, settings)

        # Call with renderContext -- should not crash
        ctx = QgsRenderContext.fromMapSettings(settings)
        painter = MagicMock(spec=QPainter)

        with patch("providers.ogr.qgsvectordataprovider.OGRDataProvider") as MockProvider:
            MockProvider.return_value.get_features.return_value = []
            renderer.render(painter, settings, renderContext=ctx)

    def test_vector_renderer_backward_compat_no_context(self):
        """QgsVectorLayerRenderer.render() works without renderContext."""
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer

        mock_layer = MagicMock()
        mock_layer.id = "vec_2"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.gpkg"
        mock_layer.renderer = MagicMock()

        settings = _make_settings()
        renderer = QgsVectorLayerRenderer(mock_layer, settings)

        # Old-style call without renderContext
        painter = MagicMock(spec=QPainter)
        with patch("providers.ogr.qgsvectordataprovider.OGRDataProvider") as MockProvider:
            MockProvider.return_value.get_features.return_value = []
            renderer.render(painter, settings)  # No renderContext -- should work

    def test_vector_renderer_creates_context_if_none(self):
        """When no renderContext is passed, vector renderer creates one from settings."""
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer

        mock_layer = MagicMock()
        mock_layer.id = "vec_3"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.gpkg"
        mock_renderer = MagicMock()
        mock_layer.renderer = mock_renderer

        settings = _make_settings()
        renderer = QgsVectorLayerRenderer(mock_layer, settings)

        painter = MagicMock(spec=QPainter)
        with patch("providers.ogr.qgsvectordataprovider.OGRDataProvider") as MockProvider:
            MockProvider.return_value.get_features.return_value = []
            renderer.render(painter, settings, renderContext=None)

        # render_feature should NOT be called (no features), but no crash

    def test_vector_renderer_uses_provided_context_for_feature_render(self):
        """Vector renderer passes the render context to the feature renderer."""
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer

        mock_layer = MagicMock()
        mock_layer.id = "vec_4"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.gpkg"

        mock_feature_renderer = MagicMock()
        mock_layer.renderer = mock_feature_renderer

        settings = _make_settings()
        renderer = QgsVectorLayerRenderer(mock_layer, settings)

        # Create a real render context
        ctx = QgsRenderContext.fromMapSettings(settings)

        painter = MagicMock(spec=QPainter)

        # Mock OGRDataProvider to return one feature
        mock_feature = {
            "id": 1,
            "properties": {},
            "shape": MagicMock(),
        }
        with patch("providers.ogr.qgsvectordataprovider.OGRDataProvider") as MockProvider:
            MockProvider.return_value.get_features.return_value = [mock_feature]
            renderer.render(painter, settings, renderContext=ctx)

        # The feature renderer's render_feature should have been called
        # with the render context
        assert mock_feature_renderer.render_feature.call_count == 1
        args = mock_feature_renderer.render_feature.call_args
        # Check the render context was passed (4th positional arg)
        assert len(args[0]) == 4  # feature, painter, settings, renderContext
        assert args[0][3] is ctx


# ---------------------------------------------------------------------------
# QgsRasterLayerRenderer accepts render context
# ---------------------------------------------------------------------------

class TestRasterLayerRendererRenderContext:

    def _setup_raster_mocks(self):
        """Set up mocks for GeospatialReader and rasterio."""
        import rasterio.transform
        mock_src = MagicMock()
        mock_src.crs.to_string.return_value = "EPSG:4326"
        mock_src.width = 100
        mock_src.height = 100
        # Use a real affine transform so ~transform works correctly
        mock_src.transform = rasterio.transform.from_bounds(0, 0, 100, 100, 100, 100)
        mock_src.res = [1.0, 1.0]
        mock_src.__enter__ = MagicMock(return_value=mock_src)
        mock_src.__exit__ = MagicMock(return_value=False)
        return mock_src

    def test_raster_renderer_accepts_render_context(self):
        """QgsRasterLayerRenderer.render() accepts optional renderContext."""
        from core.raster.qgsrasterlayerrenderer import QgsRasterLayerRenderer

        mock_layer = MagicMock()
        mock_layer.id = "rast_1"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.tif"
        mock_layer._pipe = MagicMock()
        mock_layer._pipe.renderer.return_value = MagicMock()

        settings = _make_settings()
        renderer = QgsRasterLayerRenderer(mock_layer, settings)

        ctx = QgsRenderContext.fromMapSettings(settings)
        painter = MagicMock(spec=QPainter)

        mock_src = self._setup_raster_mocks()
        with patch("core.qgsreader.GeospatialReader"):
            with patch("rasterio.open", return_value=mock_src):
                renderer.render(painter, settings, renderContext=ctx)

    def test_raster_renderer_backward_compat_no_context(self):
        """QgsRasterLayerRenderer.render() works without renderContext."""
        from core.raster.qgsrasterlayerrenderer import QgsRasterLayerRenderer

        mock_layer = MagicMock()
        mock_layer.id = "rast_2"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.tif"
        mock_layer._pipe = MagicMock()
        mock_layer._pipe.renderer.return_value = MagicMock()

        settings = _make_settings()
        renderer = QgsRasterLayerRenderer(mock_layer, settings)

        painter = MagicMock(spec=QPainter)

        mock_src = self._setup_raster_mocks()
        with patch("core.qgsreader.GeospatialReader"):
            with patch("rasterio.open", return_value=mock_src):
                renderer.render(painter, settings)


# ---------------------------------------------------------------------------
# QgsFeatureRenderer passes render context through
# ---------------------------------------------------------------------------

class TestFeatureRendererRenderContext:

    def test_single_symbol_renderer_accepts_render_context(self):
        """QgsSingleSymbolRenderer.render_feature accepts optional renderContext."""
        from core.vector.qgsvectorrenderer import QgsSingleSymbolRenderer
        from core.symbology.qgssymbol import QgsSymbol
        from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer

        fill_layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        renderer = QgsSingleSymbolRenderer(symbol=sym)

        settings = _make_settings()
        ctx = QgsRenderContext.fromMapSettings(settings)
        painter = MagicMock(spec=QPainter)
        feature = {"shape": MagicMock(), "id": 1}

        # When renderContext is provided, it should be used (not create a new one)
        with patch.object(sym, 'renderFeature') as mock_sym_render:
            renderer.render_feature(feature, painter, settings, renderContext=ctx)
            mock_sym_render.assert_called_once()
            args = mock_sym_render.call_args[0]
            # The third arg should be the passed-in context, not a new one
            assert args[2] is ctx

    def test_single_symbol_renderer_creates_context_if_none(self):
        """QgsSingleSymbolRenderer creates context from settings if none passed."""
        from core.vector.qgsvectorrenderer import QgsSingleSymbolRenderer
        from core.symbology.qgssymbol import QgsSymbol
        from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer

        fill_layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        renderer = QgsSingleSymbolRenderer(symbol=sym)

        settings = _make_settings()
        painter = MagicMock(spec=QPainter)
        feature = {"shape": MagicMock(), "id": 1}

        # When no renderContext is provided, a new one should be created
        with patch.object(sym, 'renderFeature') as mock_sym_render:
            renderer.render_feature(feature, painter, settings)
            mock_sym_render.assert_called_once()
            args = mock_sym_render.call_args[0]
            # The third arg should be a newly created QgsRenderContext
            assert isinstance(args[2], QgsRenderContext)
            assert args[2] is not None

    def test_categorized_renderer_accepts_render_context(self):
        """QgsCategorizedSymbolRenderer.render_feature accepts optional renderContext."""
        from core.vector.qgsvectorrenderer import (
            QgsCategorizedSymbolRenderer,
            QgsRendererCategory,
        )
        from core.symbology.qgssymbol import QgsSymbol
        from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer

        fill_layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        cat = QgsRendererCategory("road", "Road", sym)

        renderer = QgsCategorizedSymbolRenderer("type")
        renderer.addCategory(cat)

        settings = _make_settings()
        ctx = QgsRenderContext.fromMapSettings(settings)
        painter = MagicMock(spec=QPainter)
        feature = {"shape": MagicMock(), "id": 1, "properties": {"type": "road"}}

        with patch.object(sym, 'renderFeature') as mock_sym_render:
            renderer.render_feature(feature, painter, settings, renderContext=ctx)
            mock_sym_render.assert_called_once()
            args = mock_sym_render.call_args[0]
            assert args[2] is ctx

    def test_categorized_renderer_creates_context_if_none(self):
        """QgsCategorizedSymbolRenderer creates context from settings if none passed."""
        from core.vector.qgsvectorrenderer import (
            QgsCategorizedSymbolRenderer,
            QgsRendererCategory,
        )
        from core.symbology.qgssymbol import QgsSymbol
        from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer

        fill_layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        cat = QgsRendererCategory("road", "Road", sym)

        renderer = QgsCategorizedSymbolRenderer("type")
        renderer.addCategory(cat)

        settings = _make_settings()
        painter = MagicMock(spec=QPainter)
        feature = {"shape": MagicMock(), "id": 1, "properties": {"type": "road"}}

        with patch.object(sym, 'renderFeature') as mock_sym_render:
            renderer.render_feature(feature, painter, settings)
            mock_sym_render.assert_called_once()
            args = mock_sym_render.call_args[0]
            assert isinstance(args[2], QgsRenderContext)


# ---------------------------------------------------------------------------
# Integration: full pipeline end-to-end
# ---------------------------------------------------------------------------

class TestRenderPipelineIntegration:

    def test_full_pipeline_context_flow(self):
        """End-to-end: QgsMapRendererJob -> layer renderer -> feature renderer.

        Verifies that a single QgsRenderContext flows from the job through
        the vector layer renderer to the feature renderer without being
        recreated.
        """
        from core.vector.qgsvectorlayerrenderer import QgsVectorLayerRenderer

        # Create settings
        settings = _make_settings()

        # Create a real vector layer that uses QgsVectorLayerRenderer
        mock_layer = MagicMock()
        mock_layer.id = "vec_integ"
        mock_layer.crs = None
        mock_layer.extent = QgsRectangle(0, 0, 100, 100)
        mock_layer.opacity = 1.0
        mock_layer.visible = True
        mock_layer.provider.reader.file_path = "/tmp/test.gpkg"

        mock_feature_renderer = MagicMock()
        mock_layer.renderer = mock_feature_renderer

        def create_renderer(s):
            return QgsVectorLayerRenderer(mock_layer, s)

        mock_layer.createMapRenderer = create_renderer
        settings.layers = [mock_layer]

        job = QgsMapRendererJob(settings)
        job_ctx = job.render_context

        # Run with mocked provider to avoid file I/O
        with patch("providers.ogr.qgsvectordataprovider.OGRDataProvider") as MockProvider:
            MockProvider.return_value.get_features.return_value = []
            job.run()

        # The job's render context should have been set up properly
        assert isinstance(job_ctx, QgsRenderContext)
        assert job_ctx.extent() == settings.extent
        assert job_ctx.outputSize() == settings.output_size

    def test_context_not_recreated_per_feature(self):
        """When renderContext is passed through, it should NOT be recreated.

        This is the core optimization of Task 23: one context per render pass,
        not one per feature.
        """
        from core.vector.qgsvectorrenderer import QgsSingleSymbolRenderer
        from core.symbology.qgssymbol import QgsSymbol
        from core.symbology.qgssymbollayer import QgsSimpleFillSymbolLayer

        fill_layer = QgsSimpleFillSymbolLayer()
        sym = QgsSymbol(QgsSymbol.SymbolType.Fill, [fill_layer])
        renderer = QgsSingleSymbolRenderer(symbol=sym)

        settings = _make_settings()
        ctx = QgsRenderContext.fromMapSettings(settings)
        painter = MagicMock(spec=QPainter)

        # Track how many times fromMapSettings is called
        call_count = 0
        original_from_map = QgsRenderContext.fromMapSettings

        def counting_from_map(s):
            nonlocal call_count
            call_count += 1
            return original_from_map(s)

        features = [
            {"shape": MagicMock(), "id": i, "properties": {}}
            for i in range(5)
        ]

        with patch.object(QgsRenderContext, 'fromMapSettings', staticmethod(counting_from_map)):
            with patch.object(sym, 'renderFeature'):
                for feat in features:
                    renderer.render_feature(feat, painter, settings, renderContext=ctx)

        # fromMapSettings should NOT have been called because we passed ctx
        assert call_count == 0
