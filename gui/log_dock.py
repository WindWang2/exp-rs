import os
import logging
from PySide6.QtWidgets import (QDockWidget, QWidget, QVBoxLayout, QHBoxLayout, 
                               QTextEdit, QPushButton, QCheckBox, QLabel, 
                               QFileDialog, QToolBar, QSizePolicy)
from PySide6.QtCore import Qt, QObject, Signal, Slot
from PySide6.QtGui import QFont, QTextCursor, QColor, QAction

# Setup unique color constants for light/dark blending
COLOR_DEBUG = "#828282"
COLOR_INFO = "#0969da"      # Sleek Slate Blue
COLOR_WARNING = "#bf8700"   # Warm Gold
COLOR_ERROR = "#cf222e"     # Deep Crimson
COLOR_SUCCESS = "#1a7f37"   # Vibrant Green
COLOR_SYSTEM = "#8250df"    # Premium Purple for internal systems

class QLogHandlerSignals(QObject):
    log_emitted = Signal(str, int, str, str) # raw_msg, levelno, formatted_msg, time_str

class QLogHandler(logging.Handler):
    """
    Thread-safe PySide6 Logging Handler.
    Emits a Qt Signal when a log record is processed, guaranteeing
    safe gui updates even when logs originate from deep background worker threads.
    """
    def __init__(self):
        super().__init__()
        self.signals = QLogHandlerSignals()
        # Custom short time format for terminal window readability
        self.setFormatter(logging.Formatter('%(asctime)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s', '%H:%M:%S'))

    def emit(self, record):
        try:
            msg = self.format(record)
            time_str = self.formatter.formatTime(record)
            # Emit raw message, level number, full formatted message, and local time string
            self.signals.log_emitted.emit(record.getMessage(), record.levelno, msg, time_str)
        except Exception:
            self.handleError(record)

