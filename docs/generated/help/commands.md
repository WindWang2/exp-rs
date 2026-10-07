# 命令参考（自动生成）

> 本页由统一帮助系统 6.0 从命令注册表生成；请勿手工编辑。
> 权威来源：CommandRegistry / data/help/commands.json。

## command.app.commandPalette（command.app.commandPalette）

- 用途：打开命令面板，按名称搜索并执行任意工作台命令。

## command.cartography.compose（command.cartography.compose）

- 用途：把草拟的 MapSpec 编译为带质量报告的打印版面。
- 前提：制图工作台存在有效模板草稿
- 建议下一步：检查质量报告后执行预检
- 相关主题：command.cartography.preflight

## command.cartography.export（command.cartography.export）

- 用途：以受控 DPI 导出版面并记录 sha256 证据。
- 前提：预检通过
- 建议下一步：保存导出证明用于追溯
- 相关主题：command.cartography.preflight

## command.cartography.preflight（command.cartography.preflight）

- 用途：对当前 MapSpec 执行出图前预检并列出修复建议。
- 前提：已存在编译后的版面草稿
- 建议下一步：按建议修复后再次预检
- 相关主题：command.cartography.compose、command.cartography.repair

## command.cartography.repair（command.cartography.repair）

- 用途：按预检建议自动修复 MapSpec 的常见问题。
- 前提：预检产生了可修复项
- 建议下一步：修复后重新预检并导出
- 相关主题：command.cartography.preflight、command.cartography.export

## command.layer.addRaster（command.layer.addRaster）

- 用途：加载栅格数据（影像、DEM、分类结果等）到工程。
- 前提：文件路径可访问
- 建议下一步：缩放到图层范围检查显示效果
- 相关主题：command.layer.addVector、command.layer.zoomTo

## command.layer.addVector（command.layer.addVector）

- 用途：加载矢量数据（点/线/面）到工程，用于标注、采样或边界。
- 前提：文件路径可访问
- 相关主题：command.layer.addRaster、command.layer.attributeTable

## command.layer.attributeTable（command.layer.attributeTable）

- 用途：查看/编辑矢量属性表，检查类别字段与样本属性。
- 前提：已选中矢量图层
- 相关主题：command.layer.toggleEditing

## command.layer.newVector（command.layer.newVector）

- 用途：创建新的矢量图层用于采集样本点、训练区或标注边界。
- 相关主题：command.layer.toggleEditing、workbench.classify

## command.layer.properties（command.layer.properties）

- 用途：查看与修改当前图层的元信息：CRS、波段、符号化、直方图等。
- 前提：已选中一个图层
- 相关主题：command.layer.zoomTo

## command.layer.remove（command.layer.remove）

- 用途：从工程移除不再需要的图层，保持工作区整洁。删除的只是引用，不会删除磁盘文件。
- 前提：已选中一个图层

## command.layer.saveEdits（command.layer.saveEdits）

- 用途：把编辑会话中的几何/属性修改写回数据源，避免意外丢失。
- 前提：当前矢量图层处于编辑状态
- 建议下一步：继续编辑或关闭编辑会话
- 相关主题：command.layer.toggleEditing

## command.layer.toggleEditing（command.layer.toggleEditing）

- 用途：开启或关闭矢量编辑会话，是数字化、修改几何与属性的前置状态。
- 前提：已选中矢量图层；图层格式支持写入
- 建议下一步：编辑完成后执行“保存编辑”
- 相关主题：command.layer.saveEdits、command.layer.attributeTable

## command.layer.zoomTo（command.layer.zoomTo）

- 用途：快速定位到当前图层的地理范围，检查数据覆盖区域。
- 前提：已选中一个图层
- 相关主题：command.map.zoomFull

## command.map.compareLayers（command.map.compareLayers）

- 用途：并排对比两个时相或两种处理结果，直观检查变化与效果差异。
- 前提：工程中至少两个图层
- 相关主题：command.map.swipe

## command.map.identify（command.map.identify）

- 用途：点击查询像元或要素的属性值，核对光谱值、类别码等。
- 相关主题：command.map.measureDistance

## command.map.measureArea（command.map.measureArea）

- 用途：量测多边形面积，例如估算样区覆盖面积。
- 相关主题：command.map.measureDistance

## command.map.measureDistance（command.map.measureDistance）

