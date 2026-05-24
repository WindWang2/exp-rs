from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QTreeWidget,
                                QTreeWidgetItem, QDialog, QFormLayout, QDialogButtonBox,
                                QLabel, QPushButton, QFileDialog, QSpinBox, QDoubleSpinBox,
                                QComboBox, QCheckBox)
from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont, QColor, QIcon

from analysis.qgsprocessingregistry import QgsProcessingRegistry
from gui.rs_widgets import RsSearchInput

class ToolParameterDialog(QDialog):
    """
    Spawns parameter configuration dialogs dynamically based on Tool Schemas.
    Styled matching our premium Slate/Lavender QSS theme.
    """
    def __init__(self, tool_schema: dict, parent=None):
        super().__init__(parent)
        self.tool_schema = tool_schema
        self.inputs = {}
        
        self.setWindowTitle(tool_schema["label"])
        self.resize(460, 320)
        
        layout = QVBoxLayout()
        layout.setContentsMargins(20, 20, 20, 20)
        
        # Tool description header
        desc = QLabel(tool_schema["description"])
        desc.setWordWrap(True)
        desc.setStyleSheet("color: #555555; font-family: 'Segoe UI', 'Inter'; font-size: 12px; margin-bottom: 12px;")
        layout.addWidget(desc)
        
        form_layout = QFormLayout()
        form_layout.setLabelAlignment(Qt.AlignRight)
        
        # Dynamic parameter controls generation
        for p in tool_schema["params"]:
            ptype = p["type"]
            label_text = p["label"]
            pname = p["name"]
            
            control = None
            if ptype == "file":
                control = QWidget()
                h_layout = QHBoxLayout()
                h_layout.setContentsMargins(0, 0, 0, 0)
                file_edit = QLineEdit(str(p.get("default", "")))
                btn = QPushButton("Browse...")
                # Bind folder browse dialog
                btn.clicked.connect(lambda checked=False, fe=file_edit: self._browse_file(fe))
                h_layout.addWidget(file_edit)
                h_layout.addWidget(btn)
                control.setLayout(h_layout)
                self.inputs[pname] = file_edit # Reference the text edit
            elif ptype == "int":
                spin = QSpinBox()
                spin.setRange(-999999, 999999)
                spin.setValue(p.get("default", 0))
                control = spin
                self.inputs[pname] = spin
            elif ptype == "float":
                dspin = QDoubleSpinBox()
                dspin.setRange(-999999.0, 999999.0)
                dspin.setValue(p.get("default", 0.0))
                dspin.setDecimals(4)
                control = dspin
                self.inputs[pname] = dspin
            elif ptype == "select":
                combo = QComboBox()
                combo.addItems(p.get("options", []))
                combo.setCurrentText(str(p.get("default", "")))
                control = combo
                self.inputs[pname] = combo
            elif ptype == "bool":
                chk = QCheckBox()
                chk.setChecked(p.get("default", False))
                control = chk
                self.inputs[pname] = chk
            else:
                edit = QLineEdit(str(p.get("default", "")))
                control = edit
                self.inputs[pname] = edit
                
            if control:
                form_layout.addRow(QLabel(label_text), control)
                
        layout.addLayout(form_layout)
        
        # Standard dynamic dialogue buttons
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        
        self.setLayout(layout)

    def _browse_file(self, line_edit):
        file_path, _ = QFileDialog.getSaveFileName(self, "Select File Location", "", "GeoTIFF (*.tif *.tiff);;All Files (*)")
        if file_path:
            line_edit.setText(file_path)
            
    def get_parameter_values(self) -> dict:
        values = {}
        for pname, control in self.inputs.items():
            if isinstance(control, QLineEdit):
                values[pname] = control.text()
            elif isinstance(control, QSpinBox):
                values[pname] = control.value()
            elif isinstance(control, QDoubleSpinBox):
                values[pname] = control.value()
            elif isinstance(control, QComboBox):
                values[pname] = control.currentText()
            elif isinstance(control, QCheckBox):
                values[pname] = control.isChecked()
        return values

class QgsProcessingToolbox(QWidget):
    """
    Searchable Processing Toolbox panel.
    Lists registered tools and dynamically configures parameter dialogs on double-click.
    """
    tool_triggered = Signal(str, dict) # Emits (tool_name, parameters_dict)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.registry = QgsProcessingRegistry()
        
        layout = QVBoxLayout()
        layout.setContentsMargins(10, 10, 10, 10)
        
        # 1. Dynamic Autocomplete Search Bar
        _search = RsSearchInput("Search tools...")
        self.search_bar = _search.line          # keep public attr for back-compat
        self.search_bar.textChanged.connect(self._filter_tools)
        layout.addWidget(_search)
        
        # 2. Hierarchical Categories TreeWidget
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.itemDoubleClicked.connect(self._handle_item_double_clicked)
        layout.addWidget(self.tree)
        
        self.setLayout(layout)
        self.populate_toolbox()
        
    def populate_toolbox(self):
        self.tree.clear()
        category_items = {}
        
        for tool in self.registry.list_tools():
            cat = tool["category"]
            if cat not in category_items:
                cat_item = QTreeWidgetItem(self.tree, [cat])
                cat_item.setFlags(cat_item.flags() & ~Qt.ItemIsSelectable) # Headers are folding only
                cat_item.setFont(0, QFont("Segoe UI", 11, QFont.Bold))
                cat_item.setForeground(0, QColor("#1f6feb"))
                category_items[cat] = cat_item
                
            tool_item = QTreeWidgetItem(category_items[cat], [tool["label"]])
            tool_item.setData(0, Qt.UserRole, tool["name"])
            tool_item.setIcon(0, QIcon.fromTheme("system-run"))
            
        self.tree.expandAll()

    def _filter_tools(self, query: str):
        """Filters tool list by search text query."""
        query = query.lower()
        for i in range(self.tree.topLevelItemCount()):
            cat_item = self.tree.topLevelItem(i)
            cat_match = False
            for j in range(cat_item.childCount()):
                tool_item = cat_item.child(j)
                tool_match = query in tool_item.text(0).lower()
                tool_item.setHidden(not tool_match)
                if tool_match:
                    cat_match = True
            cat_item.setHidden(not cat_match)

    def _handle_item_double_clicked(self, item, column):
        tool_name = item.data(0, Qt.UserRole)
        if not tool_name:
            return # Category group node clicked
            
        tool = self.registry.get_tool(tool_name)
        dialog = ToolParameterDialog(tool, self)
        if dialog.exec_() == QDialog.Accepted:
            params = dialog.get_parameter_values()
            self.tool_triggered.emit(tool_name, params)

# Backward-compatibility aliases
QgsProcessingToolbox = QgsProcessingToolbox
ProcessingToolbox = QgsProcessingToolbox
