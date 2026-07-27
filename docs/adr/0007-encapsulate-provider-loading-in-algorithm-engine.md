# 0007 Encapsulate Provider Loading in AlgorithmEngine Architecture

We decided to encapsulate processing provider registration (`GdalToolsProvider`, `OtbToolsProvider`, `QgisAlgorithmsProvider`, `GenericCliProvider`) and tool path preference discovery inside a single `AlgorithmEngine::initialize()` seam.

### Context & Decision
Previously, application startup in `main.cpp` manually read QSettings for GDAL/OTB tool paths, instantiated 4 separate provider classes, registered them to `QgsApplication::processingRegistry()`, and then populated `AlgorithmEngine`.

1. **Self-Contained Initialization**: Callers invoke `AlgorithmEngine::instance().initialize()`.
2. **Encapsulated Provider Lifecycle**: `AlgorithmEngine` manages tool path settings, provider instantiation, QGIS processing registry loading, and internal adapter cataloging behind its deep interface.
3. **Streamlined Startup**: `main.cpp` and test harnesses call a single line without leaking provider details.
