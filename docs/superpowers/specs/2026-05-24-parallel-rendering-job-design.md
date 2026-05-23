# Design Spec: Parallel Map Rendering Job

## 1. Goal
Implement an asynchronous, multi-threaded rendering pipeline for the QGIS-inspired map engine. This ensures that the UI remains responsive during heavy rendering tasks (like pan/zoom) and leverages multi-core CPUs by rendering map layers in parallel.

## 2. Architecture

### 2.1 Component Overview
- **`MapRendererJob` (Coordinator):**
    - High-level coordinator (inherits `QObject`).
    - Manages the lifecycle of a single rendering request.
    - Spawns multiple `LayerRenderJob` workers.
    - Performs final composition of layer images.
- **`LayerRenderJob` (Worker):**
    - Lightweight worker (inherits `QRunnable`).
    - Renders exactly one `MapLayer` to a dedicated `QImage`.
    - Reports success/failure back to the coordinator.

### 2.2 Data Flow
1. `MapRendererJob` is initialized with `MapSettings`.
2. It identifies visible layers and their Z-order.
3. For each layer, a `LayerRenderJob` is created and queued in the `QThreadPool`.
4. As each `LayerRenderJob` completes, it signals the `MapRendererJob` with its resulting `QImage`.
5. Once all jobs are finished (or timed out/canceled), `MapRendererJob` merges the images into a final canvas.
6. The final `QImage` is emitted via the `finished(QImage)` signal.

## 3. Implementation Details

### 3.1 LayerRenderJob
```python
class LayerRenderJob(QRunnable):
    def __init__(self, layer, settings, signals, cancel_flag):
        self.layer = layer
        self.settings = settings
        self.signals = signals # Custom QObject for signals
        self.cancel_flag = cancel_flag

    def run(self):
        if self.cancel_flag.is_set():
            return
        
        image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        image.fill(Qt.transparent)
        
        painter = QPainter(image)
        # Pass cancel_flag to layer.draw for mid-draw abortion
        self.layer.draw(painter, self.settings) 
        painter.end()
        
        if not self.cancel_flag.is_set():
            self.signals.result_ready.emit(self.layer.id, image)
```

### 3.2 MapRendererJob
- **State Management:** Uses a counter and a dictionary to track results: `self.results = {layer_id: QImage}`.
- **Composition Logic:**
    ```python
    def compose(self):
        final_image = QImage(self.settings.output_size, QImage.Format_ARGB32)
        final_image.fill(Qt.transparent)
        painter = QPainter(final_image)
        for layer in self.settings.layers:
            if layer.id in self.results:
                painter.drawImage(0, 0, self.results[layer.id])
        painter.end()
        self.finished.emit(final_image)
    ```

### 3.3 Cancellation
- A `threading.Event` or a thread-safe flag is used as the `cancel_flag`.
- `MapRendererJob.cancel()` sets this flag.
- Both the coordinator (before merging) and workers (before/during drawing) check this flag.

## 4. Error Handling
- If a layer fails to render, the coordinator should either skip it or draw an error placeholder.
- If all layers fail or the job is canceled, no `finished` signal is emitted (or a `canceled` signal is emitted).

## 5. Testing Strategy
- **Unit Test:** `tests/test_renderer_job.py`
    - Verify that multiple mock layers are rendered and merged correctly.
    - Verify that cancellation prevents the `finished` signal and stops workers.
    - Verify that Z-order is respected in the final image.
