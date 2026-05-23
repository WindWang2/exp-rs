# Slate Light Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a cohesive Slate Light theme with Lavender accents using a global QSS stylesheet.

**Architecture:** Centralized QSS styling loaded at the application level. Component-level tweaks for high-DPI and custom rendering.

**Tech Stack:** PySide6, QSS (Qt Style Sheets).

---

### Task 1: Initialize Global Stylesheet

**Files:**
- Create: `resources/styles.qss`
- Modify: `main.py:1-50`

- [x] **Step 1: Create the QSS file with base palette**
- [x] **Step 2: Update main.py to load and apply the stylesheet**
- [x] **Step 3: Run app to verify base colors**
- [x] **Step 4: Commit** (Note: Repository is not a git repo, skipping git commands)

---

### Task 2: Style MapCanvas and Core Widgets

**Files:**
- Modify: `resources/styles.qss`
- Modify: `gui/canvas.py`

- [ ] **Step 1: Add MapCanvas and Input styling to QSS**
```css
/* Add to resources/styles.qss */
QGraphicsView {
    background-color: #FFFFFF;
    border: 1px solid #E0E4E8;
    border-radius: 4px;
}

QGraphicsView:focus {
    border: 1px solid #7E57C2;
}

QPushButton {
    background-color: #E0E4E8;
    border: 1px solid #D1D5DA;
    border-radius: 4px;
    padding: 5px 12px;
}

QPushButton:hover {
    background-color: #D1D5DA;
}

QPushButton[primary="true"] {
    background-color: #7E57C2;
    color: #FFFFFF;
    border: none;
}

QPushButton[primary="true"]:hover {
    background-color: #6A49AD;
}

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #FFFFFF;
    border: 1px solid #E0E4E8;
    border-radius: 4px;
    padding: 4px;
}

QLineEdit:focus, QSpinBox:focus {
    border: 1px solid #7E57C2;
}
```

- [ ] **Step 2: Remove inline styling from MapCanvas**
Remove `self.setStyleSheet(...)` in `gui/canvas.py`.

- [ ] **Step 3: Run app to verify canvas and button styling**
Run: `python main.py`
Expected: Canvas has subtle border, buttons look modern.

- [ ] **Step 4: Commit**
```bash
git add resources/styles.qss gui/canvas.py
git commit -m "style: polish canvas and input widgets"
```

---

### Task 3: Style Layer Tree and Toolbox

**Files:**
- Modify: `resources/styles.qss`

- [ ] **Step 1: Add Tree and Checklist styling to QSS**
```css
/* Add to resources/styles.qss */
QTreeView, QTreeWidget {
    background-color: #FFFFFF;
    border: 1px solid #E0E4E8;
    show-decoration-selected: 1;
    outline: 0;
}

QTreeView::item:selected {
    background-color: #F0E7FF;
    color: #7E57C2;
}

QTreeView::item:hover {
    background-color: #F8F9FB;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #E0E4E8;
    border-radius: 3px;
    background-color: #FFFFFF;
}

QCheckBox::indicator:checked {
    background-color: #7E57C2;
    border: 1px solid #7E57C2;
    image: url(resources/check.png); /* We may need a small checkmark icon or use a character */
}
```

- [ ] **Step 2: Run app to verify layer tree styling**
Run: `python main.py`
Expected: Layer tree selections are Lavender.

- [ ] **Step 3: Commit**
```bash
git add resources/styles.qss
git commit -m "style: polish layer tree and selection states"
```

---

### Task 4: High-DPI and Final UX Polish

**Files:**
- Modify: `main.py`
- Modify: `gui/canvas.py`

- [ ] **Step 1: Enable High-DPI scaling in main.py**
```python
# Before creating QApplication
QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
```

- [ ] **Step 2: Ensure Cosmetic Pen in Canvas**
Verify `QPen.setCosmetic(True)` is used for all vector additions in `gui/canvas.py`.

- [ ] **Step 3: Final verification run**
Run: `python main.py`
Expected: Everything looks sharp and follows the Slate/Lavender theme.

- [ ] **Step 4: Commit**
```bash
git add main.py gui/canvas.py
git commit -m "style: final high-dpi and vector sharpness polish"
```
