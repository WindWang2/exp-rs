# gui/rs_widgets.py
import logging
from PySide6.QtWidgets import (QFrame, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QToolButton, QSizePolicy, QStyledItemDelegate,
                               QLineEdit, QStyle, QPlainTextEdit)
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtCore import Qt, Signal, QSize, QRect
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


# append to gui/rs_widgets.py
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
        gl.setSpacing(2)
        ids = []
        for it in items:
            b = QToolButton()
            b.setObjectName("rsToolBtn")
            b.setIcon(rs_icon(it["icon"], 14, _T1))
            if it.get("label"):
                b.setText(it["label"])
                b.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
                # Add minimum width for buttons with text labels to prevent overlap
                b.setMinimumWidth(60)
            else:
                # Icon-only buttons can be smaller
                b.setMinimumWidth(28)
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


# append to gui/rs_widgets.py
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


# append to gui/rs_widgets.py
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
                f"<span style='color:{color}'>{level}</span> "
                f"<span style='color:#8a92a0'>{module}</span> "
                f"<span style='color:#2f3640'>{msg}</span>")
        self.view.appendHtml(html)
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


# append to gui/rs_widgets.py
from PySide6.QtWidgets import QMenuBar, QMenu


class RsMenuBar(QMenuBar):
    """Custom menu bar that reliably combines brand, menus, and right widgets.

    Uses eventFilter and custom paint to add brand/logo and right widgets.
    Compatible with QMainWindow.setMenuBar().
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("rsMenuBar")
        self.setNativeMenuBar(False)

        # Create corner widgets directly
        self._brand_widget = self._create_brand_widget()
        self._right_widget = self._create_right_widget()

        # Note: We rely on QSS/styling for placement since setCornerWidget is unreliable
        # The brand and right widgets are added as children and positioned via stylesheet

    def _create_brand_widget(self):
        """Create the brand widget (RS logo + name)."""
        brand = QWidget(self)
        brand.setObjectName("rsMenuBarBrand")
        bl = QHBoxLayout(brand); bl.setContentsMargins(8, 0, 4, 0); bl.setSpacing(6)
        logo = QLabel("RS"); logo.setObjectName("rsBrandLogo"); logo.setFixedSize(18, 18)
        name = QLabel("RS Studio"); name.setObjectName("rsBrandName")
        bl.addWidget(logo); bl.addWidget(name)
        brand.move(0, 0)
        brand.show()
        return brand

    def _create_right_widget(self):
        """Create the right widget (version + icons)."""
        right = QWidget(self)
        right.setObjectName("rsMenuBarRight")
        rl = QHBoxLayout(right); rl.setContentsMargins(0, 0, 8, 0); rl.setSpacing(8)
        self._version_label = QLabel("v0.9.2-dev")
        self._version_label.setStyleSheet("color:#8a92a0;font-size:11px;")
        rl.addWidget(self._version_label)
        right.move(0, 0)  # Will be positioned in resizeEvent
        right.show()
        return right

    def add_version_icon(self, icon_name, size=13, color="#5b6473"):
        """Add an icon to the right side of the menu bar."""
        lay = self._right_widget.layout()
        ic = QLabel()
        ic.setPixmap(rs_icon(icon_name, size, color).pixmap(size, size))
        lay.addWidget(ic)
        return ic

    def set_version(self, text):
        """Update the version label."""
        if hasattr(self, '_version_label'):
            self._version_label.setText(text)

    def resizeEvent(self, event):
        """Position corner widgets on resize."""
        super().resizeEvent(event)
        # Position brand widget at left
        if hasattr(self, '_brand_widget'):
            self._brand_widget.move(8, (self.height() - self._brand_widget.height()) // 2)
            self._brand_widget.resize(self._brand_widget.sizeHint())
        # Position right widget at right edge
        if hasattr(self, '_right_widget'):
            rw = self._right_widget.sizeHint().width()
            self._right_widget.move(self.width() - rw - 8, (self.height() - self._right_widget.height()) // 2)
            self._right_widget.resize(rw, self.height())

    def showEvent(self, event):
        """Ensure corner widgets are shown when menu bar is shown."""
        super().showEvent(event)
        if hasattr(self, '_brand_widget'):
            self._brand_widget.show()
        if hasattr(self, '_right_widget'):
            self._right_widget.show()
