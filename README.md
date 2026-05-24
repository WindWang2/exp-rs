# Antigravity RS: Streamlined Remote Sensing Analysis & Agent Platform

> [!NOTE]
> Antigravity RS is a standalone, lightweight, and high-performance remote sensing analysis platform designed for undergraduate remote sensing education and automated AI agent workflows.

---

## 🌟 Visual & Technical Highlights

*   **QGIS GUI Architecture Emulation**: Complete custom PySide6 Model-View classes implementing QGIS's hierarchical **`QgsLayerTreeView`** and **`QgsLayerTreeModel`** with checkbox state handling, layer ordering, and context menus.
*   **High-Performance C++ Raster Engine**: Bilinear resampling, coordinate warping, PCA computation, and contrast-stretch RGB composition written in native C++ using **Eigen** and exposed via **Pybind11**.
*   **In-App AI Agent Conversational Dock**: A natural-language assistant panel that translates user prompts to structured JSON API actions, executes calculations, writes educational Python scripts, and renders results dynamically.
*   **Zero Local GIS System Overhead**: Using a modular **Hybrid Dependency Strategy**, the platform compiles monolithic QGIS and Orfeo Toolbox (OTB) algorithm concepts directly inside the package, requiring **no QGIS or OTB desktop installations** on the student's computer.

---

## 🛠️ Tech Stack & Dependencies

| Component | Technology | Detail |
| :--- | :--- | :--- |
| **Core GUI** | PySide6 (Qt for Python) | Desktop UI window layout, map canvas, docks, spectral charts |
| **GIS Engines** | Rasterio, Fiona, PyProj | High-performance coordinate references, format readers, transforms |
| **Native Accelerators** | C++17, Pybind11, Eigen | Bilinear warping, multiband stretch composition, matrix PCA |
| **Build Pipeline** | CMake & GCC / MSVC | Automated compilation of C++ shared libraries (`raster_ops`) |
| **Testing Frame** | PyTest | Verified unit testing suite (385 test cases, 100% pass) |

---

## 📁 Repository Structure

```
exp-rs/
├── core/               # GIS engine core models & QGIS emulations
│   ├── layertree/      # Hierarchical layer node hierarchy
│   ├── raster/         # Raster dataset providers & rendering engines
│   └── vector/         # Vector dataset providers & shapefile renderers
├── gui/                # Custom premium PyQt/PySide6 desktop widgets
│   ├── layertree/      # Model-view implementations for layer lists
│   ├── canvas.py       # High-performance map viewport canvas
│   └── agent_dock.py   # In-app AI Agent side panel and chat window
├── src/                # Native C++ Pybind11 source code (raster_ops.cpp)
├── tests/              # Automated unit test suite
├── CLAUDE.md           # Developer guidelines & quick commands
└── DESIGN.md           # Product strategy and technical approach
```

---

## 🚀 Getting Started

### Prerequisites

Ensure you have Python 3.10+ and standard build tools (CMake, C++ compiler) installed.

### Installation & Compilation

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/your-username/exp-rs.git
    cd exp-rs
    ```

2.  **Install modular dependencies**:
    ```bash
    pip install PySide6 rasterio fiona pyproj numpy pytest eigen
    ```

3.  **Compile the C++ Extension**:
    ```bash
    mkdir build && cd build
    cmake ..
    make
    cp raster_ops.cpython*.so ..
    cd ..
    ```

### Running the App & Tests

*   **Launch the GUI Application**:
    ```bash
    python main.py
    ```
*   **Run Automated Test Suite**:
    ```bash
    PYTHONPATH=. pytest tests/
    ```

---

## 📚 Deep-Dive Technical Documentation

For in-depth analysis of architectural ports and performance features, see:
*   [DESIGN.md](file:///home/kevin/projects/exp-rs/DESIGN.md) - Product strategy, consideration of alternatives, and success criteria.
*   [DOCS_QGIS_IMPLEMENTATION.md](file:///home/kevin/projects/exp-rs/DOCS_QGIS_IMPLEMENTATION.md) - QGIS multi-threaded rendering pipeline, PROJ thread-safety context management, and affine/quadratic warping formulas.
*   [DOCS_OTB_IMPLEMENTATION.md](file:///home/kevin/projects/exp-rs/DOCS_OTB_IMPLEMENTATION.md) - Orfeo Toolbox streaming architecture, Eigen-powered PCA, and MeanShift segmentation interface designs.

---
*Last Updated: 2026-05-25*
