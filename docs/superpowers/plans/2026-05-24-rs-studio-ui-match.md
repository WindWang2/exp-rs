# RS Studio UI-Match Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reskin the PySide6 app's main window and existing dialogs to match the `RS Studio (standalone).html` prototype's main-workspace artboard exactly, wiring real data into the prototype's chrome.

**Architecture:** A small custom-widget toolkit (`gui/rs_widgets.py`) plus a recolorable SVG icon set (`gui/rs_icons.py`) reproduce the prototype's chrome 1:1. `gui/workspace.py` assembles a fixed 3-column `QSplitter` layout (data-browser + layers | map + console | toolbox + properties) with custom panel headers. `main.py`'s `MainWindow` becomes a thin coordinator that embeds `RsWorkspace`, builds the styled menu/toolbar, and wires the existing real backends (`MapCanvas` renderer, `LayerTreeModel`, `ProcessingToolbox`, `core.logger`, the `loaded_layers` metadata dict). The decorative fake-satellite scene is dropped; the real renderer shows real imagery with prototype overlay chrome on top.

**Tech Stack:** Python 3.13, PySide6 (Qt 6), pytest + pytest-qt 4.5, fontTools (woff2→ttf), QSvgRenderer.

**Spec:** `docs/superpowers/specs/2026-05-24-rs-studio-ui-match-design.md`

---

## Conventions for every task

