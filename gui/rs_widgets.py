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
