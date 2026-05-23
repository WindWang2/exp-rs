# Initialize Core Base Classes (QGIS Port) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Initialize core base classes (`MapLayer`, `DataProvider`, `MapSettings`) to align with QGIS architecture.

**Architecture:** Create abstract base classes and data structures in a new `engine/core/display/base` directory to provide the foundation for the display system.

**Tech Stack:** Python (ABC for abstraction)

---

### Task 1: Setup Directory Structure

**Files:**
- Create: `engine/core/display/__init__.py`
- Create: `engine/core/display/base/__init__.py`

- [ ] **Step 1: Create directories and init files**
Run: `mkdir -p engine/core/display/base && touch engine/core/display/__init__.py engine/core/display/base/__init__.py`

- [ ] **Step 2: Commit structure**
```bash
git add engine/core/display/
git commit -m "chore: setup display base directory structure"
```

### Task 2: Implement MapLayer

**Files:**
- Create: `engine/core/display/base/map_layer.py`
- Create: `tests/test_map_layer.py`

- [ ] **Step 1: Write failing test for MapLayer**
```python
import pytest
from engine.core.display.base.map_layer import MapLayer

def test_map_layer_instantiation():
    class ConcreteLayer(MapLayer):
        def draw(self, painter, settings):
            return "drawing"

    layer = ConcreteLayer("test-id", "test-name")
    assert layer.id == "test-id"
    assert layer.name == "test-name"
    assert layer.visible is True
    assert layer.opacity == 1.0
    assert layer.draw(None, None) == "drawing"

def test_map_layer_abstract():
    with pytest.raises(TypeError):
        MapLayer("id", "name")
```

- [ ] **Step 2: Run test to verify it fails**
Run: `pytest tests/test_map_layer.py`
Expected: FAIL (ModuleNotFoundError)

- [ ] **Step 3: Write minimal implementation for MapLayer**
```python
from abc import ABC, abstractmethod

class MapLayer(ABC):
    def __init__(self, layer_id: str, name: str):
        self.id = layer_id
        self.name = name
        self.visible = True
        self.opacity = 1.0
        self.crs = None
        self.extent = None

    @abstractmethod
    def draw(self, painter, settings):
        pass
```

- [ ] **Step 4: Run test to verify it passes**
Run: `pytest tests/test_map_layer.py`

- [ ] **Step 5: Commit**
```bash
git add engine/core/display/base/map_layer.py tests/test_map_layer.py
git commit -m "feat: add MapLayer base class"
```

### Task 3: Implement DataProvider

**Files:**
- Create: `engine/core/display/base/data_provider.py`
- Create: `tests/test_data_provider.py`

- [ ] **Step 1: Write failing test for DataProvider**
```python
import pytest
from engine.core.display.base.data_provider import DataProvider

def test_data_provider_abstract():
    with pytest.raises(TypeError):
        DataProvider()

def test_data_provider_concrete():
    class ConcreteProvider(DataProvider):
        def extent(self):
            return [0, 0, 10, 10]
    
    provider = ConcreteProvider()
    assert provider.extent() == [0, 0, 10, 10]
```

- [ ] **Step 2: Run test to verify it fails**
Run: `pytest tests/test_data_provider.py`

- [ ] **Step 3: Write minimal implementation for DataProvider**
```python
from abc import ABC, abstractmethod

class DataProvider(ABC):
    @abstractmethod
    def extent(self):
        pass
```

- [ ] **Step 4: Run test to verify it passes**
Run: `pytest tests/test_data_provider.py`

- [ ] **Step 5: Commit**
```bash
git add engine/core/display/base/data_provider.py tests/test_data_provider.py
git commit -m "feat: add DataProvider base class"
```

### Task 4: Implement MapSettings

**Files:**
- Create: `engine/core/display/base/map_settings.py`
- Create: `tests/test_map_settings.py`

- [ ] **Step 1: Write failing test for MapSettings**
```python
from engine.core.display.base.map_settings import MapSettings

def test_map_settings_initialization():
    settings = MapSettings()
    assert settings.layers == []
    assert settings.extent is None
    assert settings.output_size is None
    assert settings.destination_crs == "EPSG:3857"
```

- [ ] **Step 2: Run test to verify it fails**
Run: `pytest tests/test_map_settings.py`

- [ ] **Step 3: Write minimal implementation for MapSettings**
```python
class MapSettings:
    def __init__(self):
        self.layers = []
        self.extent = None
        self.output_size = None # QSize
        self.destination_crs = "EPSG:3857"
```

- [ ] **Step 4: Run test to verify it passes**
Run: `pytest tests/test_map_settings.py`

- [ ] **Step 5: Commit**
```bash
git add engine/core/display/base/map_settings.py tests/test_map_settings.py
git commit -m "feat: add MapSettings class"
```

### Task 5: Final Cleanup and Verification

- [ ] **Step 1: Run all tests**
Run: `pytest tests/test_map_layer.py tests/test_data_provider.py tests/test_map_settings.py`

- [ ] **Step 2: Final Commit**
```bash
git add engine/core/display/base/
git commit -m "feat: add base display classes (QgsMapLayer, QgsDataProvider, QgsMapSettings)"
```