- Run tests with: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest <path> -v`
- GUI tests follow the existing repo pattern (see `tests/test_canvas.py`): a module-level
  `_app = QApplication.instance() or QApplication(sys.argv)` before constructing widgets.
- Design tokens (hex values) come from the spec's "Design system reference" section.
- Commit after each task with the message shown in its final step.
- The prototype source of truth for markup is `scratch_extracted/*.js`; for CSS it is
  `scratch_template.html`.

## File structure (created / modified)

| File | Responsibility |
|------|----------------|
| `resources/fonts/*.ttf` | **Create.** IBM Plex Sans + Mono, converted from bundled woff2. |
| `gui/rs_fonts.py` | **Create.** `load_fonts()` registers bundled ttf via `QFontDatabase`. |
| `gui/rs_icons.py` | **Create.** `ICON_PATHS` dict + `rs_icon(name, size, color)` → `QIcon`; `rs_pixmap(...)`. |
| `gui/rs_widgets.py` | **Create.** `RsPanel`, `RsTabBar`, `RsToolBar`, `RsStatusBar`, `RsConsole`, `RsRowDelegate`, `RsPropertyPanel`, `RsSearchInput`, helpers. |
| `gui/map_overlay.py` | **Create.** `MapOverlayContainer` wrapping `MapCanvas` with repositioning overlay children. |
| `gui/workspace.py` | **Create.** `RsWorkspace` — assembles the 3-column layout, exposes sub-widgets. |
| `resources/styles.qss` | **Modify.** Full token palette, IBM Plex family, objectName-scoped rules for new widgets. |
| `main.py` | **Modify.** `MainWindow` embeds `RsWorkspace`; styled menubar/toolbar; AI toggle; wiring. `load_stylesheet`/`main()` call `load_fonts()`. |
| `gui/layer_tree.py` | **Modify.** Add swatch-color + meta data roles; apply `RsRowDelegate`. |
| `gui/prototype_views.py` | **Modify.** Restyle the 5 dialogs with the toolkit. |
| `gui/qgspropertiesdialog.py` / `gui/properties_dialog.py` | **Modify.** Restyle `LayerPropertiesDialog` with the toolkit. |
| `tests/test_rs_icons.py`, `tests/test_rs_widgets.py`, `tests/test_map_overlay.py`, `tests/test_workspace.py`, `tests/test_rs_fonts.py` | **Create.** Unit/smoke tests. |

---

## Task 1: Bundle and load IBM Plex fonts

**Files:**
- Create: `resources/fonts/` (ttf files), `gui/rs_fonts.py`
- Test: `tests/test_rs_fonts.py`

Qt's `QFontDatabase.addApplicationFont` returns `-1` for `.woff2` (verified). fontTools +
brotli are installed, so convert the bundled woff2 subsets to ttf once, at build time.

- [ ] **Step 1: Convert the needed woff2 files to ttf**

Run this one-off conversion script:

```bash
mkdir -p resources/fonts
PYTHONPATH=. python - <<'PY'
from fontTools.ttLib.woff2 import decompress
pairs = {
    "scratch_extracted/b48ed1d0-5145-4e19-be92-a887e580b0fb": "resources/fonts/IBMPlexSans.ttf",        # Sans latin (variable weights)
    "scratch_extracted/1d74665f-26e1-4201-b97f-8df561c5a5e6": "resources/fonts/IBMPlexMono-Regular.ttf", # Mono 400
    "scratch_extracted/a5578293-8cbe-446f-9e5c-210ab3ea50dd": "resources/fonts/IBMPlexMono-Medium.ttf",  # Mono 500
    "scratch_extracted/7cd1bdfb-1983-4e19-bf8e-a27183fcc569": "resources/fonts/IBMPlexMono-SemiBold.ttf",# Mono 600
}
for src, dst in pairs.items():
    decompress(src, dst)
    print("wrote", dst)
PY
```

Expected: four "wrote …" lines; files exist under `resources/fonts/`.

- [ ] **Step 2: Write the failing test**

```python
# tests/test_rs_fonts.py
import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QFontDatabase

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_fonts import load_fonts


def test_load_fonts_registers_plex_families():
    families = load_fonts()
    joined = " ".join(QFontDatabase.families())
    assert any("Plex Sans" in f for f in families), families
    assert any("Plex Mono" in f for f in families), families
    assert "IBM Plex Sans" in joined
```

- [ ] **Step 3: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_fonts.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'gui.rs_fonts'`.

- [ ] **Step 4: Implement `gui/rs_fonts.py`**

```python
# gui/rs_fonts.py
import os
from PySide6.QtGui import QFontDatabase

_FONT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "resources", "fonts")


def load_fonts():
    """Register bundled IBM Plex ttf files. Returns the list of loaded family names."""
    families = []
    if not os.path.isdir(_FONT_DIR):
        return families
    for name in sorted(os.listdir(_FONT_DIR)):
        if not name.lower().endswith((".ttf", ".otf")):
            continue
        fid = QFontDatabase.addApplicationFont(os.path.join(_FONT_DIR, name))
        if fid != -1:
            families.extend(QFontDatabase.applicationFontFamilies(fid))
    return families
```

- [ ] **Step 5: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_fonts.py -v`
Expected: PASS. (If a family name differs, note the exact string printed by
`QFontDatabase.applicationFontFamilies` — it is used verbatim in the QSS in Task 11.)

- [ ] **Step 6: Commit**

```bash
git add resources/fonts gui/rs_fonts.py tests/test_rs_fonts.py
git commit -m "feat(ui): bundle and load IBM Plex fonts"
```

---

## Task 2: Recolorable SVG icon set

**Files:**
- Create: `gui/rs_icons.py`
- Test: `tests/test_rs_icons.py`

Port the prototype's `icons.jsx` path table verbatim and render each as a stroked SVG
(stroke=color, width 1.5, fill none, viewBox 0 0 16 16) into a `QPixmap`/`QIcon`.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_rs_icons.py
import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QIcon

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_icons import rs_icon, rs_pixmap, ICON_PATHS


def test_known_icon_names_present():
    for name in ["folder", "save", "cursor", "pan", "zoomIn", "zoomOut", "zoomFit",
                 "cog", "wand", "workflow", "brain", "spark", "layers", "database",
                 "search", "filter", "refresh", "x", "chevD", "chevR", "raster",
                 "vector", "globe", "histogram", "palette", "crosshair", "bell", "user"]:
        assert name in ICON_PATHS, name


def test_rs_pixmap_is_non_null_and_sized():
    pm = rs_pixmap("folder", size=14, color="#1f6feb")
    assert not pm.isNull()
    # device-independent logical size is 14x14
    assert pm.width() >= 14 and pm.height() >= 14


def test_rs_icon_returns_icon():
    assert isinstance(rs_icon("cog", 16, "#2f3640"), QIcon)


def test_unknown_icon_is_blank_not_crash():
    assert not rs_pixmap("does-not-exist", 14, "#000").isNull() is False  # returns a (blank) pixmap, no exception
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_icons.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'gui.rs_icons'`.

- [ ] **Step 3: Implement `gui/rs_icons.py`**

Copy the full `ICON_PATHS` dict from `scratch_extracted/ca6af2cd-1330-42e4-a2a6-99f106d0660c.js`
(lines 5–79) verbatim into Python dict syntax, then add the renderer:

```python
# gui/rs_icons.py
from PySide6.QtCore import Qt, QByteArray, QSize
from PySide6.QtGui import QPixmap, QIcon, QPainter
from PySide6.QtSvg import QSvgRenderer

# --- ICON_PATHS: ported verbatim from prototype icons.jsx -------------------
ICON_PATHS = {
    "folder":     "M2 5a1 1 0 0 1 1-1h3.5l1.5 1.5H13a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V5z",
    "folderOpen": "M2 5a1 1 0 0 1 1-1h3.5l1.5 1.5H13a1 1 0 0 1 1 1V7M2 5v7a1 1 0 0 0 1 1h9l2-6H4l-2 4",
    "file":       "M9 2H4a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V6L9 2zM9 2v4h4",
    "raster":     "M2 3h12v10H2zM2 7h12M2 11h12M6 3v10M10 3v10",
    "vector":     "M3 4l5 3 5-3M3 12l5-3 5 3M8 7v2",
    "point":      "M8 4l4 8H4z",
    "database":   "M2 4c0-1 2.7-2 6-2s6 1 6 2-2.7 2-6 2-6-1-6-2zM2 4v8c0 1 2.7 2 6 2s6-1 6-2V4M2 8c0 1 2.7 2 6 2s6-1 6-2",
    "satellite":  "M3 8l5-5 5 5-5 5zM6 5l2 2M10 9l2 2M8 8l3-3",
    "hyperspectral": "M2 3h12v3H2zM2 6h12v3H2zM2 9h12v4H2",
    "layers":     "M8 2L2 5l6 3 6-3zM2 8l6 3 6-3M2 11l6 3 6-3",
    "eye":        "M1 8s2.5-4.5 7-4.5S15 8 15 8s-2.5 4.5-7 4.5S1 8 1 8zM8 10a2 2 0 1 0 0-4 2 2 0 0 0 0 4z",
    "eyeOff":     "M1 8s2.5-4.5 7-4.5c1.3 0 2.4.4 3.4 1M15 8s-2.5 4.5-7 4.5c-1.3 0-2.5-.4-3.5-1M2 2l12 12",
    "lock":       "M4 7V5a4 4 0 1 1 8 0v2M3 7h10v7H3z",
    "cursor":     "M3 3l4 10 2-4 4-2z",
    "pan":        "M8 2v6m0 0V6m0 2l-2-2m2 2l2-2M8 8v6m0 0v-2m0 2l-2-2m2 2l2-2M2 8h12",
    "zoomIn":     "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14M5 7h4M7 5v4",
    "zoomOut":    "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14M5 7h4",
    "zoomFit":    "M2 5V2h3M11 2h3v3M14 11v3h-3M5 14H2v-3M6 6h4v4H6z",
    "measure":    "M2 10l8-8 4 4-8 8zM5 5l1 1M7 3l1 1M3 7l1 1",
    "identify":   "M8 4v.01M8 7v5M3 8a5 5 0 1 0 10 0 5 5 0 0 0-10 0z",
    "crosshair":  "M8 2v3M8 11v3M2 8h3M11 8h3M8 8m-3 0a3 3 0 1 0 6 0 3 3 0 0 0-6 0z",
    "cog":        "M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM8 2v1.5M8 12.5V14M2 8h1.5M12.5 8H14M3.8 3.8l1 1M11.2 11.2l1 1M3.8 12.2l1-1M11.2 4.8l1-1",
    "wand":       "M3 13L13 3M11 2v2M14 5h-2M5 7l-2 2 1 1 2-2M9 3l1-1 1 1-1 1z",
    "workflow":   "M3 4a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM11 4a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM3 12a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM11 12a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM5 4h6M5 12h6M4 5v6M12 5v6",
    "play":       "M4 3l9 5-9 5z",
    "stop":       "M4 4h8v8H4z",
    "pause":      "M5 3v10M11 3v10",
    "plus":       "M8 3v10M3 8h10",
    "minus":      "M3 8h10",
    "x":          "M3 3l10 10M13 3L3 13",
    "check":      "M3 8l3 3 7-7",
    "chevR":      "M6 3l5 5-5 5",
    "chevD":      "M3 6l5 5 5-5",
    "chevU":      "M3 10l5-5 5 5",
    "chevL":      "M10 3L5 8l5 5",
    "more":       "M3 8h.01M8 8h.01M13 8h.01",
    "moreV":      "M8 3v.01M8 8v.01M8 13v.01",
    "dots":       "M3 8h.01M8 8h.01M13 8h.01",
    "search":     "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14",
    "filter":     "M2 3h12l-4 5v5l-4-2V8z",
    "refresh":    "M3 8a5 5 0 0 1 9-3l1 1M13 8a5 5 0 0 1-9 3l-1-1M11 3v3h3M5 13v-3H2",
    "save":       "M3 3h8l2 2v8H3zM5 3v4h5V3M5 9h6v4H5",
    "upload":     "M8 11V3M5 6l3-3 3 3M3 11v2h10v-2",
    "download":   "M8 3v8M5 8l3 3 3-3M3 13v-2h10v2",
    "spark":      "M8 2v3M8 11v3M2 8h3M11 8h3M5 5l2 2M9 9l2 2M5 11l2-2M9 7l2-2",
    "brain":      "M5 4a3 3 0 0 0-2 5 3 3 0 0 0 2 3v1h6v-1a3 3 0 0 0 2-3 3 3 0 0 0-2-5 3 3 0 0 0-3-1 3 3 0 0 0-3 1zM8 3v9",
    "agent":      "M4 5a2 2 0 1 1 4 0v3a2 2 0 1 1-4 0zM8 5a2 2 0 1 1 4 0v3a2 2 0 1 1-4 0zM6 11v2M10 11v2M5 13h6",
    "send":       "M2 8L14 3l-3 11-3-5z",
    "globe":      "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM2 8h12M8 2c2 2 3 4 3 6s-1 4-3 6c-2-2-3-4-3-6s1-4 3-6z",
    "pin":        "M8 2a4 4 0 0 0-4 4c0 3 4 8 4 8s4-5 4-8a4 4 0 0 0-4-4zM8 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2z",
    "bookmark":   "M4 2h8v12l-4-3-4 3z",
    "user":       "M8 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM3 14a5 5 0 0 1 10 0",
    "bell":       "M4 11V7a4 4 0 1 1 8 0v4l1 2H3zM6 13a2 2 0 0 0 4 0",
    "help":       "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM6 6a2 2 0 1 1 2 2v1M8 11v.01",
    "link":       "M7 9l2-2M5 11a2 2 0 0 1 0-3l2-2M11 5a2 2 0 0 1 0 3l-2 2",
    "grid":       "M2 2h5v5H2zM9 2h5v5H9zM2 9h5v5H2zM9 9h5v5H9z",
    "chart":      "M2 13h12M4 11V7M7 11V4M10 11V8M13 11V5",
    "histogram":  "M2 13h12M3 13V8M5 13V5M7 13V9M9 13V6M11 13V10M13 13V7",
    "spectrum":   "M2 11l2-3 2 1 2-4 2 2 2-1 2 2v4H2z",
    "palette":    "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12c0-1 1-1 1-2s-1-1-1-2 1-1 1-2-1-1-1-2zM5 6a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM11 6a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM4 9a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM12 9a1 1 0 1 0 0-2 1 1 0 0 0 0 2z",
}


def _svg(name, color):
    d = ICON_PATHS.get(name, "")
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" '
        f'fill="none" stroke="{color}" stroke-width="1.5" '
        f'stroke-linecap="round" stroke-linejoin="round"><path d="{d}"/></svg>'
    )


def rs_pixmap(name, size=14, color="#2f3640"):
    """Render a named icon to a crisp (2x) QPixmap recolored to `color`."""
    dpr = 2
    pm = QPixmap(QSize(size * dpr, size * dpr))
    pm.setDevicePixelRatio(dpr)
    pm.fill(Qt.transparent)
    renderer = QSvgRenderer(QByteArray(_svg(name, color).encode("utf-8")))
    painter = QPainter(pm)
    renderer.render(painter)
    painter.end()
    return pm


def rs_icon(name, size=14, color="#2f3640"):
    return QIcon(rs_pixmap(name, size, color))
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_icons.py -v`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add gui/rs_icons.py tests/test_rs_icons.py
git commit -m "feat(ui): add recolorable SVG icon set from prototype"
```

---

## Task 3: `RsPanel` — custom panel with header

**Files:**
- Create: `gui/rs_widgets.py` (start the module)
- Test: `tests/test_rs_widgets.py` (start the file)

`.rs-panel` = a `QFrame` with a 26px header (`objectName="rsPanelHeader"`, gradient via QSS),
leading icon, uppercase letter-spaced title (`objectName="rsPanelTitle"`), right action
buttons (18px, icon), and a body area exposed via `add_body_widget`.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_rs_widgets.py
import sys
from PySide6.QtWidgets import QApplication, QLabel

_app = QApplication.instance() or QApplication(sys.argv)

from gui.rs_widgets import RsPanel


def test_rspanel_title_and_body():
    p = RsPanel("处理工具箱", icon="cog",
                actions=[("search", "搜索"), ("bookmark", "收藏")])
    assert p.title_label.text() == "处理工具箱"
    assert len(p.action_buttons) == 2
    body = QLabel("hello")
    p.add_body_widget(body)
    assert body.parent() is not None
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rspanel_title_and_body -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'gui.rs_widgets'`.

- [ ] **Step 3: Implement `RsPanel` in `gui/rs_widgets.py`**

```python
# gui/rs_widgets.py
from PySide6.QtWidgets import (QFrame, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QToolButton, QSizePolicy)
from PySide6.QtCore import Qt, Signal
from gui.rs_icons import rs_icon

_T2 = "#5b6473"


class RsPanel(QFrame):
    """Prototype `.rs-panel`: 26px gradient header + body."""

    def __init__(self, title, icon=None, actions=None, parent=None):
        super().__init__(parent)
        self.setObjectName("rsPanel")
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        header = QWidget()
        header.setObjectName("rsPanelHeader")
        header.setFixedHeight(26)
        hl = QHBoxLayout(header)
        hl.setContentsMargins(8, 0, 6, 0)
        hl.setSpacing(6)
        if icon:
            ic = QLabel()
            ic.setPixmap(rs_icon(icon, 12, _T2).pixmap(12, 12))
            hl.addWidget(ic)
        self.title_label = QLabel(title)
        self.title_label.setObjectName("rsPanelTitle")
        hl.addWidget(self.title_label)
        hl.addStretch(1)

        self.action_buttons = []
        for name, tip in (actions or []):
            b = QToolButton()
            b.setObjectName("rsPanelAction")
            b.setIcon(rs_icon(name, 12, _T2))
            b.setToolTip(tip)
            b.setFixedSize(18, 18)
            b.setCursor(Qt.ArrowCursor)
            hl.addWidget(b)
            self.action_buttons.append(b)
        outer.addWidget(header)

        self.body = QWidget()
        self.body.setObjectName("rsPanelBody")
        self._body_layout = QVBoxLayout(self.body)
        self._body_layout.setContentsMargins(0, 0, 0, 0)
        self._body_layout.setSpacing(0)
        self.body.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        outer.addWidget(self.body, 1)

    def add_body_widget(self, w):
        self._body_layout.addWidget(w)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rspanel_title_and_body -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsPanel custom panel with header"
```

---

## Task 4: `RsTabBar` — custom tab strip

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

`.rs-tabs` = 26px row; tabs are `(id, label, icon?, count?)`; active tab gets a 2px accent
underline. Emits `tab_changed(id)`.

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsTabBar


def test_rstabbar_active_and_signal():
    bar = RsTabBar([("info", "信息", None, None),
                    ("symbol", "符号化", None, None)], active="info")
    seen = []
    bar.tab_changed.connect(seen.append)
    assert bar.active_id == "info"
    bar.set_active("symbol")
    assert bar.active_id == "symbol"
    assert seen == ["symbol"]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rstabbar_active_and_signal -v`
Expected: FAIL with `ImportError: cannot import name 'RsTabBar'`.

- [ ] **Step 3: Implement `RsTabBar`**

```python
# append to gui/rs_widgets.py
class RsTabBar(QWidget):
    tab_changed = Signal(str)

    def __init__(self, tabs, active=None, parent=None):
        super().__init__(parent)
        self.setObjectName("rsTabBar")
        self.setFixedHeight(26)
        self._buttons = {}
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self.active_id = active or (tabs[0][0] if tabs else None)
        for tid, label, icon, count in tabs:
            b = QToolButton()
            b.setObjectName("rsTab")
            b.setText(label if count is None else f"{label}  {count}")
            if icon:
                b.setIcon(rs_icon(icon, 12, _T2))
                b.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
            b.setCheckable(True)
            b.setChecked(tid == self.active_id)
            b.setCursor(Qt.ArrowCursor)
            b.clicked.connect(lambda _=False, t=tid: self.set_active(t))
            lay.addWidget(b)
            self._buttons[tid] = b
        lay.addStretch(1)

    def set_active(self, tid):
        if tid == self.active_id or tid not in self._buttons:
            self._sync()
            return
        self.active_id = tid
        self._sync()
        self.tab_changed.emit(tid)

    def _sync(self):
        for t, b in self._buttons.items():
            b.setChecked(t == self.active_id)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rstabbar_active_and_signal -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsTabBar custom tab strip"
```

---

## Task 5: `RsToolBar` — grouped icon buttons

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

`.rs-toolbar` = 36px row of groups separated by dividers. Each item:
`{"id","icon","label"?,"checkable"?,"checked"?,"tip"?}`. Buttons emit `triggered(id)`.
Checkable items in the same `exclusive` group behave like radio (nav tools).

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsToolBar


def test_rstoolbar_groups_and_trigger():
    tb = RsToolBar()
    tb.add_group([{"id": "open", "icon": "folder", "tip": "打开"},
                  {"id": "save", "icon": "save", "tip": "保存"}])
    tb.add_group([{"id": "pan", "icon": "pan", "checkable": True, "checked": True},
                  {"id": "zin", "icon": "zoomIn", "checkable": True}],
                 exclusive=True)
    fired = []
    tb.triggered.connect(fired.append)
    tb.button("open").click()
    assert fired == ["open"]
    # exclusive: clicking zin unchecks pan
    tb.button("zin").click()
    assert tb.button("zin").isChecked() and not tb.button("pan").isChecked()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rstoolbar_groups_and_trigger -v`
Expected: FAIL with `ImportError: cannot import name 'RsToolBar'`.

- [ ] **Step 3: Implement `RsToolBar`**

```python
# append to gui/rs_widgets.py
from PySide6.QtWidgets import QFrame as _QFrame  # alias kept for clarity
_T1 = "#2f3640"


class RsToolBar(QWidget):
    triggered = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("rsToolBar")
        self.setFixedHeight(36)
        self._lay = QHBoxLayout(self)
        self._lay.setContentsMargins(4, 0, 4, 0)
        self._lay.setSpacing(0)
        self._lay.addStretch(1)
        self._buttons = {}
        self._exclusive_groups = []

    def add_group(self, items, exclusive=False):
        group = QWidget()
        group.setObjectName("rsToolGroup")
        gl = QHBoxLayout(group)
        gl.setContentsMargins(4, 0, 4, 0)
        gl.setSpacing(1)
        ids = []
        for it in items:
            b = QToolButton()
            b.setObjectName("rsToolBtn")
            b.setIcon(rs_icon(it["icon"], 14, _T1))
            if it.get("label"):
                b.setText(it["label"])
                b.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
            b.setToolTip(it.get("tip", it.get("label", "")))
            b.setCheckable(bool(it.get("checkable")))
            b.setChecked(bool(it.get("checked")))
            b.setCursor(Qt.ArrowCursor)
            bid = it["id"]
            b.clicked.connect(lambda _=False, x=bid: self._on_click(x))
            gl.addWidget(b)
            self._buttons[bid] = b
            ids.append(bid)
        if exclusive:
            self._exclusive_groups.append(ids)
        # insert before the trailing stretch
        self._lay.insertWidget(self._lay.count() - 1, group)
        return group

    def button(self, bid):
        return self._buttons[bid]

    def _on_click(self, bid):
        for grp in self._exclusive_groups:
            if bid in grp:
                for other in grp:
                    self._buttons[other].setChecked(other == bid)
        self.triggered.emit(bid)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rstoolbar_groups_and_trigger -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsToolBar grouped icon toolbar"
```

---

## Task 6: `RsStatusBar` — segmented status bar

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

22px mono row of segments. Setters update coord/scale/crs/message; right side shows
static thread/render/cache placeholders.

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsStatusBar


def test_rsstatusbar_setters():
    sb = RsStatusBar()
    sb.set_coord("116.4074° E, 39.9042° N")
    sb.set_scale("1 : 50,000")
    sb.set_crs("EPSG:4326 — WGS 84")
    assert "116.4074" in sb.coord_label.text()
    assert "50,000" in sb.scale_label.text()
    assert "EPSG:4326" in sb.crs_label.text()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rsstatusbar_setters -v`
Expected: FAIL with `ImportError`.

- [ ] **Step 3: Implement `RsStatusBar`**

```python
# append to gui/rs_widgets.py
def _seg(text="", obj="rsSeg", color=None):
    lab = QLabel(text)
    lab.setObjectName(obj)
    if color:
        lab.setStyleSheet(f"color:{color};")
    return lab


class RsStatusBar(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("rsStatusBar")
        self.setFixedHeight(22)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self.ready_label = _seg("● 就绪", "rsSegOk")
        self.coord_label = _seg("—", "rsSeg")
        self.scale_label = _seg("1 : 1", "rsSeg")
        self.crs_label = _seg("EPSG:3857", "rsSeg")
        self.message_label = _seg("", "rsSegMuted")
        for w in (self.ready_label, self.coord_label, self.scale_label,
                  self.crs_label, self.message_label):
            lay.addWidget(w)
        lay.addStretch(1)
        for w in (_seg("线程 4 · 2 任务", "rsSegMuted"),
                  _seg("渲染 18ms", "rsSegMuted"),
                  _seg("缓存 2.4 GB", "rsSegMuted")):
            lay.addWidget(w)

    def set_coord(self, t): self.coord_label.setText(t)
    def set_scale(self, t): self.scale_label.setText(t)
    def set_crs(self, t): self.crs_label.setText(t)
    def set_message(self, t): self.message_label.setText(t)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rsstatusbar_setters -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsStatusBar segmented status bar"
```

---

## Task 7: `RsRowDelegate` + `RsSearchInput`

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

`RsRowDelegate` paints `.rs-row` from item data roles:
`Qt.UserRole+10 swatch(hex)`, `Qt.UserRole+11 meta(str)`, `Qt.UserRole+12 icon(name)`.
It defers checkbox/expand to the view; it draws swatch + leading icon + name (elided) +
mono meta, and a 2px accent left bar on selection. Row height 22px.

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsRowDelegate, RsSearchInput, ROLE_SWATCH, ROLE_META, ROLE_ICON
from PySide6.QtGui import QStandardItemModel, QStandardItem
from PySide6.QtWidgets import QStyleOptionViewItem
from PySide6.QtCore import QSize


def test_rowdelegate_sizehint_height():
    d = RsRowDelegate()
    model = QStandardItemModel()
    it = QStandardItem("NDVI_2025.tif")
    it.setData("#7dd35c", ROLE_SWATCH)
    it.setData("raster", ROLE_META)
    it.setData("raster", ROLE_ICON)
    model.appendRow(it)
    sz = d.sizeHint(QStyleOptionViewItem(), model.index(0, 0))
    assert sz.height() == 22


def test_search_input_text():
    s = RsSearchInput("搜索 1,247 个算法…")
    s.line.setText("ndvi")
    assert s.text() == "ndvi"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py -k "rowdelegate or search_input" -v`
Expected: FAIL with `ImportError`.

- [ ] **Step 3: Implement `RsRowDelegate` and `RsSearchInput`**

```python
# append to gui/rs_widgets.py
from PySide6.QtWidgets import QStyledItemDelegate, QLineEdit
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtCore import QSize, QRect

ROLE_SWATCH = Qt.UserRole + 10
ROLE_META = Qt.UserRole + 11
ROLE_ICON = Qt.UserRole + 12

_AC = "#1f6feb"
_AC_SOFT = QColor(31, 111, 235, 26)   # rgba(31,111,235,0.10)
_BG3 = QColor(0xee, 0xf1, 0xf5)
_T0 = "#14171c"
_T3 = "#8a92a0"


class RsRowDelegate(QStyledItemDelegate):
    def sizeHint(self, option, index):
        return QSize(option.rect.width(), 22)

    def paint(self, painter: QPainter, option, index):
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing, True)
        r = option.rect
        selected = bool(option.state & QStyle.State_Selected)
        hovered = bool(option.state & QStyle.State_MouseOver)
        if selected:
            painter.fillRect(r, _AC_SOFT)
            painter.fillRect(QRect(r.left(), r.top(), 2, r.height()), QColor(_AC))
        elif hovered:
            painter.fillRect(r, _BG3)

        x = r.left() + 6
        cy = r.center().y()
        swatch = index.data(ROLE_SWATCH)
        if swatch:
            painter.setPen(QPen(QColor(0, 0, 0, 26)))
            painter.setBrush(QColor(swatch))
            painter.drawRoundedRect(QRect(x, cy - 6, 12, 12), 2, 2)
            x += 18
        icon_name = index.data(ROLE_ICON)
        if icon_name:
            pm = rs_icon(icon_name, 13, _T2).pixmap(13, 13)
            painter.drawPixmap(x, cy - 7, pm)
            x += 19

        meta = index.data(ROLE_META)
        meta_w = 0
        if meta:
            painter.setPen(QColor(_T3))
            fm = painter.fontMetrics()
            meta_w = fm.horizontalAdvance(meta) + 10
            painter.drawText(QRect(r.right() - meta_w, r.top(), meta_w - 6, r.height()),
                             Qt.AlignRight | Qt.AlignVCenter, meta)

        name = index.data(Qt.DisplayRole) or ""
        painter.setPen(QColor(_T0 if selected else _T1))
        name_rect = QRect(x, r.top(), r.right() - meta_w - x, r.height())
        elided = painter.fontMetrics().elidedText(name, Qt.ElideRight, name_rect.width())
        painter.drawText(name_rect, Qt.AlignLeft | Qt.AlignVCenter, elided)
        painter.restore()


class RsSearchInput(QFrame):
    def __init__(self, placeholder="", parent=None):
        super().__init__(parent)
        self.setObjectName("rsSearchInput")
        self.setFixedHeight(24)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(6, 0, 6, 0)
        lay.setSpacing(4)
        ic = QLabel()
        ic.setPixmap(rs_icon("search", 12, _T3).pixmap(12, 12))
        lay.addWidget(ic)
        self.line = QLineEdit()
        self.line.setObjectName("rsSearchEdit")
        self.line.setPlaceholderText(placeholder)
        self.line.setFrame(False)
        lay.addWidget(self.line, 1)

    def text(self):
        return self.line.text()
```

Add the needed imports at the top of `gui/rs_widgets.py`:
`from PySide6.QtWidgets import QStyle`.

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py -k "rowdelegate or search_input" -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsRowDelegate and RsSearchInput"
```

---

## Task 8: `RsConsole` — log/console panel wired to logger

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

`RsTabBar` (日志/任务/Python 控制台/历史) over a mono read-only `QPlainTextEdit`. A
`logging.Handler` (reuse `gui.log_dock.QLogHandler`) feeds lines `HH:MM:SS LEVEL module msg`
with level colors. `append_log(time, level, module, msg)` is the unit entry point.

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsConsole


def test_rsconsole_append():
    c = RsConsole()
    c.append_log("10:24:12", "INFO", "gdal", "opened dataset")
    c.append_log("10:24:13", "WARN", "crs", "reprojected")
    txt = c.view.toPlainText()
    assert "opened dataset" in txt and "reprojected" in txt
    assert c.tabs.active_id == "log"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rsconsole_append -v`
Expected: FAIL with `ImportError`.

- [ ] **Step 3: Implement `RsConsole`**

```python
# append to gui/rs_widgets.py
import logging
from PySide6.QtWidgets import QPlainTextEdit

_LEVEL_COLOR = {"INFO": "#1a7f37", "WARN": "#bf8700", "WARNING": "#bf8700",
                "ERROR": "#cf222e", "DEBUG": "#8a92a0", "SUCCESS": "#1a7f37"}


class RsConsole(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("rsConsole")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self.tabs = RsTabBar([("log", "日志", "file", 0),
                              ("tasks", "任务", "cog", 0),
                              ("python", "Python 控制台", "workflow", None),
                              ("hist", "历史", "refresh", None)], active="log")
        lay.addWidget(self.tabs)
        self.view = QPlainTextEdit()
        self.view.setObjectName("rsConsoleView")
        self.view.setReadOnly(True)
        lay.addWidget(self.view, 1)
        self._count = 0

    def append_log(self, time_str, level, module, msg):
        color = _LEVEL_COLOR.get(level.upper(), "#5b6473")
        html = (f"<span style='color:#b8bec7'>{time_str}</span> "
                f"<span style='color:{color}'>{level:&lt;5}</span> "
                f"<span style='color:#8a92a0'>{module}</span> "
                f"<span style='color:#2f3640'>{msg}</span>")
        self.view.appendHtml(html.replace("&lt;", "<"))
        self._count += 1
        self.tabs._buttons["log"].setText(f"日志  {self._count}")

    def attach_logger(self, logger_name="RSStudio"):
        """Stream real logs into the console (GUI-thread safe via QLogHandler)."""
        from gui.log_dock import QLogHandler
        handler = QLogHandler()
        handler.signals.log_emitted.connect(self._on_log)
        logging.getLogger(logger_name).addHandler(handler)
        self._handler = handler

    def _on_log(self, raw_msg, levelno, formatted, time_str):
        level = logging.getLevelName(levelno)
        module = ""
        parts = formatted.split(" - ")
        if len(parts) >= 3:
            module = parts[2].strip("[]")
        self.append_log(time_str, level, module, raw_msg)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_rsconsole_append -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsConsole log panel wired to logger"
```

---

## Task 9: `RsPropertyPanel` — layer properties inspector

**Files:**
- Modify: `gui/rs_widgets.py`, `tests/test_rs_widgets.py`

`RsTabBar` (信息/符号化/直方图/元数据) over a scroll area built from `Section` blocks of
`prop_row`s. `set_layer(meta)` takes the `loaded_layers[id]` dict
(`{"name","type","path","extent"}`) and the live layer object (for opacity/crs). Empty state
when `None`.

- [ ] **Step 1: Write the failing test**

```python
# append to tests/test_rs_widgets.py
from gui.rs_widgets import RsPropertyPanel


def test_property_panel_populates_from_meta():
    p = RsPropertyPanel()
    p.set_layer({"name": "B04_red", "type": "raster",
                 "path": "/data/T50TMK_B04.jp2", "extent": None})
    txt = p.dump_text()
    assert "B04_red" in txt
    assert "T50TMK_B04.jp2" in txt
    # empty state
    p.set_layer(None)
    assert "未选择图层" in p.dump_text()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_property_panel_populates_from_meta -v`
Expected: FAIL with `ImportError`.

- [ ] **Step 3: Implement `RsPropertyPanel` and helpers**

```python
# append to gui/rs_widgets.py
from PySide6.QtWidgets import QScrollArea
import os as _os


def section_header(label):
    w = QLabel(label)
    w.setObjectName("rsSectionHeader")
    return w


def prop_row(k, v, mono=True):
    row = QWidget()
    row.setObjectName("rsPropRow")
    lay = QHBoxLayout(row)
    lay.setContentsMargins(10, 3, 10, 3)
    kl = QLabel(str(k)); kl.setObjectName("rsPropKey")
    vl = QLabel(str(v)); vl.setObjectName("rsPropValMono" if mono else "rsPropVal")
    vl.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
    lay.addWidget(kl); lay.addStretch(1); lay.addWidget(vl)
    return row


class RsPropertyPanel(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("rsPropertyPanel")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self.tabs = RsTabBar([("info", "信息", None, None),
                              ("symbol", "符号化", None, None),
                              ("hist", "直方图", None, None),
                              ("meta", "元数据", None, None)], active="info")
        lay.addWidget(self.tabs)
        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setObjectName("rsPropScroll")
        lay.addWidget(self.scroll, 1)
        self._content = QWidget()
        self._clayout = QVBoxLayout(self._content)
        self._clayout.setContentsMargins(0, 4, 0, 8)
        self._clayout.setSpacing(0)
        self.scroll.setWidget(self._content)
        self.set_layer(None)

    def _clear(self):
        while self._clayout.count():
            item = self._clayout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

    def set_layer(self, meta, layer=None):
        self._clear()
        if not meta:
            empty = QLabel("未选择图层")
            empty.setObjectName("rsPropEmpty")
            empty.setAlignment(Qt.AlignCenter)
            self._clayout.addWidget(empty)
            self._clayout.addStretch(1)
            return
        self._clayout.addWidget(section_header("数据源"))
        self._clayout.addWidget(prop_row("名称", meta.get("name", "—")))
        self._clayout.addWidget(prop_row("路径", _os.path.basename(meta.get("path", "—"))))
        self._clayout.addWidget(prop_row("类型", meta.get("type", "—")))
        ext = meta.get("extent")
        if ext is not None and hasattr(ext, "xMinimum"):
            self._clayout.addWidget(section_header("坐标系"))
            self._clayout.addWidget(
                prop_row("范围", f"{ext.xMinimum():.1f}, {ext.yMinimum():.1f}"))
        if layer is not None and hasattr(layer, "opacity"):
            self._clayout.addWidget(section_header("渲染"))
            self._clayout.addWidget(
                prop_row("不透明度", f"{int(getattr(layer, 'opacity', 1.0) * 100)} %"))
        self._clayout.addStretch(1)

    def dump_text(self):
        out = []
        for i in range(self._clayout.count()):
            w = self._clayout.itemAt(i).widget()
            if isinstance(w, QLabel):
                out.append(w.text())
            elif w is not None:
                for lab in w.findChildren(QLabel):
                    out.append(lab.text())
        return " | ".join(out)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py::test_property_panel_populates_from_meta -v`
Expected: PASS.

- [ ] **Step 5: Run full widgets test file**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_rs_widgets.py -v`
Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add gui/rs_widgets.py tests/test_rs_widgets.py
git commit -m "feat(ui): RsPropertyPanel layer inspector"
```

---

## Task 10: Map overlay container

**Files:**
- Create: `gui/map_overlay.py`
- Test: `tests/test_map_overlay.py`

Wrap the existing `MapCanvas` and lay overlay child widgets on top, repositioned on resize:
top-left image-info pill, top-right band-combo toggles, north arrow, scale bar, center
crosshair. Overlays are transparent to mouse except the band-combo toggles. The embedded
canvas is exposed as `.canvas`.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_map_overlay.py
import sys
from PySide6.QtWidgets import QApplication

_app = QApplication.instance() or QApplication(sys.argv)

from gui.map_overlay import MapOverlayContainer
from gui.canvas import MapCanvas


def test_overlay_wraps_canvas_and_resizes():
    c = MapOverlayContainer()
    assert isinstance(c.canvas, MapCanvas)
    c.resize(800, 600)
    c.show()
    _app.processEvents()
    # band combo + scale bar overlays exist and stay within bounds
    assert c.scale_bar.parent() is c
    assert c.band_combo.parent() is c
    assert c.band_combo.x() + c.band_combo.width() <= c.width() + 1
    c.set_image_info("Sentinel-2A · L2A · 2025-04-12 · 4-3-2")
    assert "Sentinel" in c.info_pill.text()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_map_overlay.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'gui.map_overlay'`.

- [ ] **Step 3: Implement `gui/map_overlay.py`**

```python
# gui/map_overlay.py
from PySide6.QtWidgets import QWidget, QLabel, QHBoxLayout, QToolButton, QButtonGroup
from PySide6.QtCore import Qt
from gui.canvas import MapCanvas


class MapOverlayContainer(QWidget):
    """Holds a MapCanvas with prototype overlay chrome positioned on top."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("mapOverlayContainer")
        self.canvas = MapCanvas(self)

        self.info_pill = QLabel("", self)
        self.info_pill.setObjectName("mapInfoPill")
        self.info_pill.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.info_pill.hide()

        self.band_combo = QWidget(self)
        self.band_combo.setObjectName("mapBandCombo")
        bl = QHBoxLayout(self.band_combo)
        bl.setContentsMargins(0, 0, 0, 0)
        bl.setSpacing(4)
        self._band_group = QButtonGroup(self)
        self._band_group.setExclusive(True)
        for i, label in enumerate(["真彩色", "假彩色", "NDVI", "分类"]):
            b = QToolButton()
            b.setObjectName("mapBandBtn")
            b.setText(label)
            b.setCheckable(True)
            b.setChecked(i == 0)
            bl.addWidget(b)
            self._band_group.addButton(b, i)

        self.north = QLabel("N", self)
        self.north.setObjectName("mapNorth")
        self.north.setAlignment(Qt.AlignCenter)
        self.north.setAttribute(Qt.WA_TransparentForMouseEvents, True)

        self.scale_bar = QLabel("0    2    4 km", self)
        self.scale_bar.setObjectName("mapScaleBar")
        self.scale_bar.setAttribute(Qt.WA_TransparentForMouseEvents, True)

        self.crosshair = QLabel(self)
        self.crosshair.setObjectName("mapCrosshair")
        self.crosshair.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.crosshair.setFixedSize(24, 24)

    def set_image_info(self, text):
        self.info_pill.setText(text)
        self.info_pill.setVisible(bool(text))
        self._reposition()

    def resizeEvent(self, event):
        self.canvas.setGeometry(0, 0, self.width(), self.height())
        self._reposition()
        super().resizeEvent(event)

    def _reposition(self):
        w, h = self.width(), self.height()
        self.info_pill.adjustSize()
        self.info_pill.move(8, 8)
        self.band_combo.adjustSize()
        self.band_combo.move(w - self.band_combo.width() - 8, 8)
        self.north.setFixedSize(34, 34)
        self.north.move(w - 48, 50)
        self.scale_bar.adjustSize()
        self.scale_bar.move(12, h - self.scale_bar.height() - 12)
        self.crosshair.move((w - 24) // 2, (h - 24) // 2)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_map_overlay.py -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/map_overlay.py tests/test_map_overlay.py
git commit -m "feat(ui): map overlay container with prototype chrome"
```

---

## Task 11: QSS — tokens, fonts, and widget styling

**Files:**
- Modify: `resources/styles.qss`

Add objectName-scoped rules for the new widgets. This is visual; verification is the app
run in Task 13. Use the exact hex tokens from the spec.

- [ ] **Step 1: Append widget styling to `resources/styles.qss`**

```css
/* ============ IBM Plex base ============ */
QWidget {
    font-family: 'IBM Plex Sans', 'Segoe UI', 'PingFang SC', 'Microsoft YaHei', sans-serif;
}

