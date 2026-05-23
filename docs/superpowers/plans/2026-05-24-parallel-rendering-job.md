# Parallel Map Rendering Job Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a parallel rendering job that renders map layers in background threads and merges them.

**Architecture:** A coordinator-worker pattern using `MapRendererJob` (coordinator) and `LayerRenderJob` (worker). Workers render to individual `QImage` buffers, which are then composed by the coordinator in the correct Z-order.

**Tech Stack:** Python, PySide6 (Qt Core, Gui, Widgets), threading.

---

### Task 1: Setup Project Structure and Basic Signals

**Files:**
- Create: `engine/core/display/pipeline/__init__.py`
- Create: `engine/core/display/pipeline/renderer_job.py`

- [ ] **Step 1: Create directory and init file**
```bash
mkdir -p engine/core/display/pipeline
touch engine/core/display/pipeline/__init__.py
```

- [ ] **Step 2: Define basic Signal classes in renderer_job.py**
```python
from PySide6.QtCore import QObject, Signal, QRunnable, Qt
from PySide6.QtGui import QImage, QPainter
import threading

class RenderSignals(QObject):
    """Signals for the rendering process."""
    result_ready = Signal(str, QImage) # layer_id, image
    finished = Signal(QImage)
    error = Signal(str, str) # layer_id, error_message
```

- [ ] **Step 3: Commit**
```bash
git add engine/core/display/pipeline/
git commit -m "chore: setup rendering pipeline directory and basic signals"
```

---

### Task 2: Implement LayerRenderJob (Worker)

**Files:**
- Modify: `engine/core/display/pipeline/renderer_job.py`
- Test: `tests/test_renderer_job.py`

- [ ] **Step 1: Write failing test for LayerRenderJob**
```python
# tests/test_renderer_job.py
import pytest
from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QImage, QColor
from engine.core.display.pipeline.renderer_job import LayerRenderJob, RenderSignals
from engine.core.display.base.map_layer import MapLayer
from engine.core.display.base.map_settings import MapSettings
import threading

class MockLayer(MapLayer):
    def draw(self, painter, settings):
        painter.fillRect(0, 0, 10, 10, QColor("red"))

def test_layer_render_job_success(qtbot):
    settings = MapSettings()
    settings.output_size = QSize(100, 100)
    layer = MockLayer("layer1", "Layer 1")
    signals = RenderSignals()
    cancel_flag = threading.Event()
    
    job = LayerRenderJob(layer, settings, signals, cancel_flag)
    
    with qtbot.waitSignal(signals.result_ready, timeout=1000) as blocker:
        job.run()
    
    layer_id, image = blocker.args
    assert layer_id == "layer1"
    assert not image.isNull()
    assert image.pixelColor(0, 0) == QColor("red")
```

- [ ] **Step 2: Implement LayerRenderJob**
```python
# engine/core/display/pipeline/renderer_job.py (append)
class LayerRenderJob(QRunnable):
    def __init__(self, layer, settings, signals, cancel_flag):
        super().__init__()
        self.layer = layer
        self.settings = settings
        self.signals = signals
        self.cancel_flag = cancel_flag

    def run(self):
        if self.cancel_flag.is_set():
            return
        
        try:
            image = QImage(self.settings.output_size, QImage.Format_ARGB32)
            image.fill(Qt.transparent)
            
            painter = QPainter(image)
            if not painter.isActive():
                 self.signals.error.emit(self.layer.id, "Failed to start QPainter")
                 return
                 
            self.layer.draw(painter, self.settings)
            painter.end()
            
            if not self.cancel_flag.is_set():
                self.signals.result_ready.emit(self.layer.id, image)
        except Exception as e:
            self.signals.error.emit(self.layer.id, str(e))
```

- [ ] **Step 3: Run test to verify it passes**
```bash
pytest tests/test_renderer_job.py -v
```

- [ ] **Step 4: Commit**
```bash
git add engine/core/display/pipeline/renderer_job.py tests/test_renderer_job.py
git commit -m "feat: implement LayerRenderJob worker"
```

---

### Task 3: Implement MapRendererJob (Coordinator)

**Files:**
- Modify: `engine/core/display/pipeline/renderer_job.py`
- Test: `tests/test_renderer_job.py`

