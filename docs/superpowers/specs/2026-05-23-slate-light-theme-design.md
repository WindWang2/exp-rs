# Design Spec: Slate Light Theme (Antigravity RS)

**Status:** APPROVED
**Date:** 2026-05-23
**Topic:** GUI Polish & Visual Theme

## 1. Overview
Implement a cohesive "Slate Light" visual theme for the Antigravity RS platform. This theme focuses on a professional, high-performance GIS aesthetic using a Slate palette with Lavender accents.

## 2. Palette & Typography

### Palette
| Role | Color Code | Description |
|------|------------|-------------|
| Primary Background | `#F8F9FB` | Main application background (Slate Cloud) |
| Lavender Accent | `#7E57C2` | Active states, primary buttons, highlights |
| Surface (White) | `#FFFFFF` | Canvas and Tree backgrounds |
| Border/Divider | `#E0E4E8` | Subtle separators (Slate) |
| Text (Primary) | `#2D3436` | High-contrast body text (Deep Slate) |
| Text (Secondary) | `#636E72` | Muted labels and placeholders |

### Typography
- **Primary Font:** 'Inter', 'Segoe UI', or system sans-serif.
- **Base Size:** 13px.
- **Dock Titles:** Semi-bold, Lavender (`#7E57C2`).

## 3. Component Styling (QSS)

### 3.1. Main Window & Docks
- **QMainWindow:** Background set to `#F8F9FB`.
- **QDockWidget:** 
    - Title bar text color: `#7E57C2`.
    - Border: 1px solid `#E0E4E8`.
- **QStatusBar:** Subtle top border `#E0E4E8`, text color `#636E72`.

### 3.2. MapCanvas
- **Background:** `#FFFFFF`.
- **Focus Border:** 1px solid `#7E57C2`.
- **Vector Rendering:** All paths must use `QPen.setCosmetic(True)`.

### 3.3. Layer Tree & Toolbox
- **QTreeView/QTreeWidget:**
    - Selection background: `#F0E7FF` (Light Lavender).
    - Selection text: `#7E57C2`.
    - No alternating row colors.
- **QCheckBox:** Custom styling with Lavender checkmarks.

### 3.4. Forms & Buttons
- **QPushButton (Primary):** Background `#7E57C2`, text `#FFFFFF`.
- **QLineEdit / QSpinBox:** 1px border `#E0E4E8`, focused border `#7E57C2`.
- **Border Radius:** 4px globally for all widgets.

## 4. Implementation Strategy
- Use a global `resources/styles.qss` file.
- Load the QSS in `main.py` and apply to the `QApplication` instance.
- Use object names or specific class selectors for targeted styling.

## 5. Success Criteria
- The application launches with a consistent Slate/Lavender appearance.
- No "Standard" Windows/Linux default widget styling remains visible.
- High-DPI icons (system fallback) scale correctly.
