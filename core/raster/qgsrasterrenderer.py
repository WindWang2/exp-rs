from abc import ABC, abstractmethod
import numpy as np
from PySide6.QtGui import QImage
from PySide6.QtCore import QRectF, Qt

class QgsRasterRenderer(ABC):
    """
    Base Strategy class for QGIS-identical raster drawing styles.
    Defines how pixel values are scaled and mapped, matching QgsRasterRenderer in QGIS.
    """
    def __init__(self, layer):
        self.layer_id = layer.id
        self.opacity = layer.opacity
        self.contrast_enhancement = layer.contrast_enhancement
        self.min_max_limits_method = layer.min_max_limits_method
        self.cumulative_cut_lower = layer.cumulative_cut_lower
        self.cumulative_cut_upper = layer.cumulative_cut_upper
        self.std_dev_factor = layer.std_dev_factor
        self.user_min = layer.user_min
        self.user_max = layer.user_max

    @abstractmethod
    def render(self, reader, scale_factor, window=None, out_size=None, coeffs_x=None, coeffs_y=None) -> QImage:
        """Processes raw band data and returns a styled QImage."""
        pass

    def calculate_stretch_bounds(self, arr):
        """Calculates stretch min/max limits using the active contrast adjustment method."""
        valid_arr = arr[np.isfinite(arr)]
        if valid_arr.size == 0:
            return 0.0, 255.0
            
        if self.min_max_limits_method == "min_max":
            return float(valid_arr.min()), float(valid_arr.max())
        elif self.min_max_limits_method == "cumulative_cut":
            low = np.percentile(valid_arr, self.cumulative_cut_lower)
            high = np.percentile(valid_arr, self.cumulative_cut_upper)
            return float(low), float(high)
        elif self.min_max_limits_method == "std_dev":
            mean = np.mean(valid_arr)
            std = np.std(valid_arr)
            low = mean - self.std_dev_factor * std
            high = mean + self.std_dev_factor * std
            return float(low), float(high)
        elif self.min_max_limits_method == "user_defined":
            low = self.user_min if self.user_min is not None else float(valid_arr.min())
            high = self.user_max if self.user_max is not None else float(valid_arr.max())
            return float(low), float(high)
        return float(valid_arr.min()), float(valid_arr.max())

    def stretch(self, arr):
        """Applies contrast stretch to fit pixel values in the [0, 255] byte range."""
        if self.contrast_enhancement == "none":
            return np.clip(arr, 0, 255).astype(np.uint8)
            
        amin, amax = self.calculate_stretch_bounds(arr)
        if amax - amin > 0:
            stretched = ((arr.astype(float) - amin) / (amax - amin) * 255.0)
            return np.clip(stretched, 0, 255).astype(np.uint8)
        return np.zeros_like(arr, dtype=np.uint8)


def _get_raster_ops():
    """Try to import C++ raster_ops, fall back to Python implementation."""
    try:
        import sys, os
        build_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../build"))
        if build_path not in sys.path:
            sys.path.insert(0, build_path)
        import raster_ops
        # Check if required functions exist
        if hasattr(raster_ops, 'warp_and_compose_rgb') and hasattr(raster_ops, 'stretch_and_compose_rgb'):
            return raster_ops
    except ImportError:
        pass
    # Fall back to Python implementation
    from core.raster.raster_ops_fallback import (
        stretch_and_compose_rgb, warp_and_compose_rgb,
        stretch_gray, warp_and_stretch_gray, warp_raster_band
    )
    import types
    mod = types.ModuleType('raster_ops_fallback')
    mod.stretch_and_compose_rgb = stretch_and_compose_rgb
    mod.warp_and_compose_rgb = warp_and_compose_rgb
    mod.stretch_gray = stretch_gray
    mod.warp_and_stretch_gray = warp_and_stretch_gray
    mod.warp_raster_band = warp_raster_band
    return mod


