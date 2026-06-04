# Preset Coordinate Reference Systems Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add commonly used CRS presets for remote sensing workflows, making it easy for users to select the correct CRS for their data.

**Architecture:** Create a CRS preset system that:
1. Defines commonly used CRS presets
2. Provides a selection dialog
3. Integrates with the main window menu/toolbar
4. Integrates with layer properties dialog

**Tech Stack:** Qt6, QGIS CRS API, C++17

---

## Common CRS Presets for Remote Sensing

### Global CRS
- **WGS84** (EPSG:4326) — World Geodetic System 1984, used by GPS
- **Web Mercator** (EPSG:3857) — Web mapping standard

### UTM Zones (Universal Transverse Mercator)
- **UTM North** (EPSG:32601-32660) — Zones 1N-60N
- **UTM South** (EPSG:32701-32760) — Zones 1S-60S
- Common zones for China: UTM Zone 49N (EPSG:32649), Zone 50N (EPSG:32650), Zone 51N (EPSG:32651)

### China Specific CRS
- **CGCS2000** (EPSG:4547-4555) — China Geodetic Coordinate System 2000
  - EPSG:4547 — CGCS2000 / 3-degree Gauss-Kruger zone 25
  - EPSG:4548 — CGCS2000 / 3-degree Gauss-Kruger zone 26
  - EPSG:4549 — CGCS2000 / 3-degree Gauss-Kruger zone 27
  - EPSG:4550 — CGCS2000 / 3-degree Gauss-Kruger zone 28
  - EPSG:4551 — CGCS2000 / 3-degree Gauss-Kruger zone 29
  - EPSG:4552 — CGCS2000 / 3-degree Gauss-Kruger zone 30
  - EPSG:4553 — CGCS2000 / 3-degree Gauss-Kruger zone 31
  - EPSG:4554 — CGCS2000 / 3-degree Gauss-Kruger zone 32
  - EPSG:4555 — CGCS2000 / 3-degree Gauss-Kruger zone 33
- **Beijing 1954** (EPSG:21413-21483) — Legacy Chinese CRS
  - EPSG:21413 — Beijing 1954 / Gauss-Kruger zone 13
  - EPSG:21414 — Beijing 1954 / Gauss-Kruger zone 14
  - ... (zones 13-23)
  - EPSG:21483 — Beijing 1954 / Gauss-Kruger zone 13 (3-degree)
- **Xian 1980** (EPSG:2326-2349) — Legacy Chinese CRS
  - EPSG:2326 — Xian 1980 / Gauss-Kruger zone 13
  - EPSG:2327 — Xian 1980 / Gauss-Kruger zone 14
  - ... (zones 13-23)
  - EPSG:2349 — Xian 1980 / Gauss-Kruger zone 13 (3-degree)

### Common Regional CRS
- **NAD83** (EPSG:4269) — North American Datum 1983
- **ETRS89** (EPSG:4258) — European Terrestrial Reference System 1989
- **GDA2020** (EPSG:7844) — Geocentric Datum of Australia 2020

---

## Task Plan

### Task 5B.14.1: Create CRS Preset Definitions

**Goal:** Define CRS presets in a structured format.

**Files:**
- Create: `src/app/crs_presets.h` — CRS preset structure and definitions
- Create: `src/app/crs_presets.cpp` — CRS preset implementation

**Steps:**
- [ ] Define CrsPreset structure (name, EPSG code, description, category)
- [ ] Create preset categories: Global, UTM, China, Regional
- [ ] Define all CRS presets
- [ ] Add helper functions: presetForEpsg(), presetsByCategory()
- [ ] Build and verify

---

### Task 5B.14.2: Create CRS Preset Selection Dialog

**Goal:** Create a dialog for selecting CRS presets.

**Files:**
- Create: `src/app/dialogs/crs_preset_dialog.h/.cpp` — CRS preset dialog

**Steps:**
- [ ] Create dialog with tree view (categories) and list view (presets)
- [ ] Add search/filter functionality
- [ ] Show CRS details (name, EPSG, WKT, proj4)
- [ ] Add "Recently Used" category
- [ ] Add "Favorites" category
- [ ] Build and verify

---

### Task 5B.14.3: Integrate CRS Presets into Main Window

**Goal:** Add CRS preset menu and toolbar to main window.

**Files:**
- Modify: `src/app/main_window.h` — add CRS preset slot
- Modify: `src/app/main_window.cpp` — add menu/toolbar items

**Steps:**
- [ ] Add "CRS Presets" menu to Settings menu
- [ ] Add CRS preset toolbar buttons
- [ ] Add slot function to open CRS preset dialog
- [ ] Connect to project CRS change
- [ ] Build and verify

---

### Task 5B.14.4: Integrate CRS Presets into Layer Properties

**Goal:** Add CRS preset selection to layer properties dialog.

**Files:**
- Modify: Layer properties dialog — add CRS preset button

**Steps:**
- [ ] Add "Select from Presets" button to CRS selection
- [ ] Open CRS preset dialog
- [ ] Apply selected CRS to layer
- [ ] Build and verify

---

### Task 5B.14.5: Add Recently Used CRS

**Goal:** Track and display recently used CRS.

**Files:**
- Modify: `src/app/crs_presets.h/.cpp` — add recent CRS tracking

**Steps:**
- [ ] Store recently used CRS in QSettings
- [ ] Add "Recently Used" category to dialog
- [ ] Limit to 10 recent CRS
- [ ] Build and verify

---

## Priority Order

1. **Task 5B.14.1** — Create CRS Preset Definitions
2. **Task 5B.14.2** — Create CRS Preset Selection Dialog
3. **Task 5B.14.3** — Integrate CRS Presets into Main Window
4. **Task 5B.14.4** — Integrate CRS Presets into Layer Properties
5. **Task 5B.14.5** — Add Recently Used CRS

---

## Notes

- CRS presets should be easy to extend
- Support both EPSG codes and custom CRS definitions
- Consider adding search by name or EPSG code
- Recently used CRS improves workflow efficiency
- Favorites allow users to mark frequently used CRS

---

*Last updated: 2026-06-01*