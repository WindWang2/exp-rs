# 02 — Collapse PluginManager into PluginHost in Core and Desktop Shell

**What to build:**
Eliminate `PluginManager`'s shallow pass-through wrapper files (`src/core/plugin_manager.h` and `src/core/plugin_manager.cpp`) and update desktop application windows (`MainWindow` and `SicnuMainWindow`) to hold `std::unique_ptr<PluginHost>` directly.

**Blocked by:** 01 — Canonicalize PluginHost Domain Glossary and ADR 0024.

**Status:** ready-for-agent

- [x] `src/core/plugin_manager.h` and `src/core/plugin_manager.cpp` deleted.
- [x] `src/core/CMakeLists.txt` updated to remove `plugin_manager.cpp` from `sicnu_core`.
- [x] `MainWindow` (`src/app/main_window.h`/`.cpp`) and `SicnuMainWindow` (`src/gui/main_window.h`/`.cpp`) updated to own `PluginHost` directly.
- [x] Application compiles cleanly without `PluginManager` references.