- [ ] **Step 1: Write failing test for MapRendererJob**
```python
# tests/test_renderer_job.py (append)
from engine.core.display.pipeline.renderer_job import MapRendererJob

class BlueLayer(MapLayer):
    def draw(self, painter, settings):
        painter.fillRect(0, 0, 100, 100, QColor("blue"))

def test_map_renderer_job_composition(qtbot):
    settings = MapSettings()
    settings.output_size = QSize(100, 100)
    layer1 = MockLayer("layer1", "Red Layer") # Red
    layer2 = BlueLayer("layer2", "Blue Layer") # Blue
    settings.layers = [layer1, layer2] # Blue on top of Red
    
    job = MapRendererJob(settings)
    
    with qtbot.waitSignal(job.finished, timeout=2000) as blocker:
        job.start()
        
    final_image = blocker.args[0]
    assert not final_image.isNull()
    # Top layer is blue, so (0,0) should be blue
    assert final_image.pixelColor(0, 0) == QColor("blue")
```

- [ ] **Step 2: Implement MapRendererJob**
```python
# engine/core/display/pipeline/renderer_job.py (append)
from PySide6.QtCore import QThreadPool

class MapRendererJob(QObject):
    finished = Signal(QImage)
    
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.signals = RenderSignals()
        self.cancel_flag = threading.Event()
        self.results = {}
        self.active_jobs = 0
        self._lock = threading.Lock()
        
        self.signals.result_ready.connect(self._on_layer_finished)
        self.signals.error.connect(self._on_layer_error)
        
    def start(self):
        visible_layers = [l for l in self.settings.layers if l.visible]
        if not visible_layers:
            empty_image = QImage(self.settings.output_size, QImage.Format_ARGB32)
            empty_image.fill(Qt.transparent)
            self.finished.emit(empty_image)
            return
            
        self.active_jobs = len(visible_layers)
        pool = QThreadPool.globalInstance()
        
        for layer in visible_layers:
            worker = LayerRenderJob(layer, self.settings, self.signals, self.cancel_flag)
            pool.start(worker)
            
    def _on_layer_finished(self, layer_id, image):
        with self._lock:
            self.results[layer_id] = image
            self.active_jobs -= 1
            if self.active_jobs == 0:
                self._compose()
                
    def _on_layer_error(self, layer_id, error_msg):
        print(f"Error rendering layer {layer_id}: {error_msg}")
        with self._lock:
            self.active_jobs -= 1
            if self.active_jobs == 0:
                self._compose()
                
    def _compose(self):
        if self.cancel_flag.is_set():
            return
            
        final_image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        final_image.fill(Qt.transparent)
        painter = QPainter(final_image)
        
        for layer in self.settings.layers:
            if layer.id in self.results:
                painter.drawImage(0, 0, self.results[layer.id])
        
        painter.end()
        self.finished.emit(final_image)

    def cancel(self):
        self.cancel_flag.set()
```

- [ ] **Step 3: Run tests to verify it passes**
```bash
pytest tests/test_renderer_job.py -v
```

- [ ] **Step 4: Commit**
```bash
git add engine/core/display/pipeline/renderer_job.py tests/test_renderer_job.py
git commit -m "feat: implement MapRendererJob coordinator"
```

---

### Task 4: Verify Cancellation Mechanism

**Files:**
- Test: `tests/test_renderer_job.py`

- [ ] **Step 1: Write failing test for cancellation**
```python
# tests/test_renderer_job.py (append)
import time

class SlowLayer(MapLayer):
    def draw(self, painter, settings):
        time.sleep(0.5) # Simulate heavy work
        painter.fillRect(0, 0, 10, 10, QColor("green"))

def test_map_renderer_job_cancellation(qtbot):
    settings = MapSettings()
    settings.output_size = QSize(100, 100)
    layer = SlowLayer("slow", "Slow Layer")
    settings.layers = [layer]
    
    job = MapRendererJob(settings)
    
    # We want to ensure NO signal is emitted if canceled early
    with qtbot.assertNotEmitted(job.finished):
        job.start()
        job.cancel()
        # Wait a bit to ensure it had time to finish if it wasn't canceled
        qtbot.wait(600) 
```

- [ ] **Step 2: Run tests to verify it passes**
```bash
pytest tests/test_renderer_job.py -v
```

- [ ] **Step 3: Commit**
```bash
git add tests/test_renderer_job.py
git commit -m "test: verify cancellation mechanism"
```

---

### Task 5: Final Verification and Documentation

- [ ] **Step 1: Run all display-related tests**
```bash
pytest tests/test_map_settings.py tests/test_map_layer.py tests/test_renderer_job.py -v
```

- [ ] **Step 2: Add docstrings and clean up code**
Ensure `renderer_job.py` is well-documented.

- [ ] **Step 3: Final Commit**
```bash
git commit -am "docs: finalize parallel rendering job implementation"
```