- 用途：量测两点间距离，用于检查样本间距、地物尺寸。
- 相关主题：command.map.measureArea

## command.map.pan（command.map.pan）

- 用途：平移地图，不改变比例尺。
- 相关主题：command.map.zoomIn

## command.map.refresh（command.map.refresh）

- 用途：重绘地图，刷新渲染缓存。

## command.map.swipe（command.map.swipe）

- 用途：卷帘式对比上下图层，拖动分割线即可逐像元检查处理前后差异。
- 相关主题：command.map.compareLayers

## command.map.zoomFull（command.map.zoomFull）

- 用途：缩放到所有图层的整体范围，快速回到全局视角。
- 相关主题：command.layer.zoomTo

## command.map.zoomIn（command.map.zoomIn）

- 用途：放大地图视图，查看更小范围的细节。
- 相关主题：command.map.zoomOut、command.map.zoomFull

## command.map.zoomOut（command.map.zoomOut）

- 用途：缩小地图视图，查看更大范围。
- 相关主题：command.map.zoomIn

## command.mission.task.resume（command.mission.task.resume）

- 用途：恢复选中的过期或已取消任务：重新绑定其引用后从断点继续，而不是重跑已完成的部分。
- 前提：选中的任务处于可恢复（stale/canceled）状态；任务引用仍可解析
- 建议下一步：在任务时间线中确认恢复后的执行进度
- 相关主题：command.mission.task.retry、command.mission.timeline.show

## command.mission.task.retry（command.mission.task.retry）

- 用途：重试当前选中的失败或已取消的任务：沿用原任务参数在任务中心重新排队执行，不改写历史记录。
- 前提：选中的任务处于 failed 或 canceled 状态；任务引用的输入数据仍然可用
- 建议下一步：在任务时间线中查看新的执行记录
- 相关主题：command.mission.task.resume、command.mission.timeline.show

## command.mission.timeline.show（command.mission.timeline.show）

- 用途：打开任务时间线面板：按时间轴查看任务执行、事件与状态迁移。
- 前提：已打开包含任务运行记录的工程
- 建议下一步：在时间线中选中一个任务节点查看其详情
- 相关主题：command.mission.task.retry、command.mission.task.resume

## command.project.exit（command.project.exit）

- 用途：关闭应用程序。
- 前提：未保存修改已确认

## command.project.importLayer（command.project.importLayer）

- 用途：把本地栅格/矢量数据接入当前工程，是所有分析的第一步。
- 前提：数据文件可读且格式受支持 (GeoTIFF、Shapefile、GeoPackage 等)
- 建议下一步：选中图层后运行处理工具
- 相关主题：command.layer.addRaster、command.layer.addVector、command.project.stacBrowse

## command.project.new（command.project.new）

- 用途：开始一个新的制图或分析任务，获得干净的图层与视图状态。
- 前提：当前工程未保存的修改已确认处理
- 建议下一步：通过“添加栅格图层”或“添加矢量图层”加载数据
- 相关主题：command.project.open、command.layer.addRaster、command.layer.addVector

## command.project.newLayout（command.project.newLayout）

- 用途：创建打印布局，把地图、图例、比例尺等制图元素组合成成果图。
- 前提：至少一个数据图层
- 相关主题：workbench.layout

## command.project.open（command.project.open）

- 用途：恢复一个已保存的工程，包括图层、样式与视图状态。
- 前提：已知工程文件路径
- 建议下一步：在图层树中检查数据源是否完整
- 相关主题：command.project.save、command.project.saveAs

## command.project.save（command.project.save）

- 用途：把图层列表、符号化与视图状态持久化，便于下次继续。
- 前提：工程已有保存路径（首次保存请用另存为）
- 建议下一步：定期保存以避免丢失编辑成果
- 相关主题：command.project.saveAs、command.layer.saveEdits

## command.project.saveAs（command.project.saveAs）

- 用途：将当前工程另存为新文件，用于分叉出不同的工作版本。
- 相关主题：command.project.save

## command.project.stacBrowse（command.project.stacBrowse）

- 用途：通过 STAC 目录检索和下载遥感影像（如 Sentinel-2、Landsat），替代手工下载。
- 前提：可访问的 STAC API 地址
- 建议下一步：检索结果可直接加入数据管理器
- 相关主题：command.project.importLayer

## command.rs.applyMask（command.rs.applyMask）