class QgsMultiBandColorRenderer(QgsRasterRenderer):
    """
    Renders a multi-spectral composite combining three bands into RGB channels,
    matching QgsMultiBandColorRenderer in QGIS.
    """
    def __init__(self, layer):
        super().__init__(layer)
        self.red_band = layer.red_band
        self.green_band = layer.green_band
        self.blue_band = layer.blue_band

    def render(self, reader, scale_factor, window=None, out_size=None, coeffs_x=None, coeffs_y=None) -> QImage:
        r_band = reader.read_raster_band(self.red_band, scale_factor, window=window)
        g_band = reader.read_raster_band(self.green_band, scale_factor, window=window)
        b_band = reader.read_raster_band(self.blue_band, scale_factor, window=window)

        raster_ops = _get_raster_ops()

        r_min, r_max = self.calculate_stretch_bounds(r_band)
        g_min, g_max = self.calculate_stretch_bounds(g_band)
        b_min, b_max = self.calculate_stretch_bounds(b_band)

        if out_size is not None and coeffs_x is not None and coeffs_y is not None:
            out_w, out_h = out_size
            rgb_data = raster_ops.warp_and_compose_rgb(
                r_band.astype(np.float32),
                g_band.astype(np.float32),
                b_band.astype(np.float32),
                out_w, out_h,
                coeffs_x, coeffs_y,
                r_min, r_max,
                g_min, g_max,
                b_min, b_max
            )
        else:
            rgb_data = raster_ops.stretch_and_compose_rgb(
                r_band.astype(np.float32),
                g_band.astype(np.float32),
                b_band.astype(np.float32),
                r_min, r_max,
                g_min, g_max,
                b_min, b_max
            )

        h, w, _ = rgb_data.shape
        img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
        img.ndarray = rgb_data
        return img.copy()


class QgsSingleBandGrayRenderer(QgsRasterRenderer):
    """
    Renders a single band as a grayscale image (black to white),
    matching QgsSingleBandGrayRenderer in QGIS.
    """
    def __init__(self, layer):
        super().__init__(layer)
        self.gray_band = layer.gray_band

    def render(self, reader, scale_factor, window=None, out_size=None, coeffs_x=None, coeffs_y=None) -> QImage:
        band = reader.read_raster_band(self.gray_band, scale_factor, window=window)

        raster_ops = _get_raster_ops()

        gray_min, gray_max = self.calculate_stretch_bounds(band)

        if out_size is not None and coeffs_x is not None and coeffs_y is not None:
            out_w, out_h = out_size
            gray_data = raster_ops.warp_and_stretch_gray(
                band.astype(np.float32),
                out_w, out_h,
                coeffs_x, coeffs_y,
                gray_min, gray_max
            )
        else:
            gray_data = raster_ops.stretch_gray(
                band.astype(np.float32),
                gray_min, gray_max
            )

        h, w, _ = gray_data.shape
        img = QImage(gray_data.data, w, h, 3 * w, QImage.Format_RGB888)
        img.ndarray = gray_data
        return img.copy()


class QgsSingleBandPseudoColorRenderer(QgsRasterRenderer):
    """
    Renders a single band mapped dynamically to a continuous color ramp,
    matching QgsSingleBandPseudoColorRenderer in QGIS.
    """
    def __init__(self, layer):
        super().__init__(layer)
        self.pseudocolor_band = layer.pseudocolor_band
        self.color_ramp = layer.color_ramp
        
    def render(self, reader, scale_factor, window=None, out_size=None, coeffs_x=None, coeffs_y=None) -> QImage:
        band = reader.read_raster_band(self.pseudocolor_band, scale_factor, window=window)

        if out_size is not None and coeffs_x is not None and coeffs_y is not None:
            raster_ops = _get_raster_ops()
            out_w, out_h = out_size
            band = raster_ops.warp_raster_band(band.astype(np.float32), out_w, out_h, coeffs_x, coeffs_y)
            
        if self.contrast_enhancement == "none":
            amin, amax = 0.0, 255.0
        else:
            amin, amax = self.calculate_stretch_bounds(band)
        
        if amax - amin > 0:
            norm = np.clip((band.astype(float) - amin) / (amax - amin), 0.0, 1.0)
        else:
            norm = np.zeros_like(band, dtype=float)
        
        import matplotlib
        try:
            cmap = matplotlib.colormaps[self.color_ramp]
        except KeyError:
            cmap = matplotlib.colormaps["viridis"]
            
        rgba = cmap(norm)
        rgb = (rgba[:, :, :3] * 255.0).astype(np.uint8)
        
        h, w = band.shape
        rgb_data = np.ascontiguousarray(rgb)
        img = QImage(rgb_data.data, w, h, 3 * w, QImage.Format_RGB888)
        img.ndarray = rgb_data
        return img.copy()


RasterRenderer = QgsRasterRenderer
MultiBandColorRenderer = QgsMultiBandColorRenderer
SingleBandGrayRenderer = QgsSingleBandGrayRenderer
SingleBandPseudoColorRenderer = QgsSingleBandPseudoColorRenderer
