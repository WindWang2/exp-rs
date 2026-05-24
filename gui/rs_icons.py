# gui/rs_icons.py
from PySide6.QtCore import Qt, QByteArray, QSize
from PySide6.QtGui import QPixmap, QIcon, QPainter
from PySide6.QtSvg import QSvgRenderer

# --- ICON_PATHS: ported verbatim from prototype icons.jsx -------------------
ICON_PATHS = {
    # file / data
    "folder":       "M2 5a1 1 0 0 1 1-1h3.5l1.5 1.5H13a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V5z",
    "folderOpen":   "M2 5a1 1 0 0 1 1-1h3.5l1.5 1.5H13a1 1 0 0 1 1 1V7M2 5v7a1 1 0 0 0 1 1h9l2-6H4l-2 4",
    "file":         "M9 2H4a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1V6L9 2zM9 2v4h4",
    "raster":       "M2 3h12v10H2zM2 7h12M2 11h12M6 3v10M10 3v10",
    "vector":       "M3 4l5 3 5-3M3 12l5-3 5 3M8 7v2",
    "point":        "M8 4l4 8H4z",
    "database":     "M2 4c0-1 2.7-2 6-2s6 1 6 2-2.7 2-6 2-6-1-6-2zM2 4v8c0 1 2.7 2 6 2s6-1 6-2V4M2 8c0 1 2.7 2 6 2s6-1 6-2",
    "satellite":    "M3 8l5-5 5 5-5 5zM6 5l2 2M10 9l2 2M8 8l3-3",
    "hyperspectral":"M2 3h12v3H2zM2 6h12v3H2zM2 9h12v4H2",

    # layers & view
    "layers":       "M8 2L2 5l6 3 6-3zM2 8l6 3 6-3M2 11l6 3 6-3",
    "eye":          "M1 8s2.5-4.5 7-4.5S15 8 15 8s-2.5 4.5-7 4.5S1 8 1 8zM8 10a2 2 0 1 0 0-4 2 2 0 0 0 0 4z",
    "eyeOff":       "M1 8s2.5-4.5 7-4.5c1.3 0 2.4.4 3.4 1M15 8s-2.5 4.5-7 4.5c-1.3 0-2.5-.4-3.5-1M2 2l12 12",
    "lock":         "M4 7V5a4 4 0 1 1 8 0v2M3 7h10v7H3z",

    # tools
    "cursor":       "M3 3l4 10 2-4 4-2z",
    "pan":          "M8 2v6m0 0V6m0 2l-2-2m2 2l2-2M8 8v6m0 0v-2m0 2l-2-2m2 2l2-2M2 8h12",
    "zoomIn":       "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14M5 7h4M7 5v4",
    "zoomOut":      "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14M5 7h4",
    "zoomFit":      "M2 5V2h3M11 2h3v3M14 11v3h-3M5 14H2v-3M6 6h4v4H6z",
    "measure":      "M2 10l8-8 4 4-8 8zM5 5l1 1M7 3l1 1M3 7l1 1",
    "identify":     "M8 4v.01M8 7v5M3 8a5 5 0 1 0 10 0 5 5 0 0 0-10 0z",
    "crosshair":    "M8 2v3M8 11v3M2 8h3M11 8h3M8 8m-3 0a3 3 0 1 0 6 0 3 3 0 0 0-6 0z",

    # process / algorithm
    "cog":          "M8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM8 2v1.5M8 12.5V14M2 8h1.5M12.5 8H14M3.8 3.8l1 1M11.2 11.2l1 1M3.8 12.2l1-1M11.2 4.8l1-1",
    "wand":         "M3 13L13 3M11 2v2M14 5h-2M5 7l-2 2 1 1 2-2M9 3l1-1 1 1-1 1z",
    "workflow":     "M3 4a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM11 4a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM3 12a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM11 12a1 1 0 1 0 2 0 1 1 0 0 0-2 0zM5 4h6M5 12h6M4 5v6M12 5v6",
    "play":         "M4 3l9 5-9 5z",
    "stop":         "M4 4h8v8H4z",
    "pause":        "M5 3v10M11 3v10",

    # editing
    "plus":         "M8 3v10M3 8h10",
    "minus":        "M3 8h10",
    "x":            "M3 3l10 10M13 3L3 13",
    "check":        "M3 8l3 3 7-7",
    "chevR":        "M6 3l5 5-5 5",
    "chevD":        "M3 6l5 5 5-5",
    "chevU":        "M3 10l5-5 5 5",
    "chevL":        "M10 3L5 8l5 5",
    "more":         "M3 8h.01M8 8h.01M13 8h.01",
    "moreV":        "M8 3v.01M8 8v.01M8 13v.01",
    "dots":         "M3 8h.01M8 8h.01M13 8h.01",
    "search":       "M7 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zM10.5 10.5L14 14",
    "filter":       "M2 3h12l-4 5v5l-4-2V8z",
    "refresh":      "M3 8a5 5 0 0 1 9-3l1 1M13 8a5 5 0 0 1-9 3l-1-1M11 3v3h3M5 13v-3H2",
    "save":         "M3 3h8l2 2v8H3zM5 3v4h5V3M5 9h6v4H5",
    "upload":       "M8 11V3M5 6l3-3 3 3M3 11v2h10v-2",
    "download":     "M8 3v8M5 8l3 3 3-3M3 13v-2h10v2",

    # ai
    "spark":        "M8 2v3M8 11v3M2 8h3M11 8h3M5 5l2 2M9 9l2 2M5 11l2-2M9 7l2-2",
    "brain":        "M5 4a3 3 0 0 0-2 5 3 3 0 0 0 2 3v1h6v-1a3 3 0 0 0 2-3 3 3 0 0 0-2-5 3 3 0 0 0-3-1 3 3 0 0 0-3 1zM8 3v9",
    "agent":        "M4 5a2 2 0 1 1 4 0v3a2 2 0 1 1-4 0zM8 5a2 2 0 1 1 4 0v3a2 2 0 1 1-4 0zM6 11v2M10 11v2M5 13h6",
    "send":         "M2 8L14 3l-3 11-3-5z",

    # misc
    "globe":        "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM2 8h12M8 2c2 2 3 4 3 6s-1 4-3 6c-2-2-3-4-3-6s1-4 3-6z",
    "pin":          "M8 2a4 4 0 0 0-4 4c0 3 4 8 4 8s4-5 4-8a4 4 0 0 0-4-4zM8 7a1 1 0 1 0 0-2 1 1 0 0 0 0 2z",
    "bookmark":     "M4 2h8v12l-4-3-4 3z",
    "user":         "M8 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM3 14a5 5 0 0 1 10 0",
    "bell":         "M4 11V7a4 4 0 1 1 8 0v4l1 2H3zM6 13a2 2 0 0 0 4 0",
    "help":         "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12zM6 6a2 2 0 1 1 2 2v1M8 11v.01",
    "link":         "M7 9l2-2M5 11a2 2 0 0 1 0-3l2-2M11 5a2 2 0 0 1 0 3l-2 2",
    "grid":         "M2 2h5v5H2zM9 2h5v5H9zM2 9h5v5H2zM9 9h5v5H9z",
    "chart":        "M2 13h12M4 11V7M7 11V4M10 11V8M13 11V5",
    "histogram":    "M2 13h12M3 13V8M5 13V5M7 13V9M9 13V6M11 13V10M13 13V7",
    "spectrum":     "M2 11l2-3 2 1 2-4 2 2 2-1 2 2v4H2z",
    "palette":      "M8 14A6 6 0 1 0 8 2a6 6 0 0 0 0 12c0-1 1-1 1-2s-1-1-1-2 1-1 1-2-1-1-1-2zM5 6a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM11 6a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM4 9a1 1 0 1 0 0-2 1 1 0 0 0 0 2zM12 9a1 1 0 1 0 0-2 1 1 0 0 0 0 2z",
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
    """Return a QIcon for the named icon, recolored to `color`."""
    return QIcon(rs_pixmap(name, size, color))