/* ============ RsPanel ============ */
#rsPanel { background: #ffffff; border: none; }
#rsPanelHeader {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #fafbfc, stop:1 #f1f3f6);
    border-bottom: 1px solid #d4d8de;
}
#rsPanelTitle {
    color: #14171c; font-weight: 600; font-size: 11px;
    text-transform: uppercase; letter-spacing: 1px;
}
QToolButton#rsPanelAction { border: none; border-radius: 2px; background: transparent; }
QToolButton#rsPanelAction:hover { background: #eef1f5; }
#rsPanelBody { background: #ffffff; }

/* ============ RsTabBar ============ */
#rsTabBar { background: #f6f7f9; border-bottom: 1px solid #d4d8de; }
QToolButton#rsTab {
    border: none; background: transparent; color: #5b6473;
    padding: 0 12px; font-size: 11px; border-right: 1px solid #e4e7eb;
}
QToolButton#rsTab:hover { color: #2f3640; }
QToolButton#rsTab:checked {
    background: #ffffff; color: #14171c;
    border-bottom: 2px solid #1f6feb;
}

/* ============ RsToolBar ============ */
#rsToolBar { background: #ffffff; border-bottom: 1px solid #d4d8de; }
#rsToolGroup { border-right: 1px solid #e4e7eb; }
QToolButton#rsToolBtn {
    border: 1px solid transparent; border-radius: 3px; padding: 0 6px;
    min-height: 26px; color: #2f3640; font-size: 11px;
}
QToolButton#rsToolBtn:hover { background: #eef1f5; color: #14171c; }
QToolButton#rsToolBtn:checked {
    background: rgba(31,111,235,0.10); color: #1f6feb;
    border: 1px solid rgba(31,111,235,0.42);
}

