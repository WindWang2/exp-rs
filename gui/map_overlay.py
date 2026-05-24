# gui/map_overlay.py
from PySide6.QtWidgets import QWidget, QLabel, QHBoxLayout, QToolButton, QButtonGroup
from PySide6.QtCore import Qt
from gui.canvas import MapCanvas


class MapOverlayContainer(QWidget):
    """Holds a MapCanvas with prototype overlay chrome positioned on top."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("mapOverlayContainer")
        self._in_resize = False  # Guard against recursive resize loops
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
        if self._in_resize:
            return
        self._in_resize = True
        try:
            self.canvas.setGeometry(0, 0, self.width(), self.height())
            self._reposition()
            super().resizeEvent(event)
        finally:
            self._in_resize = False

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
