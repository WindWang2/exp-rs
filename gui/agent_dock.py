from PySide6.QtWidgets import (QDockWidget, QWidget, QVBoxLayout, QHBoxLayout, 
                               QTextEdit, QLineEdit, QPushButton, QScrollArea, 
                               QLabel, QFrame, QSizePolicy)
from PySide6.QtCore import Qt, Signal, Slot, QThread
from PySide6.QtGui import QFont, QColor
from agent.executor import AgentExecutor
import os

class AgentWorker(QThread):
    """Background worker thread preventing main GUI lockups during API queries."""
    response_received = Signal(dict)
    
    def __init__(self, executor: AgentExecutor, prompt: str):
        super().__init__()
        self.executor = executor
        self.prompt = prompt
        
    def run(self):
        try:
            res = self.executor.execute_chat(self.prompt)
            self.response_received.emit(res)
        except Exception as e:
            self.response_received.emit({
                "thought": f"API request error: {e}. Running local fallbacks.",
                "tool_call": None,
                "code": "# Failed to fetch response"
            })

class MessageBubble(QFrame):
    """Rounded message bubbles following our modern design system."""
    def __init__(self, text: str, is_user: bool, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        
        lbl = QLabel(text)
        lbl.setWordWrap(True)
        lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
        
        if is_user:
            # Clean grey bubble aligned right for user
            self.setStyleSheet("""
                QFrame {
                    background-color: #eef1f5;
                    border: 1px solid #e2e6ec;
                    border-radius: 8px;
                    margin-left: 50px;
                }
            """)
            lbl.setStyleSheet("color: #2f3640; font-family: 'Segoe UI', 'Inter'; font-size: 12px;")
            layout.addWidget(lbl)
        else:
            # Crisp white bubble with AI-purple border aligned left for agent
            self.setStyleSheet("""
                QFrame {
                    background-color: #ffffff;
                    border: 1.5px solid #8250df;
                    border-radius: 8px;
                    margin-right: 50px;
                }
            """)
            lbl.setStyleSheet("color: #2f3640; font-family: 'Segoe UI', 'Inter'; font-size: 12px;")
            layout.addWidget(lbl)
            
        self.setLayout(layout)

class ScriptDrawer(QWidget):
    """Collapsible Educational script drawer displaying runnable Python/PyQGIS equivalent codes."""
    run_clicked = Signal(dict) # Emits the tool call dictionary
    
    def __init__(self, code_text: str, tool_call: dict = None, parent=None):
        super().__init__(parent)
        self.tool_call = tool_call
        
        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        
        title = QLabel("Educational Script Equivalent:")
        title.setStyleSheet("color: #14171c; font-family: 'Segoe UI', 'Inter'; font-size: 12px; font-weight: bold;")
        layout.addWidget(title)
        
        # Code display panel
        self.code_edit = QTextEdit()
        self.code_edit.setReadOnly(True)
        self.code_edit.setPlainText(code_text)
        self.code_edit.setFixedHeight(120)
        self.code_edit.setStyleSheet("""
            QTextEdit {
                background-color: #ffffff;
                border: 1px solid #d4d8de;
                border-radius: 4px;
                color: #14171c;
                font-family: 'SF Mono', Consolas, 'Liberation Mono', Menlo, Courier, monospace;
                font-size: 11px;
                padding: 6px;
            }
        """)
        layout.addWidget(self.code_edit)
        
        # Buttons bar
        btn_bar = QHBoxLayout()
        btn_bar.setContentsMargins(0, 0, 0, 0)
        
        self.copy_btn = QPushButton("Copy Code")
        self.copy_btn.setStyleSheet("""
            QPushButton {
                background-color: #e2e6ec;
                color: #2f3640;
                border: 1px solid #d4d8de;
                border-radius: 4px;
                padding: 4px 8px;
                font-size: 11px;
            }
            QPushButton:hover { background-color: #cfd5dd; }
        """)
        self.copy_btn.clicked.connect(self._copy_code)
        
        self.run_btn = QPushButton("Run Script")
        self.run_btn.setStyleSheet("""
            QPushButton {
                background-color: #1f6feb;
                color: #ffffff;
                border-radius: 4px;
                padding: 4px 8px;
                font-size: 11px;
                font-weight: bold;
            }
            QPushButton:hover { background-color: #0d5fcc; }
        """)
        self.run_btn.clicked.connect(self._run_script)
        # Disable run button if no tool call accompanies the script
        self.run_btn.setEnabled(tool_call is not None)
        
        btn_bar.addWidget(self.copy_btn)
        btn_bar.addStretch()
        btn_bar.addWidget(self.run_btn)
        
        layout.addLayout(btn_bar)
        self.setLayout(layout)
        self.setStyleSheet("background-color: #fafbfc; border: 1px solid #d4d8de; border-radius: 4px;")

    def _copy_code(self):
        from PySide6.QtGui import QGuiApplication
        clipboard = QGuiApplication.clipboard()
        clipboard.setText(self.code_edit.toPlainText())
        self.copy_btn.setText("Copied!")

    def _run_script(self):
        if self.tool_call:
            self.run_clicked.emit(self.tool_call)

class AgentDockWidget(QDockWidget):
    """
    AI Conversational Agent Dock Widget.
    Features scrolling bubbles panel, educational code viewer, and safe dispatch hooks.
    """
    tool_execution_requested = Signal(str, dict) # Emits (tool_name, params_dict)
    
    def __init__(self, parent=None):
        super().__init__("AI AGENT CONSOLE", parent)
        self.setAllowedAreas(Qt.LeftDockWidgetArea | Qt.RightDockWidgetArea)
        self.setFeatures(QDockWidget.DockWidgetMovable | QDockWidget.DockWidgetFloatable)
        
        self.executor = AgentExecutor()
        
        # Main dock widget layout
        self.main_widget = QWidget()
        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        
        # 1. Scrollable chat area
        self.scroll = QScrollArea()
        self.scroll.setObjectName("agentChatArea")
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        
        self.chat_container = QWidget()
        self.chat_layout = QVBoxLayout()
        self.chat_layout.setContentsMargins(10, 10, 10, 10)
        self.chat_layout.addStretch() # Push bubbles to bottom
        self.chat_container.setLayout(self.chat_layout)
        self.scroll.setWidget(self.chat_container)
        layout.addWidget(self.scroll)
        
        # 2. Typing indicator (hidden by default)
        self.typing_label = QLabel("Agent is typing plan...")
        self.typing_label.setObjectName("typingIndicator")
        self.typing_label.setVisible(False)
        layout.addWidget(self.typing_label)
        
        # 3. Message Input bar
        input_bar = QHBoxLayout()
        input_bar.setContentsMargins(0, 5, 0, 0)
        
        self.input_field = QLineEdit()
        self.input_field.setPlaceholderText("Ask agent (e.g. Calculate NDVI)...")
        self.input_field.returnPressed.connect(self._send_message)
        
        self.send_btn = QPushButton("Send")
        self.send_btn.setProperty("accent", True)
        self.send_btn.clicked.connect(self._send_message)
        
        input_bar.addWidget(self.input_field)
        input_bar.addWidget(self.send_btn)
        layout.addLayout(input_bar)
        
        self.main_widget.setLayout(layout)
        self.setWidget(self.main_widget)
        
        # Welcome greeting bubble on startup
        self.add_message_bubble("Welcome to Antigravity RS! I can help you process your remote sensing labs automatically. Try asking me: 'Calculate NDVI on sample_crops' or 'Run K-Means classification'.", is_user=False)

    def add_message_bubble(self, text: str, is_user: bool):
        bubble = MessageBubble(text, is_user)
        # Insert before the stretch item
        self.chat_layout.insertWidget(self.chat_layout.count() - 1, bubble)
        # Scroll to bottom
        bar = self.scroll.verticalScrollBar()
        bar.setValue(bar.maximum())

    def add_script_drawer(self, code_text: str, tool_call: dict = None):
        drawer = ScriptDrawer(code_text, tool_call)
        drawer.run_clicked.connect(self._handle_run_clicked)
        self.chat_layout.insertWidget(self.chat_layout.count() - 1, drawer)
        bar = self.scroll.verticalScrollBar()
        bar.setValue(bar.maximum())

    def _send_message(self):
        prompt = self.input_field.text().strip()
        if not prompt:
            return
            
        self.input_field.clear()
        self.add_message_bubble(prompt, is_user=True)
        
        # Lock buttons and show loading typing state
        self.send_btn.setEnabled(False)
        self.input_field.setEnabled(False)
        self.typing_label.setVisible(True)
        
        # Execute query in background thread
        self.worker = AgentWorker(self.executor, prompt)
        self.worker.response_received.connect(self._handle_worker_finished)
        self.worker.start()

    def _handle_worker_finished(self, response: dict):
        # Restore widgets state
        self.send_btn.setEnabled(True)
        self.input_field.setEnabled(True)
        self.typing_label.setVisible(False)
        
        # Render response thought and drawer script
        self.add_message_bubble(response["thought"], is_user=False)
        if "code" in response and response["code"]:
            self.add_script_drawer(response["code"], response.get("tool_call"))

    def _handle_run_clicked(self, tool_call: dict):
        # Relays run commands directly to main window coordinator
        self.tool_execution_requested.emit(tool_call["name"], tool_call["params"])