class LogDockWidget(QDockWidget):
    """
    Premium Log Console dock panel for Antigravity RS.
    Matches Slate Light dashboard aesthetics while embedding a high-fidelity output terminal.
    """
    def __init__(self, parent=None):
        super().__init__("LOG MESSAGES (运行日志)", parent)
        self.setObjectName("LogMessagesDock")
        self.setAllowedAreas(Qt.BottomDockWidgetArea | Qt.LeftDockWidgetArea | Qt.RightDockWidgetArea)
        
        # Central container
        self.container = QWidget()
        self.layout = QVBoxLayout(self.container)
        self.layout.setContentsMargins(6, 6, 6, 6)
        self.layout.setSpacing(4)
        
        # 1. Custom Console Toolbar
        self.toolbar_layout = QHBoxLayout()
        self.toolbar_layout.setContentsMargins(4, 2, 4, 2)
        self.toolbar_layout.setSpacing(12)
        
        # Level Filters
        self.cb_info = QCheckBox("Info")
        self.cb_info.setChecked(True)
        self.cb_info.setStyleSheet("font-weight: bold; color: #0969da;")
        
        self.cb_warn = QCheckBox("Warnings")
        self.cb_warn.setChecked(True)
        self.cb_warn.setStyleSheet("font-weight: bold; color: #bf8700;")
        
        self.cb_error = QCheckBox("Errors")
        self.cb_error.setChecked(True)
        self.cb_error.setStyleSheet("font-weight: bold; color: #cf222e;")
        
        self.cb_debug = QCheckBox("Debug")
        self.cb_debug.setChecked(False)
        self.cb_debug.setStyleSheet("font-weight: bold; color: #6e7781;")
        
        self.toolbar_layout.addWidget(self.cb_info)
        self.toolbar_layout.addWidget(self.cb_warn)
        self.toolbar_layout.addWidget(self.cb_error)
        self.toolbar_layout.addWidget(self.cb_debug)
        
        self.toolbar_layout.addStretch()
        
        # Autoscroll Toggle
        self.cb_autoscroll = QCheckBox("Auto-Scroll")
        self.cb_autoscroll.setChecked(True)
        self.cb_autoscroll.setStyleSheet("color: #57606a;")
        self.toolbar_layout.addWidget(self.cb_autoscroll)
        
        # Action Buttons
        self.btn_clear = QPushButton("🗑️ Clear")
        self.btn_clear.setToolTip("Clear all console records")
        self.btn_clear.setStyleSheet("""
            QPushButton { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 3px; padding: 2px 8px; font-size: 11px; }
            QPushButton:hover { background-color: #f3f4f6; }
        """)
        self.btn_clear.clicked.connect(self.clear_logs)
        self.toolbar_layout.addWidget(self.btn_clear)
        
        self.btn_export = QPushButton("💾 Export...")
        self.btn_export.setToolTip("Export logs to external txt file")
        self.btn_export.setStyleSheet("""
            QPushButton { background-color: #f6f8fa; border: 1px solid #d0d7de; border-radius: 3px; padding: 2px 8px; font-size: 11px; }
            QPushButton:hover { background-color: #f3f4f6; }
        """)
        self.btn_export.clicked.connect(self.export_logs)
        self.toolbar_layout.addWidget(self.btn_export)
        
        self.layout.addLayout(self.toolbar_layout)
        
        # 2. Rich Text Terminal Output
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        # Apply premium dark terminal theme for output contrast
        self.text_edit.setStyleSheet("""
            QTextEdit {
                background-color: #0F141C;
                border: 1px solid #d4d8de;
                border-radius: 4px;
                color: #e0e6ed;
                padding: 6px;
            }
        """)
        # Set modern monospace typography
        font = QFont("Consolas", 10)
        font.setStyleHint(QFont.Monospace)
        self.text_edit.setFont(font)
        self.layout.addWidget(self.text_edit)
        
        self.setWidget(self.container)
        
        # Internal cache of log records to allow real-time level filtering updates
        self.log_records = []
        
        # Connect filter toggles
        self.cb_info.toggled.connect(self.refresh_display)
        self.cb_warn.toggled.connect(self.refresh_display)
        self.cb_error.toggled.connect(self.refresh_display)
        self.cb_debug.toggled.connect(self.refresh_display)
        
        # Setup thread-safe log interceptor
        self.log_handler = QLogHandler()
        self.log_handler.signals.log_emitted.connect(self.add_log_record)
        
        # Register in standard logging system
        logger = logging.getLogger("RSStudio")
        logger.addHandler(self.log_handler)
        
        # Visual splash initial log
        logger.info("Antigravity RS Logger initialized. Operation logs are now captured in real-time.")

    @Slot(str, int, str, str)
    def add_log_record(self, raw_msg: str, levelno: int, formatted_msg: str, time_str: str):
        """Processes and caches a incoming thread-safe log event."""
        # Detect success logs dynamically
        is_success = "success" in raw_msg.lower() or "completed" in raw_msg.lower()
        
        record = {
            "raw": raw_msg,
            "level": levelno,
            "formatted": formatted_msg,
            "time": time_str,
            "success": is_success
        }
        self.log_records.append(record)
        
        # Render if it satisfies current filtering checkboxes
        if self._should_display(record):
            self._append_to_screen(record)

    def _should_display(self, record) -> bool:
        level = record["level"]
        if level >= logging.ERROR:
            return self.cb_error.isChecked()
        elif level >= logging.WARNING:
            return self.cb_warn.isChecked()
        elif level >= logging.INFO:
            return self.cb_info.isChecked()
        else: # DEBUG
            return self.cb_debug.isChecked()

    def _append_to_screen(self, record):
        """Safely appends a log record formatted with premium styling HTML."""
        level = record["level"]
        raw = record["raw"]
        time_str = record["time"]
        
        # Extract filename and code location if present
        # Format: time - LEVEL - [file:line] - msg
        parts = record["formatted"].split(" - ")
        location = ""
        if len(parts) >= 3:
            location = parts[2]
            
        color = COLOR_INFO
        level_label = "INFO"
        
        if record["success"]:
            color = COLOR_SUCCESS
            level_label = "SUCCESS"
        elif level >= logging.ERROR:
            color = COLOR_ERROR
            level_label = "ERROR"
        elif level >= logging.WARNING:
            color = COLOR_WARNING
            level_label = "WARNING"
        elif level < logging.INFO:
            color = COLOR_DEBUG
            level_label = "DEBUG"
            
        # Parse internal or agent tasks to purple
        if "agent" in raw.lower() or "toolbox" in raw.lower():
            if level_label == "INFO":
                color = COLOR_SYSTEM
                level_label = "SYSTEM"

        html = f"<span style='color: #828282;'>[{time_str}]</span> "
        html += f"<span style='color: {color}; font-weight: bold;'>[{level_label}]</span> "
        if location:
            html += f"<span style='color: #5b6473; font-style: italic;'>{location}</span> "
        html += f"<span style='color: #e0e6ed;'>{raw}</span>"
        
        self.text_edit.append(html)
        
        if self.cb_autoscroll.isChecked():
            self.text_edit.moveCursor(QTextCursor.End)

    @Slot()
    def refresh_display(self):
        """Clears screens and completely regenerates rendering from record cache matching updated filters."""
        self.text_edit.clear()
        for rec in self.log_records:
            if self._should_display(rec):
                self._append_to_screen(rec)

    @Slot()
    def clear_logs(self):
        """Flushes cached records and clears display."""
        self.log_records.clear()
        self.text_edit.clear()
        logging.getLogger("RSStudio").info("Log Console cleared by user.")

    @Slot()
    def export_logs(self):
        """Saves current logs cache to a custom external text file."""
        file_path, _ = QFileDialog.getSaveFileName(self, "Export Log History", "rs_studio_export.txt", "Text Files (*.txt);;All Files (*)")
        if file_path:
            try:
                with open(file_path, "w", encoding="utf-8") as f:
                    for rec in self.log_records:
                        f.write(rec["formatted"] + "\n")
                logging.getLogger("RSStudio").info(f"Log history exported to '{file_path}' successfully.")
            except Exception as e:
                logging.getLogger("RSStudio").error(f"Failed to export log history: {e}")