- 用途：把掩膜应用到影像：置 NoData 或裁剪，剔除云与无效区。
- 前提：影像与掩膜图层
- 相关主题：operator.rs.apply_mask、command.rs.qaMask

## command.rs.atmospheric（command.rs.atmospheric）

- 用途：把表观反射率校正为地表反射率（DOS/QUAC 模型），为定量分析打基础。
- 前提：已定标的表观反射率或 DN 数据
- 相关主题：operator.rs.atmospheric_dos1、operator.rs.atmospheric_dos2、operator.rs.atmospheric_quac

## command.rs.bandMath（command.rs.bandMath）

- 用途：用表达式对多波段像元值做自定义运算，实现指数、掩膜逻辑等灵活计算。
- 前提：已选中栅格图层
- 相关主题：operator.rs.band_math、operator.rs.spectral_index

## command.rs.bandRatio（command.rs.bandRatio）

- 用途：计算两波段比值或归一化比值，抑制光照差异突出反射率差异。
- 前提：已选中多波段栅格
- 相关主题：operator.rs.band_ratio

## command.rs.changeDetection（command.rs.changeDetection）

- 用途：对两期影像做差异/比值/CVA 变化检测，定位地物变化区域。
- 前提：两期已配准影像
- 相关主题：operator.rs.change_detection、concept.rs.change_detection

## command.rs.contrastStretch（command.rs.contrastStretch）

- 用途：改善影像显示对比度：线性拉伸、百分比裁剪或直方图均衡。
- 前提：已选中栅格图层
- 相关主题：operator.rs.contrast_stretch

## command.rs.extractBands（command.rs.extractBands）

- 用途：从多波段栅格中提取指定波段另存，用于精简数据或单独处理。
- 前提：已选中多波段栅格
- 相关主题：operator.rs.extract_bands

## command.rs.fusion（command.rs.fusion）

- 用途：全色锐化：把多光谱色彩与全色细节融合（Brovey/IHS/Gram-Schmidt 等）。
- 前提：配准的全色与多光谱影像
- 相关主题：operator.rs.image_fusion

## command.rs.mosaic（command.rs.mosaic）

- 用途：把多景相邻影像镶嵌为一幅连续影像。
- 前提：多个已配准的栅格； CRS 一致
- 相关主题：operator.rs.mosaic

## command.rs.ortho（command.rs.ortho）

- 用途：基于 RPC/GCP 做正射纠正，消除地形与投影几何变形。
- 前提：影像带 RPC 或已有控制点；DEM（地形明显时必需）
- 相关主题：workbench.georef_i2m、concept.rs.rpc

## command.rs.pca（command.rs.pca）

- 用途：对多波段做主成分分析，压缩冗余、突出主要信息维度。
- 前提：已选中多波段栅格
- 相关主题：operator.rs.pca、operator.rs.mnf

## command.rs.qaMask（command.rs.qaMask）

- 用途：解码 Landsat/Sentinel QA 波段为云/阴影/雪掩膜。
- 前提：包含 QA 波段的 Landsat/Sentinel 产品
- 相关主题：operator.rs.qa_mask、command.rs.applyMask

## command.rs.radiometric（command.rs.radiometric）

- 用途：把 DN 定标为辐亮度/反射率，是定量遥感的第一个换算。
- 前提：带定标参数的产品
- 相关主题：operator.rs.radiometric_calibration、operator.rs.dn_to_radiance

## command.rs.spatialFilter（command.rs.spatialFilter）

- 用途：用均值/高斯/中值/拉普拉斯核做空间卷积，去噪或增强边缘。
- 前提：已选中栅格图层
- 相关主题：operator.rs.image_enhancement

## command.rs.speckle（command.rs.speckle）

- 用途：抑制 SAR 影像相干斑点（Lee/Frost/Kuan/Gamma-MAP），为后续解译与分类降噪。
- 前提：已选中 SAR 栅格图层
- 相关主题：operator.rs.sar_speckle、concept.rs.speckle

## command.rs.spectralIndex（command.rs.spectralIndex）

- 用途：计算常用光谱指数（NDVI/EVI/NDWI/NDBI/MNDWI 等），一步完成波段组合。
- 前提：已选中多波段栅格；波段对应关系正确
- 相关主题：operator.rs.spectral_index、concept.rs.ndvi

## command.rs.temporal（command.rs.temporal）

