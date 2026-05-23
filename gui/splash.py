from PySide6.QtWidgets import QSplashScreen, QWidget, QVBoxLayout, QLabel, QProgressBar
from PySide6.QtCore import Qt, QThread, Signal, QTimer, QRectF
from PySide6.QtGui import QFont, QColor, QPalette
import os
import time
import numpy as np
import rasterio
from rasterio.transform import from_origin

def generate_synthetic_crops(output_path: str):
    """
    Generates a 3-band multi-spectral GeoTIFF mimicking realistic agricultural fields.
    Band 1: Red (visual), Band 2: Green (visual), Band 3: Near-Infrared (NIR).
    Used to showcase NDVI/NDWI and classifications instantly.
    """
    if os.path.exists(output_path):
        return # Skip if already generated
        
    h, w = 512, 512
    # Spatial transform: coordinates centered around Web Mercator origin
    transform = from_origin(-10000, 10000, 20000/w, 20000/h)
    
    # Coordinate grids
    y, x = np.ogrid[:h, :w]
    
    # Base background: soil/dry grass (high Red, medium Green, moderate NIR)
    red = np.full((h, w), 80, dtype=np.uint8)
    green = np.full((h, w), 60, dtype=np.uint8)
    nir = np.full((h, w), 100, dtype=np.uint8)
    
    # 1. Meandering River: low Red, medium Green, extremely low NIR (water signature)
    river_y = 220 + 70 * np.sin(x / 55)
    river_mask = np.abs(y - river_y) < 25
    red[river_mask] = 20
    green[river_mask] = 40
    nir[river_mask] = 10
    
    # 2. Crop Field Circle 1: low Red, medium Green, very high NIR (healthy crop)
    field1_mask = (x - 140)**2 + (y - 140)**2 < 85**2
    red[field1_mask] = 30
    green[field1_mask] = 95
    nir[field1_mask] = 230
    
    # 3. Crop Field Circle 2: moderate Red, high Green, moderate-high NIR (sparse crop)
    field2_mask = (x - 370)**2 + (y - 360)**2 < 100**2
    red[field2_mask] = 45
    green[field2_mask] = 110
    nir[field2_mask] = 160
    
    # 4. Straight Concrete Road: uniform high reflectance in all bands
    road_mask = np.abs(x - y) < 8
    # Keep river and crop fields on top of road for drawing layers realism
    road_mask = road_mask & (~river_mask) & (~field1_mask) & (~field2_mask)
    red[road_mask] = 175
    green[road_mask] = 175
    nir[road_mask] = 175
    
    profile = {
        'driver': 'GTiff',
        'dtype': 'uint8',
        'nodata': None,
        'width': w,
        'height': h,
        'count': 3,
        'crs': 'EPSG:3857',
        'transform': transform,
        'tiled': False,
        'interleave': 'band'
    }
    
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with rasterio.open(output_path, 'w', **profile) as dst:
        dst.write(red, 1)
        dst.write(green, 2)
        dst.write(nir, 3)

class BootWorker(QThread):
    """Background boot orchestrator."""
    progress_updated = Signal(int, str)
    finished = Signal(str)
    
    def run(self):
        # Step 1: Spatial coordinates database search
        self.progress_updated.emit(20, "Searching coordinate reference database...")
        time.sleep(0.4)
        
        try:
            conda_proj = "/opt/miniconda3/share/proj"
            if os.path.exists(conda_proj):
                os.environ["PROJ_LIB"] = conda_proj
            else:
                import pyproj
                datadir = pyproj.datadir.get_data_dir()
                if datadir:
                    os.environ["PROJ_LIB"] = datadir
        except Exception:
            pass
            
        self.progress_updated.emit(40, "Setting up standalone coordinate reference variables...")
        time.sleep(0.4)
        
        # Step 2: Sample crop field dataset generation
        self.progress_updated.emit(60, "Synthesizing agricultural crop-field overviews...")
        sample_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "data", "sample_crops.tif"))
        try:
            generate_synthetic_crops(sample_path)
        except Exception as e:
            print(f"Error generating sample dataset: {e}")
            
        time.sleep(0.4)
        self.progress_updated.emit(80, "Initializing central GIS processing toolboxes...")
        time.sleep(0.3)
        
        self.progress_updated.emit(100, "Starting Antigravity RS Map Engine...")
        time.sleep(0.2)
        self.finished.emit(sample_path)

class OnboardingSplashScreen(QSplashScreen):
    """
    High-aesthetic startup splash panel.
    Coordinates background bootstrapping tasks and generates student sample GeoTIFFs.
    """
    boot_complete = Signal(str) # Emits absolute path to sample dataset
    
    def __init__(self):
        super().__init__()
        self.setWindowFlags(Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.resize(520, 360)
        
        # Build premium Slate interface layout
        layout = QVBoxLayout()
        layout.setContentsMargins(30, 40, 30, 40)
        
        # Glowing Window Title
        self.title_label = QLabel("ANTIGRAVITY RS")
        self.title_label.setObjectName("splashTitle")
        self.title_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.title_label)
        
        # Subtitle description
        self.sub_label = QLabel("Remote Sensing Analysis & Agent Platform")
        self.sub_label.setObjectName("splashSubtitle")
        self.sub_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.sub_label)
        
        # Status loading readout
        self.status_label = QLabel("Initializing GIS engine...")
        self.status_label.setObjectName("splashStatus")
        layout.addWidget(self.status_label)
        
        # Modern rounded progress bar
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        layout.addWidget(self.progress)
        
        widget = QWidget(self)
        widget.setObjectName("splashContainer")
        widget.setLayout(layout)
        widget.resize(520, 360)
        
        # Launch background task thread
        self.worker = BootWorker()
        self.worker.progress_updated.connect(self._update_progress)
        self.worker.finished.connect(self._handle_finished)
        self.worker.start()

    def _update_progress(self, val: int, message: str):
        self.progress.setValue(val)
        self.status_label.setText(message)

    def _handle_finished(self, sample_path: str):
        self.boot_complete.emit(sample_path)
        self.close()