/* ============ RsStatusBar ============ */
#rsStatusBar { background: #f6f7f9; border-top: 1px solid #d4d8de; }
#rsStatusBar QLabel {
    font-family: 'IBM Plex Mono', 'SF Mono', monospace; font-size: 11px;
    color: #5b6473; padding: 0 10px; border-right: 1px solid #e4e7eb;
}
QLabel#rsSegOk { color: #1a7f37; }
QLabel#rsSegMuted { color: #8a92a0; }

/* ============ RsSearchInput ============ */
#rsSearchInput { background: #f6f7f9; border: 1px solid #d4d8de; border-radius: 3px; }
QLineEdit#rsSearchEdit { background: transparent; border: none; font-size: 12px; color: #2f3640; }

/* ============ RsConsole ============ */
#rsConsole { background: #ffffff; border-top: 1px solid #e4e7eb; }
QPlainTextEdit#rsConsoleView {
    background: #ffffff; border: none; padding: 6px 12px;
    font-family: 'IBM Plex Mono', 'SF Mono', monospace; font-size: 12px;
    color: #5b6473;
}

/* ============ Property panel ============ */
#rsPropScroll { background: #ffffff; border: none; }
QLabel#rsSectionHeader {
    color: #8a92a0; font-size: 10px; font-weight: 600;
    text-transform: uppercase; letter-spacing: 1px; padding: 8px 10px 4px;
}
QLabel#rsPropKey { color: #5b6473; font-size: 12px; }
QLabel#rsPropVal { color: #14171c; font-size: 12px; }
QLabel#rsPropValMono { color: #14171c; font-family: 'IBM Plex Mono','SF Mono',monospace; font-size: 12px; }
QLabel#rsPropEmpty { color: #8a92a0; font-size: 12px; padding: 24px; }