- 用途：时序分析入口：时序指数、趋势、物候与断点检测。
- 前提：时间序列影像集合
- 相关主题：operator.rs.temporal_composite、concept.rs.temporal_analysis

## command.rs.terrain（command.rs.terrain）

- 用途：从 DEM 生成坡度/坡向/山影等地形产品。
- 前提：已选中 DEM 栅格
- 相关主题：operator.rs.terrain_analysis

## command.teaching.adminConsole.show（command.teaching.adminConsole.show）

- 用途：打开教师授课与作业评估中控台：课程编排、实验包校验与批量自动评分。
- 前提：当前账号处于教学管理员或教师角色
- 建议下一步：选择实验包进行就绪度预检或执行学生作业批量自动评分
- 相关主题：command.teaching.labCockpit.show

## command.teaching.labCockpit.show（command.teaching.labCockpit.show）

- 用途：打开遥感实验学习工作台：课程首页、就绪度检查与分步引导。
- 前提：当前账号处于教学场景（学生或教师）
- 建议下一步：按课程首页的就绪度提示准备数据后开始引导步骤
- 相关主题：command.mission.timeline.show

## command.view.linkCenter（command.view.linkCenter）

- 用途：在多个地图视图之间同步平移/缩放的中心与范围，一图移动、组内视图跟随。
- 前提：已打开至少一个副视图或会话视图
- 建议下一步：用“活动视图联动”把当前视图加入联动组
- 相关主题：command.view.linkScale、command.view.linkGroupStatus

## command.view.linkCursor（command.view.linkCursor）

- 用途：在联动视图之间同步光标十字丝，跨 CRS 自动换算坐标。
- 前提：至少两个视图已联动
- 建议下一步：在任一联动视图内移动鼠标观察十字丝
- 相关主题：command.view.linkCenter、command.workbench.visualAnalytics

## command.view.linkGroupStatus（command.view.linkGroupStatus）

- 用途：把活动视图加入/移出默认联动组。
- 前提：存在活动视图
- 建议下一步：移动当前视图，检查联动组内视图是否跟随
- 相关主题：command.view.linkCenter、command.view.linkUnlinkAll

## command.view.linkScale（command.view.linkScale）

- 用途：在联动视图之间同步显示比例，保证判读比例一致。
- 前提：已打开至少一个副视图或会话视图
- 建议下一步：先用“联动视图中心”打开范围联动
- 相关主题：command.view.linkCenter、command.view.linkCursor

## command.view.linkUndo（command.view.linkUndo）

- 用途：撤销活动视图上一次平移/缩放，回到上一个范围。
- 前提：活动视图已有范围历史
- 建议下一步：连续执行可逐步回退；重新平移可产生新历史
- 相关主题：command.view.linkGroupStatus

## command.view.linkUnlinkAll（command.view.linkUnlinkAll）

- 用途：把所有视图移出所有联动组，恢复各自独立浏览。
- 前提：存在已联动视图
- 建议下一步：需要再次联动时重新勾选联动命令
- 相关主题：command.view.linkGroupStatus、command.view.linkCenter

## command.view.linkVisibility（command.view.linkVisibility）

- 用途：按资产身份在不同视图之间同步图层可见性与透明度（同名不同源的图层不会误联）。
- 前提：同一资产已加载到多个视图
- 建议下一步：在图层树中切换可见性，观察其他视图同步
- 相关主题：command.view.linkCenter

## command.workbench.cartography（command.workbench.cartography）

- 用途：打开制图工作台：模板草拟、操作符派发与导出证明。
- 前提：地图窗口存在可选图层
- 建议下一步：从模板草拟开始编排地图版面
- 相关主题：command.cartography.compose

## command.workbench.classify（command.workbench.classify）

- 用途：打开监督/非监督分类工作区，完成样本采集、训练与分类全流程。
- 前提：已加载待分类影像
- 建议下一步：先采集或导入训练样本
- 相关主题：workbench.classify、operator.rs.supervised_classification

## command.workbench.classifyStudio（command.workbench.classifyStudio）

- 用途：打开分类/变化检测工作台会话窗口。
- 前提：存在分类或变化检测输入数据
- 建议下一步：选择影像并启动分类流程
- 相关主题：command.layer.addRaster

## command.workbench.datasetExperiment（command.workbench.datasetExperiment）

- 用途：浏览数据集版本、样本、运行与指标对比。

