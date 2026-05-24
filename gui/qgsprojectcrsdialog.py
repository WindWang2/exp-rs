from PySide6.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLineEdit, 
                                 QListWidget, QListWidgetItem, QPushButton, 
                                 QLabel, QDialogButtonBox)
from PySide6.QtCore import Qt

class ProjectCrsDialog(QDialog):
    """
    A premium Coordinate Reference System (CRS) selector dialog for Antigravity RS.
    Allows searching and selecting geographic/projected systems, matching QGIS Project Properties.
    """
    def __init__(self, current_crs: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Project Properties | Coordinate Reference System (CRS)")
        self.resize(500, 450)
        
        # Harmonious Slate Light styling
        self.setStyleSheet("""
            QDialog {
                background-color: #ffffff;
                font-family: 'Segoe UI', 'Inter', sans-serif;
            }
            QLabel {
                color: #2F3640;
                font-size: 12px;
            }
            QLineEdit {
                border: 1px solid #d4d8de;
                border-radius: 4px;
                padding: 6px 10px;
                background-color: #ffffff;
                color: #14171c;
                font-size: 13px;
            }
            QLineEdit:focus {
                border: 1.5px solid #1f6feb;
            }
            QListWidget {
                border: 1px solid #d4d8de;
                border-radius: 4px;
                background-color: #ffffff;
                color: #14171c;
                padding: 5px;
            }
            QListWidget::item {
                padding: 8px 10px;
                border-bottom: 1px solid #eef1f5;
                border-radius: 2px;
            }
            QListWidget::item:hover {
                background-color: #eef1f5;
            }
            QListWidget::item:selected {
                background-color: rgba(31, 111, 235, 0.10);
                color: #1f6feb;
                font-weight: bold;
            }
        """)
        
        self.selected_crs = current_crs
        
        # Define popular/standard systems
        self.crs_list = [
            {"code": "EPSG:3857", "name": "WGS 84 / Pseudo-Mercator", "type": "Projected (Meters)", "desc": "Standard Web Mercator projection used by Google Maps, OSM, and web GIS."},
            {"code": "EPSG:4326", "name": "WGS 84", "type": "Geographic (Degrees)", "desc": "World Geodetic System 1984. Standard GPS coordinates system using latitude and longitude."},
            {"code": "EPSG:4490", "name": "CGCS2000", "type": "Geographic (Degrees)", "desc": "China Geodetic Coordinate System 2000. Standard geographic system used in mainland China."},
            {"code": "EPSG:32649", "name": "WGS 84 / UTM zone 49N", "type": "Projected (Meters)", "desc": "Universal Transverse Mercator zone 49N, covering central China/Southeast Asia."},
            {"code": "EPSG:32650", "name": "WGS 84 / UTM zone 50N", "type": "Projected (Meters)", "desc": "Universal Transverse Mercator zone 50N, covering eastern China, Beijing, and regional areas."},
            {"code": "EPSG:32651", "name": "WGS 84 / UTM zone 51N", "type": "Projected (Meters)", "desc": "Universal Transverse Mercator zone 51N, covering Taiwan, Shanghai, and coastal areas."}
        ]
        
        # Layouts
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(12)
        
        # Header Info
        header = QLabel("Filter through available projections below to set your Map Canvas reference system:")
        header.setStyleSheet("font-weight: bold; color: #555555;")
        layout.addWidget(header)
        
        # Search Line
        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("Search CRS (e.g. WGS 84, CGCS, 3857)...")
        self.search_edit.textChanged.connect(self._filter_list)
        layout.addWidget(self.search_edit)
        
        # List of CRS
        self.list_widget = QListWidget()
        self.list_widget.itemSelectionChanged.connect(self._on_selection_changed)
        self.list_widget.itemDoubleClicked.connect(self.accept)
        layout.addWidget(self.list_widget)
        
        # Description Panel
        self.desc_group = QLabel("Select a projection to view details.")
        self.desc_group.setWordWrap(True)
        self.desc_group.setStyleSheet("""
            background-color: #f6f7f9;
            border: 1px dashed #d4d8de;
            border-radius: 4px;
            padding: 10px;
            color: #5b6473;
            font-size: 11px;
            min-height: 50px;
        """)
        layout.addWidget(self.desc_group)
        
        # Dialog buttons
        self.button_box = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, self)
        self.button_box.accepted.connect(self.accept)
        self.button_box.rejected.connect(self.reject)
        
        # Style buttons
        self.button_box.setStyleSheet("""
            QPushButton {
                background-color: #e2e6ec;
                border: 1px solid #d4d8de;
                border-radius: 4px;
                padding: 6px 16px;
                font-family: 'Segoe UI', 'Inter';
                font-size: 12px;
                color: #2F3640;
            }
            QPushButton:hover {
                background-color: #cfd5dd;
                border-color: #1f6feb;
            }
            QPushButton:pressed {
                background-color: #b3bac4;
            }
            QPushButton[text="OK"] {
                background-color: #1f6feb;
                color: white;
                border: 1px solid #1f6feb;
            }
            QPushButton[text="OK"]:hover {
                background-color: #0d5fcc;
                border-color: #0d5fcc;
            }
        """)
        layout.addWidget(self.button_box)
        
        # Populating list initially
        self._populate_list()
        
    def _populate_list(self):
        self.list_widget.clear()
        selected_item = None
        for crs in self.crs_list:
            item = QListWidgetItem(f"{crs['code']} — {crs['name']} [{crs['type']}]")
            item.setData(Qt.UserRole, crs)
            self.list_widget.addItem(item)
            if crs["code"] == self.selected_crs:
                selected_item = item
                
        if selected_item:
            self.list_widget.setCurrentItem(selected_item)
            
    def _filter_list(self, text):
        query = text.lower().strip()
        self.list_widget.clear()
        
        for crs in self.crs_list:
            match = (query in crs["code"].lower() or 
                     query in crs["name"].lower() or 
                     query in crs["type"].lower())
            if match or not query:
                item = QListWidgetItem(f"{crs['code']} — {crs['name']} [{crs['type']}]")
                item.setData(Qt.UserRole, crs)
                self.list_widget.addItem(item)
                if crs["code"] == self.selected_crs:
                    self.list_widget.setCurrentItem(item)

    def _on_selection_changed(self):
        items = self.list_widget.selectedItems()
        if not items:
            self.desc_group.setText("Select a projection to view details.")
            return
            
        crs = items[0].data(Qt.UserRole)
        self.selected_crs = crs["code"]
        self.desc_group.setText(f"<b>CRS ID:</b> {crs['code']}<br>"
                                f"<b>Name:</b> {crs['name']}<br>"
                                f"<b>Format:</b> {crs['type']}<br><br>"
                                f"<b>Description:</b> {crs['desc']}")

QgsProjectCrsDialog = ProjectCrsDialog