/* ============ Map overlays ============ */
#mapOverlayContainer { background: #e9ecf0; }
QLabel#mapInfoPill {
    background: rgba(255,255,255,0.92); border: 1px solid rgba(20,23,28,0.10);
    border-radius: 3px; padding: 4px 8px;
    font-family: 'IBM Plex Mono','SF Mono',monospace; font-size: 11px; color: #2f3640;
}
QToolButton#mapBandBtn {
    background: rgba(255,255,255,0.92); border: 1px solid rgba(20,23,28,0.10);
    border-radius: 3px; padding: 4px 9px; font-size: 11px; color: #2f3640;
}
QToolButton#mapBandBtn:checked {
    background: #ffffff; color: #1f6feb; border: 1px solid rgba(31,111,235,0.42); font-weight: 600;
}
QLabel#mapNorth {
    background: rgba(255,255,255,0.92); border: 1px solid rgba(20,23,28,0.10);
    border-radius: 17px; color: #1f6feb; font-weight: 700; font-size: 12px;
}
QLabel#mapScaleBar {
    background: rgba(255,255,255,0.92); border: 1px solid rgba(20,23,28,0.10);
    border-radius: 3px; padding: 4px 8px;
    font-family: 'IBM Plex Mono','SF Mono',monospace; font-size: 11px; color: #14171c;
}