## command.workbench.experimentExplorationStudio（command.workbench.experimentExplorationStudio）

- 用途：打开实验探索工作台：参数敏感性分析、空间对比、故障教学与首次分歧定位。
- 前提：已有可用的实验运行记录或工作流产物
- 建议下一步：选择一次运行查看其参数敏感度或分歧报告
- 相关主题：command.mission.timeline.show

## command.workbench.georefDual（command.workbench.georefDual）

- 用途：打开双窗口影像配准（Image to Map / Image to Image）。
- 前提：已准备参考图或目标影像
- 建议下一步：在源/目标窗口间刺点并求解变换
- 相关主题：command.workbench.classifyStudio

## command.workbench.georefI2I（command.workbench.georefI2I）

- 用途：影像对影像配准：以参考影像为基准纠正源影像的几何偏移，支持同名点与 SIFT 自动匹配。
- 前提：源影像与参考影像已加载；两影像有足够重叠区
- 相关主题：workbench.georef_i2i、concept.rs.rpc

## command.workbench.georefI2M（command.workbench.georefI2M）

- 用途：影像对地图配准：以主工程地图为参考取控制点，支持 RPC 物理模型。
- 前提：源影像已加载；工程地图已有地理参考
- 相关主题：workbench.georef_i2m、concept.rs.rpc

## command.workbench.ir2Pipeline（command.workbench.ir2Pipeline）

- 用途：打开 IR 2.0 流水线设计器，编排任务节点。
- 建议下一步：从算子目录拖入节点开始编排
- 相关主题：command.workbench.operatorCatalog

## command.workbench.model（command.workbench.model）

- 用途：查看模型目录、就绪状态并提交测试推理。

## command.workbench.obia（command.workbench.obia）

- 用途：打开面向对象分类工作区：先分割成对象，再基于对象特征分类。
- 前提：已加载影像
- 相关主题：workbench.obia、operator.rs.obia_segment

## command.workbench.operatorCatalog（command.workbench.operatorCatalog）

- 用途：打开 rs: 算子目录：按名称/模态搜索算子，管理最近使用与收藏。
- 建议下一步：选择一个算子以创建单步工作流
- 相关主题：command.workflow.new

## command.workbench.processingHistory（command.workbench.processingHistory）

- 用途：查看跨任务中心/工作流的统一处理历史与状态追踪。

## command.workbench.temporal（command.workbench.temporal）

- 用途：浏览时序集合、筛选日期并预览时相影像。

## command.workbench.visualAnalytics（command.workbench.visualAnalytics）

- 用途：打开可视化分析工作台：有界抽样的直方图、散点与波段曲线，并支持图表间联动刷选。
- 前提：当前选择包含栅格图层
- 建议下一步：刷新图表后拖选直方图范围过滤散点
- 相关主题：command.view.linkCursor、command.workbench.cartography

## command.workflow.new（command.workflow.new）

- 用途：新建一个空的工作流画布，开始编排数据处理全流程。
- 前提：当前工程未保存的修改已确认处理
- 建议下一步：从算子库拖入第一个算子（例如“读取栅格”）
- 相关主题：command.workflow.open、command.workflow.save、command.workflow.run

## command.workflow.open（command.workflow.open）

- 用途：打开一个已保存的工作流文件，继续编排或执行。
- 前提：已知工作流文件位置
- 建议下一步：点击“运行全流程”执行工作流
- 相关主题：command.workflow.new、command.workflow.save、command.workflow.run

## command.workflow.run（command.workflow.run）

- 用途：按拓扑顺序执行画布上的全部算子，产出最终数据产品。
- 前提：工作流参数已校验通过；输入数据已就绪
- 建议下一步：在运行日志与诊断面板查看执行进度
- 相关主题：command.workflow.stop、command.workflow.save

## command.workflow.save（command.workflow.save）

- 用途：把当前工作流（算子、连线与参数）保存为可复用文件。
- 前提：画布上存在至少一个算子或连线
- 建议下一步：需要再次使用时通过“打开工作流”载入
- 相关主题：command.workflow.new、command.workflow.open、command.workflow.run

## command.workflow.stop（command.workflow.stop）

- 用途：停止正在运行的工作流，已完成的中间结果会被保留。
- 前提：工作流正在运行
- 建议下一步：修正参数后可再次“运行全流程”
- 相关主题：command.workflow.run
