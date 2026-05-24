# gui/workspace.py
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QSplitter, QTreeView)
from PySide6.QtCore import Qt
from gui.rs_widgets import (RsToolBar, RsStatusBar, RsConsole, RsPropertyPanel,
                            RsPanel, RsTabBar, RsSearchInput, RsRowDelegate)
from gui.map_overlay import MapOverlayContainer
from gui.layer_tree import LayerTreeModel, LayerTreeView


class RsWorkspace(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Toolbar (groups in prototype order)
        self.toolbar = RsToolBar()
        self.toolbar.add_group([
            {"id": "open", "icon": "folder", "tip": "打开"},
            {"id": "save", "icon": "save", "tip": "保存工程"},
            {"id": "new", "icon": "plus", "tip": "新建"}])
        self.toolbar.add_group([
            {"id": "select", "icon": "cursor", "checkable": True, "tip": "选择"},
            {"id": "pan", "icon": "pan", "checkable": True, "checked": True, "tip": "漫游"},
            {"id": "zoomIn", "icon": "zoomIn", "tip": "放大"},
            {"id": "zoomOut", "icon": "zoomOut", "tip": "缩小"},
            {"id": "zoomFit", "icon": "zoomFit", "tip": "全图"}], exclusive=True)
        self.toolbar.add_group([
            {"id": "identify", "icon": "identify", "tip": "要素信息"},
            {"id": "measure", "icon": "measure", "tip": "量测"},
            {"id": "sample", "icon": "crosshair", "tip": "光谱采样"}])
        self.toolbar.add_group([
            {"id": "bandcombo", "icon": "raster", "label": "波段合成"},
            {"id": "stretch", "icon": "palette", "label": "拉伸"},
            {"id": "hist", "icon": "histogram", "label": "直方图"}])
        self.toolbar.add_group([
            {"id": "classify", "icon": "wand", "label": "分类", "tip": "监督分类"},
            {"id": "model", "icon": "workflow", "label": "模型", "tip": "模型构建器"},
            {"id": "process", "icon": "cog", "label": "处理", "tip": "处理工具箱"}])
        self.toolbar.add_group([
            {"id": "ai", "icon": "spark", "label": "AI 助手", "checkable": True,
             "tip": "打开 AI 助手"}])
        root.addWidget(self.toolbar)

        # Main horizontal splitter
        self.main_splitter = QSplitter(Qt.Horizontal)
        root.addWidget(self.main_splitter, 1)

        # LEFT: data browser + layers
        left = QSplitter(Qt.Vertical)
        self.data_browser = QTreeView()
        self.data_browser.setHeaderHidden(True)
        self.data_browser.setItemDelegate(RsRowDelegate(self.data_browser))
        browser_panel = RsPanel("数据浏览器", icon="database",
                                actions=[("plus", "添加数据源"), ("refresh", "刷新")])
        browser_panel.add_body_widget(self.data_browser)
        self.layer_model = LayerTreeModel(self)
        self.layer_view = LayerTreeView(self)
        self.layer_view.setModel(self.layer_model)
        self.layer_view.setItemDelegate(RsRowDelegate(self.layer_view))
        layers_panel = RsPanel("图层", icon="layers",
                               actions=[("plus", "添加图层"), ("filter", "筛选"),
                                        ("moreV", "更多")])
        layers_panel.add_body_widget(self.layer_view)
        left.addWidget(browser_panel)
        left.addWidget(layers_panel)
        left.setSizes([260, 600])

        # CENTER: tabs + map + console
        center = QWidget()
        cl = QVBoxLayout(center)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        self.view_tabs = RsTabBar([("map", "地图视图 1", "globe", None),
                                   ("layout", "布局视图", "grid", None),
                                   ("3d", "3D 视图", "cog", None)], active="map")
        cl.addWidget(self.view_tabs)
        center_split = QSplitter(Qt.Vertical)
        self.map_container = MapOverlayContainer()
        self.canvas = self.map_container.canvas
        self.console = RsConsole()
        center_split.addWidget(self.map_container)
        center_split.addWidget(self.console)
        center_split.setSizes([700, 160])
        cl.addWidget(center_split, 1)

        # RIGHT: toolbox + properties
        right = QSplitter(Qt.Vertical)
        from gui.toolbox import ProcessingToolbox
        toolbox_panel = RsPanel("处理工具箱", icon="cog",
                                actions=[("search", "搜索"), ("bookmark", "收藏")])
        self.toolbox = ProcessingToolbox()
        toolbox_panel.add_body_widget(self.toolbox)
        self.property_panel = RsPropertyPanel()
        props_panel = RsPanel("图层属性", icon="raster",
                              actions=[("refresh", "刷新"), ("x", "关闭")])
        props_panel.add_body_widget(self.property_panel)
        right.addWidget(toolbox_panel)
        right.addWidget(props_panel)
        right.setSizes([500, 500])

        self.main_splitter.addWidget(left)
        self.main_splitter.addWidget(center)
        self.main_splitter.addWidget(right)
        self.main_splitter.setStretchFactor(0, 0)
        self.main_splitter.setStretchFactor(1, 1)
        self.main_splitter.setStretchFactor(2, 0)
        self.main_splitter.setSizes([280, 840, 320])

        self.status = RsStatusBar()
        root.addWidget(self.status)