/* ============ Menu bar (brand) ============ */
QMenuBar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:1 #f4f6f9);
           border-bottom: 1px solid #d4d8de; }
QLabel#rsBrandLogo {
    background: #1f6feb; color: #ffffff; font-weight: 700; font-size: 10px;
    border-radius: 3px; min-width: 18px; min-height: 18px; qproperty-alignment: AlignCenter;
}
QLabel#rsBrandName { color: #14171c; font-weight: 600; letter-spacing: 1px; padding: 0 8px; }
```

- [ ] **Step 2: Verify the stylesheet parses (no crash on load)**

Run:
```bash
QT_QPA_PLATFORM=offscreen PYTHONPATH=. python -c "
import sys
from PySide6.QtWidgets import QApplication
app = QApplication.instance() or QApplication(sys.argv)
open('resources/styles.qss').read()
app.setStyleSheet(open('resources/styles.qss').read())
print('stylesheet applied OK')
"
```
Expected: `stylesheet applied OK`.

- [ ] **Step 3: Commit**

```bash
git add resources/styles.qss
git commit -m "feat(ui): QSS tokens and styling for RS widgets"
```

---

## Task 12: `RsWorkspace` assembly

**Files:**
- Create: `gui/workspace.py`
- Test: `tests/test_workspace.py`

Assemble the 3-column layout, exposing `.toolbar`, `.canvas`, `.layer_view`, `.layer_model`,
`.data_browser`, `.toolbox`, `.property_panel`, `.console`, `.status`.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_workspace.py
import sys
from PySide6.QtWidgets import QApplication, QSplitter

_app = QApplication.instance() or QApplication(sys.argv)

from gui.workspace import RsWorkspace
from gui.rs_widgets import RsToolBar, RsStatusBar, RsConsole, RsPropertyPanel
from gui.map_overlay import MapOverlayContainer


def test_workspace_exposes_parts():
    w = RsWorkspace()
    assert isinstance(w.toolbar, RsToolBar)
    assert isinstance(w.status, RsStatusBar)
    assert isinstance(w.console, RsConsole)
    assert isinstance(w.property_panel, RsPropertyPanel)
    assert isinstance(w.map_container, MapOverlayContainer)
    assert w.canvas is w.map_container.canvas
    # three top-level columns in the horizontal splitter
    assert w.main_splitter.count() == 3


def test_workspace_default_sizes():
    w = RsWorkspace()
    w.resize(1440, 900)
    w.show()
    _app.processEvents()
    sizes = w.main_splitter.sizes()
    assert sizes[0] > 0 and sizes[2] > 0  # left & right docks visible
```

- [ ] **Step 2: Run test to verify it fails**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_workspace.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'gui.workspace'`.

- [ ] **Step 3: Implement `gui/workspace.py`**

```python
# gui/workspace.py
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QSplitter, QTreeView, QTreeWidget)
from PySide6.QtCore import Qt
from gui.rs_widgets import (RsToolBar, RsStatusBar, RsConsole, RsPropertyPanel,
                            RsPanel, RsTabBar, RsSearchInput, RsRowDelegate)
from gui.map_overlay import MapOverlayContainer
from gui.layer_tree import LayerTreeModel, LayerTreeView


class RsWorkspace(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Toolbar (groups in prototype order)
        self.toolbar = RsToolBar()
        self.toolbar.add_group([
            {"id": "open", "icon": "folder", "tip": "打开"},
            {"id": "save", "icon": "save", "tip": "保存工程"},
            {"id": "new", "icon": "plus", "tip": "新建"}])
        self.toolbar.add_group([
            {"id": "select", "icon": "cursor", "checkable": True, "tip": "选择"},
            {"id": "pan", "icon": "pan", "checkable": True, "checked": True, "tip": "漫游"},
            {"id": "zoomIn", "icon": "zoomIn", "tip": "放大"},
            {"id": "zoomOut", "icon": "zoomOut", "tip": "缩小"},
            {"id": "zoomFit", "icon": "zoomFit", "tip": "全图"}], exclusive=True)
        self.toolbar.add_group([
            {"id": "identify", "icon": "identify", "tip": "要素信息"},
            {"id": "measure", "icon": "measure", "tip": "量测"},
            {"id": "sample", "icon": "crosshair", "tip": "光谱采样"}])
        self.toolbar.add_group([
            {"id": "bandcombo", "icon": "raster", "label": "波段合成"},
            {"id": "stretch", "icon": "palette", "label": "拉伸"},
            {"id": "hist", "icon": "histogram", "label": "直方图"}])
        self.toolbar.add_group([
            {"id": "classify", "icon": "wand", "label": "分类", "tip": "监督分类"},
            {"id": "model", "icon": "workflow", "label": "模型", "tip": "模型构建器"},
            {"id": "process", "icon": "cog", "label": "处理", "tip": "处理工具箱"}])
        self.toolbar.add_group([
            {"id": "ai", "icon": "spark", "label": "AI 助手", "checkable": True,
             "tip": "打开 AI 助手"}])
        root.addWidget(self.toolbar)

        # Main horizontal splitter
        self.main_splitter = QSplitter(Qt.Horizontal)
        root.addWidget(self.main_splitter, 1)

        # LEFT: data browser + layers
        left = QSplitter(Qt.Vertical)
        self.data_browser = QTreeView()
        self.data_browser.setHeaderHidden(True)
        self.data_browser.setItemDelegate(RsRowDelegate(self.data_browser))
        browser_panel = RsPanel("数据浏览器", icon="database",
                                actions=[("plus", "添加数据源"), ("refresh", "刷新")])
        browser_panel.add_body_widget(self.data_browser)
        self.layer_model = LayerTreeModel(self)
        self.layer_view = LayerTreeView(self)
        self.layer_view.setModel(self.layer_model)
        self.layer_view.setItemDelegate(RsRowDelegate(self.layer_view))
        layers_panel = RsPanel("图层", icon="layers",
                               actions=[("plus", "添加图层"), ("filter", "筛选"),
                                        ("moreV", "更多")])
        layers_panel.add_body_widget(self.layer_view)
        left.addWidget(browser_panel)
        left.addWidget(layers_panel)
        left.setSizes([260, 600])

        # CENTER: tabs + map + console
        center = QWidget()
        cl = QVBoxLayout(center)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        self.view_tabs = RsTabBar([("map", "地图视图 1", "globe", None),
                                   ("layout", "布局视图", "grid", None),
                                   ("3d", "3D 视图", "cog", None)], active="map")
        cl.addWidget(self.view_tabs)
        center_split = QSplitter(Qt.Vertical)
        self.map_container = MapOverlayContainer()
        self.canvas = self.map_container.canvas
        self.console = RsConsole()
        center_split.addWidget(self.map_container)
        center_split.addWidget(self.console)
        center_split.setSizes([700, 160])
        cl.addWidget(center_split, 1)

        # RIGHT: toolbox + properties
        right = QSplitter(Qt.Vertical)
        from gui.toolbox import ProcessingToolbox
        toolbox_panel = RsPanel("处理工具箱", icon="cog",
                                actions=[("search", "搜索"), ("bookmark", "收藏")])
        self.toolbox = ProcessingToolbox()
        toolbox_panel.add_body_widget(self.toolbox)
        self.property_panel = RsPropertyPanel()
        props_panel = RsPanel("图层属性", icon="raster",
                              actions=[("refresh", "刷新"), ("x", "关闭")])
        props_panel.add_body_widget(self.property_panel)
        right.addWidget(toolbox_panel)
        right.addWidget(props_panel)
        right.setSizes([500, 500])

        self.main_splitter.addWidget(left)
        self.main_splitter.addWidget(center)
        self.main_splitter.addWidget(right)
        self.main_splitter.setStretchFactor(0, 0)
        self.main_splitter.setStretchFactor(1, 1)
        self.main_splitter.setStretchFactor(2, 0)
        self.main_splitter.setSizes([280, 840, 320])

        self.status = RsStatusBar()
        root.addWidget(self.status)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_workspace.py -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add gui/workspace.py tests/test_workspace.py
git commit -m "feat(ui): RsWorkspace 3-column assembly"
```

---

## Task 13: Rewire `MainWindow` + menu bar + toolbar actions + AI toggle

**Files:**
- Modify: `main.py`, `gui/layer_tree.py`

Replace the dock-based `MainWindow` body with `RsWorkspace`; build the styled brand menu bar;
connect toolbar buttons to existing handlers; wire console/status/properties; keep
`AgentDockWidget` as a hidden toggle.

- [ ] **Step 1: Add swatch + meta roles to layer items**

In `gui/layer_tree.py`, modify `LayerTreeItem.__init__` to set delegate roles (uses the role
constants from `gui.rs_widgets`). Add after the existing `setData(...)` lines:

```python
        # RsRowDelegate roles
        from gui.rs_widgets import ROLE_SWATCH, ROLE_META, ROLE_ICON
        palette = {"raster": "#7dd35c", "vector": "#4fb6e6"}
        self.setData(palette.get(layer_type, "#8a92a0"), ROLE_SWATCH)
        self.setData(layer_type, ROLE_META)
        self.setData("raster" if layer_type == "raster" else "vector", ROLE_ICON)
```

- [ ] **Step 2: Modify `main.py` `load_stylesheet`/`main()` to load fonts**

In `main()` (before `load_stylesheet(app)`), add:

```python
    from gui.rs_fonts import load_fonts
    load_fonts()
```

- [ ] **Step 3: Replace `MainWindow.__init__` body and `_create_menus_and_toolbars`**

Replace the section of `MainWindow.__init__` from `# 1. Initialize Central Map Canvas`
through the end of `__init__` with:

```python
        from gui.workspace import RsWorkspace
        self.workspace = RsWorkspace(self)
        self.setCentralWidget(self.workspace)

        # Aliases so existing slots keep working unchanged
        self.canvas = self.workspace.canvas
        self.layer_model = self.workspace.layer_model
        self.layer_view = self.workspace.layer_view
        self.toolbox = self.workspace.toolbox
        self.status = self.workspace.status

        # Map tool
        self.pan_tool = MapToolPan(self.canvas)
        self.canvas.set_map_tool(self.pan_tool)

        # Layer tree signals (unchanged handlers)
        self.layer_model.visibility_changed.connect(self._handle_layer_visibility_changed)
        self.layer_model.layers_reordered.connect(self._handle_layer_reorder)
        self.layer_view.zoom_to_layer_requested.connect(self._zoom_to_layer)
        self.layer_view.remove_layer_requested.connect(self._remove_layer)
        self.layer_view.properties_requested.connect(self._show_layer_properties)
        self.layer_view.selectionModel().selectionChanged.connect(self._on_layer_selected)

        # Toolbox
        self.toolbox.tool_triggered.connect(self._execute_tool)

        # Console: stream real logs
        self.workspace.console.attach_logger("RSStudio")

        # AI Agent dock (hidden by default, toggled by toolbar/menu)
        self.agent_dock = AgentDockWidget(self)
        self.agent_dock.tool_execution_requested.connect(self._execute_tool)
        self.addDockWidget(Qt.RightDockWidgetArea, self.agent_dock)
        self.agent_dock.hide()

        # Status bar coordinate feedback
        self.canvas.coordinates_changed.connect(
            lambda x, y: self.status.set_coord(f"{x:.2f}, {y:.2f}"))
        self.status.set_crs("EPSG:3857 — Web Mercator")

        # Toolbar wiring
        self.workspace.toolbar.triggered.connect(self._on_toolbar)

        self._build_menubar()

        self.loaded_layers = {}
        if sample_path and os.path.exists(sample_path):
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, lambda: self._load_file(sample_path))
```

- [ ] **Step 4: Replace `_create_menus_and_toolbars` with `_build_menubar` + handlers**

Delete the old `_create_menus_and_toolbars` method and `_activate_pan_tool`, and add:

```python
    def _build_menubar(self):
        from PySide6.QtWidgets import QLabel, QWidget, QHBoxLayout
        mb = self.menuBar()
        # Brand corner widget (left)
        brand = QWidget()
        bl = QHBoxLayout(brand); bl.setContentsMargins(8, 0, 4, 0); bl.setSpacing(6)
        logo = QLabel("RS"); logo.setObjectName("rsBrandLogo"); logo.setFixedSize(18, 18)
        name = QLabel("RS Studio"); name.setObjectName("rsBrandName")
        bl.addWidget(logo); bl.addWidget(name)
        mb.setCornerWidget(brand, Qt.TopLeftCorner)

        for label in ["文件", "编辑", "视图", "图层", "处理", "栅格", "矢量",
                      "数据库", "AI 助手", "插件", "窗口", "帮助"]:
            menu = mb.addMenu(label)
            if label == "文件":
                a1 = menu.addAction("添加栅格…"); a1.triggered.connect(self._open_raster_dialog)
                a2 = menu.addAction("添加矢量…"); a2.triggered.connect(self._open_vector_dialog)
                menu.addSeparator()
                a3 = menu.addAction("保存工程")
                a3.triggered.connect(lambda: QMessageBox.information(self, "保存", "工程已保存"))
            elif label == "视图":
                menu.addAction(self.agent_dock.toggleViewAction())
            elif label == "处理":
                menu.addAction("光谱剖面…").triggered.connect(self._show_spectral_profile)
                menu.addAction("ROI 编辑器…").triggered.connect(self._show_roi_editor)
                menu.addAction("模型构建器…").triggered.connect(self._show_model_builder)
                menu.addAction("几何校正…").triggered.connect(self._show_gcp_georef)
                menu.addAction("属性表…").triggered.connect(self._show_attribute_table)

        # Right corner widget
        right = QWidget()
        rl = QHBoxLayout(right); rl.setContentsMargins(0, 0, 8, 0); rl.setSpacing(8)
        ver = QLabel("v0.9.2-dev"); ver.setStyleSheet("color:#8a92a0;font-size:11px;")
        from gui.rs_icons import rs_icon
        for nm in ("bell", "user"):
            ic = QLabel(); ic.setPixmap(rs_icon(nm, 13, "#5b6473").pixmap(13, 13)); rl.addWidget(ic)
        rl.insertWidget(0, ver)
        mb.setCornerWidget(right, Qt.TopRightCorner)

    def _on_toolbar(self, action_id):
        handlers = {
            "open": self._open_raster_dialog,
            "save": lambda: QMessageBox.information(self, "保存", "工程已保存"),
            "pan": lambda: self.canvas.set_map_tool(self.pan_tool),
            "zoomIn": lambda: getattr(self.canvas, "zoomIn", lambda: None)(),
            "zoomOut": lambda: getattr(self.canvas, "zoomOut", lambda: None)(),
            "zoomFit": self._zoom_to_all,
            "classify": self._show_roi_editor,
            "model": self._show_model_builder,
            "process": lambda: None,
            "ai": self._toggle_agent,
        }
        fn = handlers.get(action_id)
        if fn:
            fn()

    def _toggle_agent(self):
        self.agent_dock.setVisible(not self.agent_dock.isVisible())

    def _on_layer_selected(self, *_):
        idx = self.layer_view.currentIndex()
        item = self.layer_model.itemFromIndex(idx) if idx.isValid() else None
        lid = getattr(item, "layer_id", None)
        meta = self.loaded_layers.get(lid) if lid else None
        layer = next((l for l in self.canvas.layers() if getattr(l, "id", None) == lid), None)
        self.workspace.property_panel.set_layer(meta, layer)
```

- [ ] **Step 5: Verify the app imports and constructs headless**

Run:
```bash
QT_QPA_PLATFORM=offscreen PYTHONPATH=. python -c "
import sys
from PySide6.QtWidgets import QApplication
app = QApplication.instance() or QApplication(sys.argv)
import main
w = main.MainWindow()
w.resize(1440, 900); w.show()
app.processEvents()
assert w.workspace.toolbar is not None
assert w.menuBar().actions(), 'menu items present'
print('MainWindow constructs OK; menus:', [a.text() for a in w.menuBar().actions()])
"
```
Expected: prints the 12 menu titles, no exception.

- [ ] **Step 6: Run the existing suite to check for regressions**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest tests/test_main.py -v`
Expected: PASS (update `tests/test_main.py` only if it asserted on the removed
`_create_menus_and_toolbars`/dock structure; adjust those assertions to the new API).

- [ ] **Step 7: Manual visual check (the real verification)**

Run: `python main.py`
Compare against artboard 01 in the prototype. Confirm: brand + 12 menu items; grouped
toolbar with dividers and the active/hover states; left data-browser over layers; center
tabs + map (real render) + console streaming real logs; right toolbox (search + tree) over
properties; segmented status bar; AI button toggles the agent panel. Note any pixel
discrepancies for the Task 15 polish pass.

- [ ] **Step 8: Commit**

```bash
git add main.py gui/layer_tree.py tests/test_main.py
git commit -m "feat(ui): rewire MainWindow to RsWorkspace with prototype chrome"
```

---

## Task 14: Restyle the existing dialogs

**Files:**
- Modify: `gui/prototype_views.py`, `gui/properties_dialog.py` (and/or `gui/qgspropertiesdialog.py` — confirm which is used by `_show_layer_properties`), `gui/toolbox/qgsprocessingtoolbox.py` (the `ToolParameterDialog`)

Apply the toolkit to each dialog so it matches its artboard. This is largely visual; verify
by launching each dialog from the running app.

- [ ] **Step 1: Read each dialog and its matching artboard**

Read `gui/prototype_views.py` and map each dialog to its artboard JS in `scratch_extracted/`:
spectral → `83ca9e7c…`, model builder → `a486d529…`, georef → `04fd1b05…`,
ROI/classify → `2faf7e85…`, attribute table → `ffaf44ec…`, algorithm dialog → `cc686f81…`.

- [ ] **Step 2: Replace ad-hoc headers/tabs with toolkit widgets**

For each dialog, swap hand-rolled title rows for `RsTabBar`/`RsPanel` headers, replace
key/value displays with `prop_row`, replace section captions with `section_header`, and use
`rs_icon` for any icons. Keep all existing signals, slots, and widget object names so
behavior is unchanged. (Detailed per-dialog edits depend on current contents read in Step 1;
make the minimal edits that achieve visual match without changing behavior.)

- [ ] **Step 3: Verify dialogs construct headless**

Run:
```bash
QT_QPA_PLATFORM=offscreen PYTHONPATH=. python -c "
import sys
from PySide6.QtWidgets import QApplication
app = QApplication.instance() or QApplication(sys.argv)
from gui.prototype_views import (SpectralProfileDialog, ModelBuilderDialog,
    GeorefDialog, ROIEditorDialog, AttributeTableDialog)
for D in (SpectralProfileDialog, ModelBuilderDialog, GeorefDialog,
          ROIEditorDialog, AttributeTableDialog):
    d = D(); print('OK', D.__name__)
"
```
Expected: five `OK …` lines, no exception.

- [ ] **Step 4: Manual visual check**

Run `python main.py`, open each dialog from the 处理 menu, compare to its artboard.

- [ ] **Step 5: Commit**

```bash
git add gui/prototype_views.py gui/properties_dialog.py gui/qgspropertiesdialog.py gui/toolbox/qgsprocessingtoolbox.py
git commit -m "feat(ui): restyle dialogs to match prototype artboards"
```

---

## Task 15: Full run-through and polish pass

**Files:** any of the above, as needed.

- [ ] **Step 1: Run the full test suite**

Run: `QT_QPA_PLATFORM=offscreen PYTHONPATH=. pytest -q`
Expected: green (fix any regressions introduced by the rewire).

- [ ] **Step 2: Side-by-side compare with the prototype**

Open `RS Studio (standalone).html` (artboard 01) next to `python main.py`. Walk the spec's
"Design system reference" heights/colors and adjust QSS spacing/padding to remove visible
gaps. Verify fonts render as IBM Plex (Latin) with CJK fallback.

- [ ] **Step 3: Commit polish**

```bash
git add -A
git commit -m "polish(ui): align spacing and colors with prototype"
```

---

## Self-review notes (coverage check)

- Spec §"Fonts" → Task 1. §"icon set" → Task 2. §toolkit widgets (RsPanel/RsTabBar/
  RsToolBar/RsStatusBar/RsConsole/RsRowDelegate/RsPropertyPanel/RsSearchInput) → Tasks 3–9.
  §"Map overlay container" → Task 10. §QSS → Task 11. §"workspace assembly" → Task 12.
  §"Menu bar"/§"Wiring real data"/§AI toggle → Task 13. §"Dialogs" → Task 14. Polish → 15.
- Type consistency: role constants `ROLE_SWATCH/ROLE_META/ROLE_ICON` defined in Task 7,
  consumed in Tasks 12–13; `RsWorkspace` attribute names match the wiring in Task 13;
  `attach_logger`/`append_log` defined in Task 8 and called in Task 13.
- Known follow-ups left intentionally light (visual polish, per-dialog edits) are flagged as
  manual verification steps because pixel-fidelity can't be unit-asserted.
