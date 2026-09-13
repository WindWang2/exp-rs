<?xml version='1.0' encoding='utf-8'?>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>AnnotationWidget</name>
    <message>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <source>Choose the annotation type: text / arrow / rectangle / circle / freehand.</source>
        <translation>选择标注类型：文本/箭头/矩形/圆形/手绘。</translation>
    </message>
    <message>
        <source>Text</source>
        <translation>文本</translation>
    </message>
    <message>
        <source>Arrow</source>
        <translation>箭头</translation>
    </message>
    <message>
        <source>Rectangle</source>
        <translation>矩形</translation>
    </message>
    <message>
        <source>Circle</source>
        <translation>圆形</translation>
    </message>
    <message>
        <source>Freehand</source>
        <translation>手绘</translation>
    </message>
    <message>
        <source>Color:</source>
        <translation>颜色：</translation>
    </message>
    <message>
        <source>Click to choose the annotation color.</source>
        <translation>点击选择标注颜色。</translation>
    </message>
    <message>
        <source>Width:</source>
        <translation>线宽：</translation>
    </message>
    <message>
        <source>Line width (1–10 pixels).</source>
        <translation>线条宽度（1-10 像素）。</translation>
    </message>
    <message>
        <source>Add Text</source>
        <translation>添加文本</translation>
    </message>
    <message>
        <source>Adds a text annotation.</source>
        <translation>添加文本标注。</translation>
    </message>
    <message>
        <source>Clear All</source>
        <translation>清除全部</translation>
    </message>
    <message>
        <source>Clears all annotations.</source>
        <translation>清除所有标注。</translation>
    </message>
    <message>
        <source>Add Text Annotation</source>
        <translation>添加文本标注</translation>
    </message>
    <message>
        <source>Enter annotation text</source>
        <translation>输入标注文字</translation>
    </message>
    <message>
        <source>Text:</source>
        <translation>文字：</translation>
    </message>
    <message>
        <source>Select Color</source>
        <translation>选择颜色</translation>
    </message>
</context>
<context>
    <name>ApplyMaskDialog</name>
    <message>
        <source>Input Data and Mask Raster</source>
        <translation>输入数据与掩膜栅格</translation>
    </message>
    <message>
        <source>Product raster to mask (multiband).</source>
        <translation>待掩膜的产品栅格（多波段）。</translation>
    </message>
    <message>
        <source>Product Raster</source>
        <translation>产品栅格</translation>
    </message>
    <message>
        <source>Applies the mask (1 = obscured, 0 = valid) to the product raster: obscured pixels are set to NoData, yielding an analysis-ready image.</source>
        <translation>将掩膜（1 = 被遮挡，0 = 有效）应用到产品栅格：被遮挡像元在所有波段置为 NoData，得到分析就绪影像。</translation>
    </message>
    <message>
        <source>A binary mask raster (band 1; &gt; 0 means obscured). Usually the output of the 'QA Mask' dialog;With different grids but the same CRS, nearest-neighbour alignment happens automatically.</source>
        <translation>二值掩膜栅格（第 1 波段，&gt;0 视为被遮挡）。通常是“QA 掩膜”对话框的输出；网格不同且 CRS 相同时会自动最近邻对齐。</translation>
    </message>
    <message>
        <source>Mask Raster</source>
        <translation>掩膜栅格</translation>
    </message>
    <message>
        <source>Advanced Options and Alignment</source>
        <translation>高级选项与对齐</translation>
    </message>
    <message>
        <source>By default the input band's own NoData is reused; specify a value only when the input band has none.</source>
        <translation>默认复用输入波段自身的 NoData；仅在输入波段未定义 NoData 时才需要指定。</translation>
    </message>
    <message>
        <source>Specify the output NoData value</source>
        <translation>指定输出 NoData 值</translation>
    </message>
    <message>
        <source>When ticked, obscured pixels are written with this NoData value (instead of the input band's own NoData).Required when the input bands define no NoData.</source>
        <translation>勾选后，被遮挡像元写入该 NoData 值（替代输入波段自带 NoData）。输入波段未定义 NoData 时此项必填。</translation>
    </message>
    <message>
        <source>When the mask grid differs from the product (e.g. a 20 m SCL against a 10 m product), nearest-neighbour sampling aligns the mask to the product grid.A CRS mismatch always raises an error; it is never corrected automatically.</source>
        <translation>掩膜网格与产品不同（如 20 m SCL 对 10 m 产品）时，用最近邻采样把掩膜对齐到产品网格。CRS 不一致始终报错，不会自动纠正。</translation>
    </message>
    <message>
        <source>NoData replacement fill value for masked pixels</source>
        <translation>被掩膜遮挡像元的 NoData 替换填充值</translation>
    </message>
    <message>
        <source>NoData Override</source>
        <translation>NoData 覆盖</translation>
    </message>
    <message>
        <source>Align the mask grid automatically (nearest neighbour, same CRS only)</source>
        <translation>自动对齐掩膜网格（最近邻，仅限相同 CRS）</translation>
    </message>
    <message>
        <source>Grid Alignment</source>
        <translation>网格对齐</translation>
    </message>
    <message>
        <source>Specify the output file.</source>
        <translation>请指定输出文件。</translation>
    </message>
    <message>
        <source>Select the product raster and the mask raster.</source>
        <translation>请选择产品栅格与掩膜栅格。</translation>
    </message>
    <message>
        <source>The selected raster layer is invalid.</source>
        <translation>所选栅格图层无效。</translation>
    </message>
    <message>
        <source>Apply Mask</source>
        <translation>应用掩膜</translation>
    </message>
</context>
<context>
    <name>AsyncGdalRunner</name>
    <message>
        <source>Operation failed. Check log for details.</source>
        <translation>操作失败，详情请查看日志。</translation>
    </message>
</context>
<context>
    <name>AtmosphericDialog</name>
    <message>
        <source>Atmospheric Correction</source>
        <translation>大气校正</translation>
    </message>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layer for atmospheric correction.</source>
        <translation>选择待执行大气校正的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Correction Parameters</source>
        <translation>校正参数</translation>
    </message>
    <message>
        <source>DN → Radiance</source>
        <translation>DN → 辐射亮度</translation>
    </message>
    <message>
        <source>DOS1 Dark-Object Subtraction</source>
        <translation>DOS1 暗目标减法</translation>
    </message>
    <message>
        <source>DOS2 (with transmittance)</source>
        <translation>DOS2（含透过率）</translation>
    </message>
    <message>
        <source>QUAC Quick Atmospheric Correction</source>
        <translation>QUAC 快速大气校正</translation>
    </message>
    <message>
        <source>• DN to radiance: L=gain×DN+bias
• DOS1: dark object subtraction
• DOS2: DOS1 + transmittance
• QUAC: fast all-band correction from image statistics</source>
        <translation>• DN-&gt;辐射：L=gain×DN+bias
• DOS1：暗目标减法
• DOS2：DOS1 + 透过率
• QUAC：基于图像统计的全波段快速校正</translation>
    </message>
    <message>
        <source>Correction Method</source>
        <translation>校正方法</translation>
    </message>
    <message>
        <source>Band number to correct.</source>
        <translation>要校正的波段号。</translation>
    </message>
    <message>
        <source>Target Band</source>
        <translation>目标波段</translation>
    </message>
    <message>
        <source>Radiometric calibration gain.</source>
        <translation>辐射定标增益（gain）。</translation>
    </message>
    <message>
        <source>Gain</source>
        <translation>增益 Gain</translation>
    </message>
    <message>
        <source>Radiometric calibration bias.</source>
        <translation>辐射定标偏置（bias）。</translation>
    </message>
    <message>
        <source>Bias</source>
        <translation>偏置 Bias</translation>
    </message>
    <message>
        <source>Airmass</source>
        <translation>大气光学质量</translation>
    </message>
    <message>
        <source>Airmass (DOS2 only), usually ≥ 1.</source>
        <translation>大气光学质量（仅 DOS2），通常 ≥ 1。</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Sensor metadata file not found; enter gain/bias manually.</source>
        <translation>未找到传感器元数据文件；请手动输入 gain/bias。</translation>
    </message>
    <message>
        <source>Detected %1, but band %2 has no coefficients: %3</source>
        <translation>已探测到 %1，但波段 %2 无系数：%3</translation>
    </message>
    <message>
        <source>Enter gain/bias manually.</source>
        <translation>请手动输入 gain/bias。</translation>
    </message>
    <message>
        <source>Gain/bias auto-filled from %1 (editable).</source>
        <translation>已从 %1 自动填充 gain/bias（可手动修改）。</translation>
    </message>
    <message>
        <source>Uses manual gain/bias (the %1 metadata remains available for other bands).</source>
        <translation>使用手动 gain/bias（元数据 %1 仍可用于其他波段）。</translation>
    </message>
</context>
<context>
    <name>BandMathDialog</name>
    <message>
        <source>No raster layer selected.</source>
        <translation>未选择栅格图层。</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Valid bands (%1): %2</source>
        <translation>有效波段 (%1 个)：%2</translation>
    </message>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layers for the band math operation.</source>
        <translation>选择待参与波段运算的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Available Variables</source>
        <translation>可用变量</translation>
    </message>
    <message>
        <source>Mathematical Expression</source>
        <translation>数学表达式</translation>
    </message>
    <message>
        <source>e.g. (b1 - b2) / (b1 + b2) or b1 * 0.0001</source>
        <translation>例如：(b1 - b2) / (b1 + b2) 或 b1 * 0.0001</translation>
    </message>
    <message>
        <source>Band math expression. Bands are written b1, b2, ... (starting at 1).
Examples: (b1 - b2) / (b1 + b2); b1 * 0.0001; sqrt(b1*b1 + b2*b2); b1 &gt; 0.4 ? 1 : 0
Supports: + - * /, parentheses, comparisons (&lt; &gt; &lt;= &gt;= == !=), logic (&amp;&amp; ||),
ternary conditionals (b1 &gt; x ? true : false) and math functions (sin/cos/exp/ln/sqrt/abs/pow/min/max/pi...)</source>
        <translation>波段运算表达式。波段写作 b1, b2…（从 1 起）。
示例：(b1 - b2) / (b1 + b2)；b1 * 0.0001；sqrt(b1*b1 + b2*b2)；b1 &gt; 0.4 ? 1 : 0
支持：+ - * /、括号、比较(&lt; &gt; &lt;= &gt;= == !=)、逻辑(&amp;&amp; ||)、
三元条件(b1 &gt; x ? true : false)、数学函数(sin/cos/exp/ln/sqrt/abs/pow/min/max/pi…)</translation>
    </message>
    <message>
        <source>Formula</source>
        <translation>运算公式</translation>
    </message>
    <message>
        <source>Tip: the common vegetation index NDVI ≈ (b_nir − b_red) / (b_nir + b_red); avoid division by zero.</source>
        <translation>提示：常用植被指数 NDVI ≈ (b_nir − b_red) / (b_nir + b_red)；请注意避免除以零。</translation>
    </message>
    <message>
        <source>Enter a mathematical expression.</source>
        <translation>请输入数学表达式。</translation>
    </message>
    <message>
        <source>Band Math</source>
        <translation>波段运算</translation>
    </message>
</context>
<context>
    <name>BandRatioDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layer for band math.</source>
        <translation>选择待执行波段运算的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Operation Parameters</source>
        <translation>运算参数</translation>
    </message>
    <message>
        <source>Band Ratio</source>
        <translation>波段比值 (Band Ratio)</translation>
    </message>
    <message>
        <source>IHS Color Transform</source>
        <translation>IHS 颜色变换</translation>
    </message>
    <message>
        <source>• Band ratio: numerator band ÷ denominator band
• IHS transform: converts the three RGB bands into intensity, hue and saturation</source>
        <translation>• 波段比值：分子波段 ÷ 分母波段
• IHS 变换：RGB 三波段转换为强度 (Intensity)、色调 (Hue)、饱和度 (Saturation)</translation>
    </message>
    <message>
        <source>Operation Mode</source>
        <translation>运算模式</translation>
    </message>
    <message>
        <source>Numerator Band</source>
        <translation>分子波段</translation>
    </message>
    <message>
        <source>Numerator band of the ratio.</source>
        <translation>比值运算分子波段。</translation>
    </message>
    <message>
        <source>Denominator Band</source>
        <translation>分母波段</translation>
    </message>
    <message>
        <source>Denominator band of the ratio (must not be all zeros).</source>
        <translation>比值运算分母波段（请勿全为 0）。</translation>
    </message>
    <message>
        <source>Red Band (R)</source>
        <translation>红光波段 R</translation>
    </message>
    <message>
        <source>Red component band of the IHS transform.</source>
        <translation>IHS 变换红色分量波段。</translation>
    </message>
    <message>
        <source>Green Band (G)</source>
        <translation>绿光波段 G</translation>
    </message>
    <message>
        <source>Green component band of the IHS transform.</source>
        <translation>IHS 变换绿色分量波段。</translation>
    </message>
    <message>
        <source>Blue Band (B)</source>
        <translation>蓝光波段 B</translation>
    </message>
    <message>
        <source>Blue component band of the IHS transform.</source>
        <translation>IHS 变换蓝色分量波段。</translation>
    </message>
    <message>
        <source>Select two different valid bands for the band ratio.</source>
        <translation>请为波段比值选择两个不同的有效波段。</translation>
    </message>
    <message>
        <source>Select valid RGB bands for the IHS transform.</source>
        <translation>请为 IHS 变换选择有效的 RGB 波段。</translation>
    </message>
    <message>
        <source>Band Ratio and IHS Transform</source>
        <translation>波段比值与 IHS 变换</translation>
    </message>
</context>
<context>
    <name>BandRoleCombo</name>
    <message>
        <source>Automatic (by product semantic role)</source>
        <translation>自动（按产品语义角色）</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Band %1 (%2)</source>
        <translation>波段 %1 (%2)</translation>
    </message>
</context>
<context>
    <name>BatchProcessingDialog</name>
    <message>
        <source>Batch Processing</source>
        <translation>批量处理</translation>
    </message>
    <message>
        <source>Batch Processing Pipeline Guide</source>
        <translation>批处理流程指引</translation>
    </message>
    <message>
        <source>Pick an algorithm → add files to process → set the output directory → start the batch task.</source>
        <translation>选择算法 → 添加待处理文件 → 设置输出目录 → 启动批量处理任务。</translation>
    </message>
    <message>
        <source>Algorithm Selection</source>
        <translation>算法选择</translation>
    </message>
    <message>
        <source>Choose the remote-sensing or geoprocessing algorithm to batch-run. Validate it on a single file in the toolbox first.</source>
        <translation>选择要批量运行的遥感或地理处理算法。建议先在工具箱对单个文件验证效果。</translation>
    </message>
    <message>
        <source>Processing Algorithms</source>
        <translation>处理算法</translation>
    </message>
    <message>
        <source>Algorithm Parameter Overrides (optional)</source>
        <translation>算法参数覆盖 (可选)</translation>
    </message>
    <message>
        <source>Overrides the algorithm parameters used by the batch run (inputs and outputs are decided by the file list to process).</source>
        <translation>覆盖批量运行所用的算法参数（输入与输出由待处理文件列表自动决定）。</translation>
    </message>
    <message>
        <source>Input File List</source>
        <translation>输入文件列表</translation>
    </message>
    <message>
        <source>List of files to process.</source>
        <translation>待处理的文件列表。</translation>
    </message>
    <message>
        <source>Add Files...</source>
        <translation>添加文件…</translation>
    </message>
    <message>
        <source>Remove Selected</source>
        <translation>移除选中</translation>
    </message>
    <message>
        <source>Output Directory Settings</source>
        <translation>输出目录设置</translation>
    </message>
    <message>
        <source>Choose the batch result save directory...</source>
        <translation>选择批量处理结果保存目录…</translation>
    </message>
    <message>
        <source>All batch run result files are written to this directory.</source>
        <translation>所有批量运行的结果文件均写入此目录。</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Output Directory</source>
        <translation>输出目录</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Run Batch</source>
        <translation>运行批量</translation>
    </message>
    <message>
        <source>Runs the processing tasks one by one in list order. Do not force-close the dialog while a batch is running.</source>
        <translation>按列表顺序逐个执行处理任务。批量运行中请勿强制关闭对话框。</translation>
    </message>
    <message>
        <source>Select Input Files</source>
        <translation>选择输入文件</translation>
    </message>
    <message>
        <source>Remote-Sensing Rasters and Vectors (*.tif *.tiff *.img *.shp *.gpkg);;All Files (*)</source>
        <translation>遥感栅格与矢量 (*.tif *.tiff *.img *.shp *.gpkg);;所有文件 (*)</translation>
    </message>
    <message>
        <source>%1 input files selected</source>
        <translation>已选择 %1 个输入文件</translation>
    </message>
    <message>
        <source>Select Output Directory</source>
        <translation>选择输出目录</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>取消中…</translation>
    </message>
    <message>
        <source>Add the input files to process first.</source>
        <translation>请先添加待处理的输入文件。</translation>
    </message>
    <message>
        <source>Specify the output directory.</source>
        <translation>请指定输出保存目录。</translation>
    </message>
    <message>
        <source>Failed to create the output directory:
%1</source>
        <translation>创建输出目录失败：
%1</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Batch processing was cancelled by the user</source>
        <translation>批量处理已被用户取消</translation>
    </message>
    <message>
        <source>Processing %1...</source>
        <translation>正在处理 %1…</translation>
    </message>
    <message>
        <source>Batch: %1</source>
        <translation>批量: %1</translation>
    </message>
    <message>
        <source>%1: task submission failed</source>
        <translation>%1: 任务提交失败</translation>
    </message>
    <message>
        <source>Invalid Parameters</source>
        <translation>参数无效</translation>
    </message>
    <message>
        <source>%1: %2</source>
        <translation>%1：%2</translation>
    </message>
    <message>
        <source>Batch processing cancelled: %1 succeeded, %2 failed</source>
        <translation>批量处理已取消：%1 个成功，%2 个失败</translation>
    </message>
    <message>
        <source>Batch processing finished: %1 succeeded, %2 failed</source>
        <translation>批量处理完成：%1 个成功，%2 个失败</translation>
    </message>
    <message>
        <source>Batch processing finished (with some failures):
%1 succeeded, %2 failed</source>
        <translation>批量处理完成（存在部分失败）：
%1 个成功，%2 个失败</translation>
    </message>
    <message>
        <source>
... and %1 more errors (see the system log)</source>
        <translation>
… 及其余 %1 个错误（详见系统日志）</translation>
    </message>
    <message>
        <source>Batch processing finished:
%1 files processed successfully</source>
        <translation>批量处理完成：
成功处理 %1 个文件</translation>
    </message>
    <message>
        <source>Selected: %1 (RS, default parameters)</source>
        <translation>Selected: %1 (RS, 默认参数)</translation>
    </message>
    <message>
        <source>Selected: %1</source>
        <translation>已选：%1</translation>
    </message>
    <message>
        <source>RS operator not found: %1</source>
        <translation>未找到 RS 算子：%1</translation>
    </message>
    <message>
        <source>RS operator returned no output</source>
        <translation>RS 算子未返回输出</translation>
    </message>
    <message>
        <source>Algorithm not found: %1</source>
        <translation>未找到算法：%1</translation>
    </message>
    <message>
        <source>Failed to create algorithm instance: %1</source>
        <translation>创建算法实例失败：%1</translation>
    </message>
    <message>
        <source>Algorithm returned no results</source>
        <translation>算法未返回结果</translation>
    </message>
    <message>
        <source>%1 files selected</source>
        <translation>已选择 %1 个文件</translation>
    </message>
</context>
<context>
    <name>ChangeDetectionDialog</name>
    <message>
        <source>Change Detection</source>
        <translation>变化检测</translation>
    </message>
    <message>
        <source>Two-Date Input Data</source>
        <translation>双时相输入数据</translation>
    </message>
    <message>
        <source>The two epochs must be precisely co-registered and radiometrically normalized.</source>
        <translation>前后时相须完成高精度几何配准与辐射归一化。</translation>
    </message>
    <message>
        <source>Raster image of the earlier (pre-change) epoch.</source>
        <translation>变化前（较早）时相栅格影像。</translation>
    </message>
    <message>
        <source>Raster image of the later (post-change) epoch.</source>
        <translation>变化后（较晚）时相栅格影像。</translation>
    </message>
    <message>
        <source>Band of the earlier image used in the comparison.</source>
        <translation>前期影像参与比较的波段。</translation>
    </message>
    <message>
        <source>Band of the later image used in the comparison.</source>
        <translation>后期影像参与比较的波段。</translation>
    </message>
    <message>
        <source>Earlier Image</source>
        <translation>前期影像</translation>
    </message>
    <message>
        <source>Earlier Band</source>
        <translation>前期波段</translation>
    </message>
    <message>
        <source>Later Image</source>
        <translation>后期影像</translation>
    </message>
    <message>
        <source>Later Band</source>
        <translation>后期波段</translation>
    </message>
    <message>
        <source>Dual-View Comparison...</source>
        <translation>双视图对比…</translation>
    </message>
    <message>
        <source>Opens the side-by-side comparison view (divider / swipe + blink) to visually inspect registration and change.</source>
        <translation>打开并排对比视图（分割线/Swipe + 闪烁），目视检查配准与变化。</translation>
    </message>
    <message>
        <source>Detection Method and Mask Options</source>
        <translation>检测方法与掩膜选项</translation>
    </message>
    <message>
        <source>Supports differencing, normalized differencing, ratioing, CVA change vector analysis and MAD multivariate change detection.</source>
        <translation>支持差值法、归一化差值法、比值法、CVA 变化向量分析与 MAD 多变量变化检测。</translation>
    </message>
    <message>
        <source>Difference</source>
        <translation>差值 Difference</translation>
    </message>
    <message>
        <source>Normalized Difference</source>
        <translation>归一化差值</translation>
    </message>
    <message>
        <source>Ratio</source>
        <translation>比值 Ratio</translation>
    </message>
    <message>
        <source>Change Vector Analysis (CVA)</source>
        <translation>变化向量分析 CVA</translation>
    </message>
    <message>
        <source>Multivariate Alteration Detection (MAD)</source>
        <translation>多变量变化检测 MAD</translation>
    </message>
    <message>
        <source>Change Mask (manual threshold)</source>
        <translation>变化掩膜（手动阈值）</translation>
    </message>
    <message>
        <source>• Difference: later − earlier
• Normalized difference: (later − earlier)/(later + earlier)
• Ratio: later / earlier
• CVA: multiband change vector magnitude (all bands)
• MAD: multivariate alteration detection (canonical correlation analysis)
• Mask: |difference| ≥ threshold</source>
        <translation>• 差值：后−前
• 归一化差值：(后−前)/(后+前)
• 比值：后/前
• CVA：多波段变化向量幅值（用全部波段）
• MAD：多变量变化检测（CCA 典型相关分析）
• 掩膜：|差值|≥阈值</translation>
    </message>
    <message>
        <source>Change Algorithm</source>
        <translation>变化算法</translation>
    </message>
    <message>
        <source>Also output a binary change mask</source>
        <translation>同时输出二值变化掩膜</translation>
    </message>
    <message>
        <source>Besides the method raster, also outputs a 0/1 change mask (with threshold strategy, morphological cleanup and a minimum mapping unit).</source>
        <translation>除方法栅格外，再输出 0/1 变化掩膜（可配阈值策略、形态学清理与最小制图单元）。</translation>
    </message>
    <message>
        <source>Manual Threshold</source>
        <translation>手动阈值</translation>
    </message>
    <message>
        <source>Otsu Threshold</source>
        <translation>Otsu 大津法</translation>
    </message>
    <message>
        <source>Percentile Threshold</source>
        <translation>分位数阈值</translation>
    </message>
    <message>
        <source>Statistical Threshold (mean + kσ)</source>
        <translation>统计阈值（均值+kσ）</translation>
    </message>
    <message>
        <source>Threshold extraction strategy for the binary change mask.</source>
        <translation>二值变化掩膜阈值提取策略。</translation>
    </message>
    <message>
        <source>Threshold Strategy</source>
        <translation>阈值策略</translation>
    </message>
    <message>
        <source>Threshold</source>
        <translation>阈值</translation>
    </message>
    <message>
        <source>Specify an absolute change threshold manually.</source>
        <translation>手动指定绝对变化阈值。</translation>
    </message>
    <message>
        <source>Extract change areas by change-magnitude percentile (0–100).</source>
        <translation>按变化强度百分位提取变化区域（0~100）。</translation>
    </message>
    <message>
        <source>Percentile Value (%)</source>
        <translation>分位数值 (%)</translation>
    </message>
    <message>
        <source>Statistical threshold = change mean + k × std dev.</source>
        <translation>统计阈值 = 变化均值 + k × 标准差。</translation>
    </message>
    <message>
        <source>k (std-dev multiplier)</source>
        <translation>k (标准差倍数)</translation>
    </message>
    <message>
        <source>No Operation</source>
        <translation>无操作</translation>
    </message>
    <message>
        <source>Morphological Erosion</source>
        <translation>形态学腐蚀</translation>
    </message>
    <message>
        <source>Morphological Dilation</source>
        <translation>形态学膨胀</translation>
    </message>
    <message>
        <source>Opening (remove isolated patches)</source>
        <translation>开运算 (去孤立斑)</translation>
    </message>
    <message>
        <source>Closing (fill holes)</source>
        <translation>闭运算 (填孔洞)</translation>
    </message>
    <message>
        <source>Morphological post-processing for the binary change mask.</source>
        <translation>二值变化掩膜形态学后处理操作。</translation>
    </message>
    <message>
        <source>Morphological Cleanup</source>
        <translation>形态学清理</translation>
    </message>
    <message>
        <source>Morphological operation iterations</source>
        <translation>形态学运算迭代次数</translation>
    </message>
    <message>
        <source>Iterations</source>
        <translation>迭代次数</translation>
    </message>
    <message>
        <source>Minimum mapping unit (pixels): removes small connected patches below this area; 0 = off.</source>
        <translation>最小制图单元（像元）：移除小于该面积的碎小连通斑块；0 = 关闭。</translation>
    </message>
    <message>
        <source>Minimum Mapping Unit (pixels)</source>
        <translation>最小制图单元 (像元)</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Select the earlier and later epoch images first.</source>
        <translation>请先选择前后时相影像。</translation>
    </message>
    <message>
        <source>Specify the output file.</source>
        <translation>请指定输出文件。</translation>
    </message>
    <message>
        <source>Select the earlier and later images.</source>
        <translation>请选择前期与后期影像。</translation>
    </message>
    <message>
        <source>The earlier or later image is invalid.</source>
        <translation>前期或后期影像无效。</translation>
    </message>
    <message>
        <source>The pixel grids of the two images are incompatible; per-pixel comparison is not possible:
%1</source>
        <translation>两张影像的像元网格不兼容，无法逐像元比较：
%1</translation>
    </message>
    <message>
        <source>The earlier image is invalid.</source>
        <translation>前期影像无效。</translation>
    </message>
    <message>
        <source>The later image is invalid.</source>
        <translation>后期影像无效。</translation>
    </message>
    <message>
        <source>Running...</source>
        <translation>正在运行…</translation>
    </message>
    <message>
        <source>Change mean %1, std dev %2</source>
        <translation>变化均值 %1，标准差 %2</translation>
    </message>
    <message>
        <source>; changed pixels %1 / %2 (%3%)</source>
        <translation>；变化像元 %1 / %2（%3%）</translation>
    </message>
</context>
<context>
    <name>ComparisonDialog</name>
    <message>
        <source>Layer Comparison</source>
        <translation>图层对比</translation>
    </message>
    <message>
        <source>Select the left/right comparison layers and press 'Load Comparison'; supports a draggable swipe divider, side-by-side and quick blink toggling.</source>
        <translation>选择左右对比图层后点击「加载对比」，支持分割线卷帘 (Swipe)、并排对比与快速闪烁切换。</translation>
    </message>
    <message>
        <source>Comparison Layer Configuration</source>
        <translation>对比图层配置</translation>
    </message>
    <message>
        <source>Left layer (earlier epoch / base)</source>
        <translation>左侧图层 (前时相/基准)</translation>
    </message>
    <message>
        <source>The left viewport shows the base or earlier-epoch raster.</source>
        <translation>左侧视口显示的基准或较早时相栅格。</translation>
    </message>
    <message>
        <source>Right layer (later epoch / target)</source>
        <translation>右侧图层 (后时相/目标)</translation>
    </message>
    <message>
        <source>The right viewport shows the target or later-epoch raster.</source>
        <translation>右侧视口显示的目标或较晚时相栅格。</translation>
    </message>
    <message>
        <source>Load Comparison</source>
        <translation>加载对比</translation>
    </message>
    <message>
        <source>Renders the selected left/right layers into the comparison view below.</source>
        <translation>将所选左右图层渲染加载到下方对比视图中。</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Select a valid raster layer on each side.</source>
        <translation>请在左右两侧各选择一个有效的栅格图层。</translation>
    </message>
    <message>
        <source>preview unavailable</source>
        <translation>预览不可用</translation>
    </message>
</context>
<context>
    <name>ComparisonWidget</name>
    <message>
        <source>Mode:</source>
        <translation>模式：</translation>
    </message>
    <message>
        <source>Comparison mode: swipe (side-by-side slider) or blink (auto toggle).</source>
        <translation>对比模式：分屏（左右滑动对比）或闪烁（自动切换）。</translation>
    </message>
    <message>
        <source>Split Screen</source>
        <translation>分屏对比</translation>
    </message>
    <message>
        <source>Flicker</source>
        <translation>闪烁对比</translation>
    </message>
    <message>
        <source>Split:</source>
        <translation>分屏位置：</translation>
    </message>
    <message>
        <source>Split position (0 = all left, 100 = all right).</source>
        <translation>分屏位置（0=全左，100=全右）。</translation>
    </message>
    <message>
        <source>Start Flicker</source>
        <translation>开始闪烁</translation>
    </message>
    <message>
        <source>Starts/stops blink comparison (auto-toggles between the two layers).</source>
        <translation>开始/停止闪烁对比（自动在两个图层间切换）。</translation>
    </message>
    <message>
        <source>Load two layers to compare</source>
        <translation>请加载两个图层进行对比</translation>
    </message>
    <message>
        <source>Left</source>
        <translation>左</translation>
    </message>
    <message>
        <source>Right</source>
        <translation>右</translation>
    </message>
    <message>
        <source>Layer A</source>
        <translation>图层 A</translation>
    </message>
    <message>
        <source>Layer B</source>
        <translation>图层 B</translation>
    </message>
    <message>
        <source>Stop Flicker</source>
        <translation>停止闪烁</translation>
    </message>
</context>
<context>
    <name>ContrastStretchDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layer for contrast stretching.</source>
        <translation>选择待执行对比度拉伸的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Preset Algorithms and Export</source>
        <translation>预设算法与导出</translation>
    </message>
    <message>
        <source>Custom Photoshop Levels</source>
        <translation>Photoshop 自定义色阶</translation>
    </message>
    <message>
        <source>Linear Stretch (Min-Max)</source>
        <translation>线性拉伸 (Min-Max)</translation>
    </message>
    <message>
        <source>Percent Clip Stretch</source>
        <translation>百分比裁剪拉伸</translation>
    </message>
    <message>
        <source>Std-Dev Stretch</source>
        <translation>标准差拉伸</translation>
    </message>
    <message>
        <source>Histogram Equalization</source>
        <translation>直方图均衡化</translation>
    </message>
    <message>
        <source>Stretch method:
• Photoshop Levels: interactively adjust shadows, highlights and the gamma midtone
• Linear: min–max
• Percent clip: clip both tails, then stretch
• Std dev: mean±K×std dev
• Histogram equalization: enhances global contrast</source>
        <translation>拉伸方法：
• Photoshop 色阶：交互调节阴影、高光与 Gamma 中间调
• 线性：最小–最大
• 百分比裁剪：两端裁剪后再拉伸
• 标准差：均值±K×标准差
• 直方图均衡化：增强全局对比</translation>
    </message>
    <message>
        <source>Preset Method</source>
        <translation>预设方法</translation>
    </message>
    <message>
        <source>Clip Ratio</source>
        <translation>裁剪比例</translation>
    </message>
    <message>
        <source>Discard this fraction of pixels at both tails before stretching; 1–2% is typical.</source>
        <translation>两端各舍弃该比例像元后再拉伸。常用 1–2%。</translation>
    </message>
    <message>
        <source>Std-Dev Multiplier K</source>
        <translation>标准差倍数 K</translation>
    </message>
    <message>
        <source>Stretches to mean±K·σ; 2 is typical.</source>
        <translation>拉伸到 mean±K·σ。常用 2。</translation>
    </message>
    <message>
        <source>Custom levels need at least two control points; use a preset method instead.</source>
        <translation>自定义色阶至少需要两个控制点，请改用预设方法。</translation>
    </message>
    <message>
        <source>Contrast Stretch and Levels Adjustment</source>
        <translation>对比度拉伸与色阶调节</translation>
    </message>
</context>
<context>
    <name>CrossSectionWidget</name>
    <message>
        <source>Click two points on map to create cross-section</source>
        <translation>在地图上点击两个点以创建剖面</translation>
    </message>
    <message>
        <source>Select a raster layer first</source>
        <translation>请先选择一个栅格图层</translation>
    </message>
    <message>
        <source>Cross-Section — %1</source>
        <translation>地形剖面 — %1</translation>
    </message>
    <message>
        <source>Distance</source>
        <translation>距离</translation>
    </message>
</context>
<context>
    <name>CrsPresetDialog</name>
    <message>
        <source>Select CRS Preset</source>
        <translation>选择坐标系预设</translation>
    </message>
    <message>
        <source>CRS Preset Search</source>
        <translation>坐标系预设检索</translation>
    </message>
    <message>
        <source>Quick Search</source>
        <translation>快速检索</translation>
    </message>
    <message>
        <source>Filter by name or EPSG code (e.g. WGS 84, 3857, CGCS2000)...</source>
        <translation>按名称或 EPSG 代码过滤（如 WGS 84、3857、CGCS2000 等）…</translation>
    </message>
    <message>
        <source>Quickly filter the preset list by CRS name or EPSG code.</source>
        <translation>按坐标系名称或 EPSG 代码快速过滤预设列表。</translation>
    </message>
    <message>
        <source>Common CRS Presets</source>
        <translation>常用坐标系预设</translation>
    </message>
    <message>
        <source>Coordinate System Name</source>
        <translation>坐标系名称</translation>
    </message>
    <message>
        <source>EPSG</source>
        <translation>EPSG</translation>
    </message>
    <message>
        <source>Grouped list of common CRSs. Selecting shows details on the right; double-click applies directly.</source>
        <translation>常用坐标系分组列表。选中后右侧显示详情，双击可直接应用。</translation>
    </message>
    <message>
        <source>Coordinate System Details</source>
        <translation>坐标系详细信息</translation>
    </message>
    <message>
        <source>EPSG code</source>
        <translation>EPSG 代码</translation>
    </message>
    <message>
        <source>Classification</source>
        <translation>分类</translation>
    </message>
    <message>
        <source>Detailed Description</source>
        <translation>详细描述</translation>
    </message>
    <message>
        <source>WKT Definition</source>
        <translation>WKT 定义</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>View CRS preset descriptions and help.</source>
        <translation>查看坐标系预设说明与帮助。</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Recently Used</source>
        <translation>最近使用</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>N/A</source>
        <translation>无</translation>
    </message>
</context>
<context>
    <name>CrsSelector</name>
    <message>
        <source>…</source>
        <translation>…</translation>
    </message>
    <message>
        <source>Choose the CRS from the projection selector.</source>
        <translation>从投影选择器选择 CRS。</translation>
    </message>
</context>
<context>
    <name>ExtractBandDialog</name>
    <message>
        <source>Inputs and Band Selection</source>
        <translation>输入与波段选择</translation>
    </message>
    <message>
        <source>A multiband raster layer from the project.</source>
        <translation>工程中的多波段栅格图层。</translation>
    </message>
    <message>
        <source>Raster Layer</source>
        <translation>栅格图层</translation>
    </message>
    <message>
        <source>Chooses the target band to extract and export separately.</source>
        <translation>选择待抽取并单独导出的目标波段。</translation>
    </message>
    <message>
        <source>Target Band</source>
        <translation>目标波段</translation>
    </message>
    <message>
        <source>Tip: extracts a single band from a multiband raster and saves it as a standalone single-band GeoTIFF.</source>
        <translation>提示：从多波段栅格中抽取单一波段并另存为独立的单波段 GeoTIFF 影像。</translation>
    </message>
    <message>
        <source>Select a valid raster layer.</source>
        <translation>请选择有效的栅格图层。</translation>
    </message>
    <message>
        <source>Select the target band to extract.</source>
        <translation>请选择要提取的目标波段。</translation>
    </message>
    <message>
        <source>_band%1.tif</source>
        <translation>_band%1.tif</translation>
    </message>
    <message>
        <source>Select a raster layer.</source>
        <translation>请选择栅格图层。</translation>
    </message>
    <message>
        <source>Extract Band</source>
        <translation>提取波段</translation>
    </message>
</context>
<context>
    <name>FusionDialog</name>
    <message>
        <source>Inputs and Fusion Method</source>
        <translation>输入与融合方法</translation>
    </message>
    <message>
        <source>The panchromatic and multispectral images must cover the same area and be precisely co-registered.</source>
        <translation>全色与多光谱影像须空间覆盖一致并已完成高精度几何配准。</translation>
    </message>
    <message>
        <source>High-resolution single-band panchromatic raster.</source>
        <translation>全色高分辨率单波段栅格。</translation>
    </message>
    <message>
        <source>Panchromatic Image (high resolution)</source>
        <translation>全色影像 (高分)</translation>
    </message>
    <message>
        <source>Low-resolution multiband multispectral image.</source>
        <translation>多光谱低分辨率多波段影像。</translation>
    </message>
    <message>
        <source>Multispectral Image (lower resolution)</source>
        <translation>多光谱影像 (低分)</translation>
    </message>
    <message>
        <source>Linear Weighting</source>
        <translation>线性加权</translation>
    </message>
    <message>
        <source>Brovey Transform</source>
        <translation>Brovey 变换</translation>
    </message>
    <message>
        <source>IHS Fusion (RGB required)</source>
        <translation>IHS 融合 (需 RGB)</translation>
    </message>
    <message>
        <source>PCA Fusion</source>
        <translation>PCA 融合</translation>
    </message>
    <message>
        <source>OTB BundleToPerfectSensor</source>
        <translation>OTB BundleToPerfectSensor</translation>
    </message>
    <message>
        <source>GDAL Pansharpen</source>
        <translation>GDAL 全色锐化</translation>
    </message>
    <message>
        <source>Built-in methods: Linear / Brovey / IHS / PCA; external tools: OTB / GDAL pansharpening.</source>
        <translation>内置方法：Linear / Brovey / IHS / PCA；外部工具：OTB / GDAL 全色锐化。</translation>
    </message>
    <message>
        <source>Fusion Method</source>
        <translation>融合方法</translation>
    </message>
    <message>
        <source>Pan Weight</source>
        <translation>全色权重</translation>
    </message>
    <message>
        <source>Panchromatic band share in linear fusion (0.0–1.0).</source>
        <translation>线性融合中全色波段占比 (0.0~1.0)。</translation>
    </message>
    <message>
        <source>Per-Band Weights</source>
        <translation>分波段权重</translation>
    </message>
    <message>
        <source>Red band used by the IHS transform.</source>
        <translation>IHS 变换对应的红波段。</translation>
    </message>
    <message>
        <source>Green band used by the IHS transform.</source>
        <translation>IHS 变换对应的绿波段。</translation>
    </message>
    <message>
        <source>Blue band used by the IHS transform.</source>
        <translation>IHS 变换对应的蓝波段。</translation>
    </message>
    <message>
        <source>Red Band (R)</source>
        <translation>红光波段 R</translation>
    </message>
    <message>
        <source>Green Band (G)</source>
        <translation>绿光波段 G</translation>
    </message>
    <message>
        <source>Blue Band (B)</source>
        <translation>蓝光波段 B</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Select valid panchromatic and multispectral raster layers.</source>
        <translation>请选择有效的全色和多光谱栅格图层。</translation>
    </message>
    <message>
        <source>Select both the panchromatic and multispectral layers.</source>
        <translation>请同时选择全色和多光谱图层。</translation>
    </message>
    <message>
        <source>The panchromatic and multispectral rasters are not co-registered:
%1</source>
        <translation>全色与多光谱栅格像元网格未配准：
%1</translation>
    </message>
    <message>
        <source>IHS fusion requires valid red, green and blue bands for the multispectral image.</source>
        <translation>IHS 融合需要为多光谱影像指定有效的红、绿、蓝波段。</translation>
    </message>
    <message>
        <source>Select Panchromatic Image</source>
        <translation>选择全色影像</translation>
    </message>
    <message>
        <source>GeoTIFF Raster (*.tif *.tiff);;All Files (*)</source>
        <translation>GeoTIFF 栅格 (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Select Multispectral Image</source>
        <translation>选择多光谱影像</translation>
    </message>
    <message>
        <source>Image Fusion</source>
        <translation>影像融合</translation>
    </message>
</context>
<context>
    <name>GuidedWorkflowWidget</name>
    <message>
        <source>&lt;b&gt;Guided Workflow&lt;/b&gt;</source>
        <translation>&lt;b&gt;引导式工作流&lt;/b&gt;</translation>
    </message>
    <message>
        <source>Select workflow:</source>
        <translation>选择工作流：</translation>
    </message>
    <message>
        <source>Choose a guided workflow.</source>
        <translation>选择一个引导式工作流。</translation>
    </message>
    <message>
        <source>Start Workflow</source>
        <translation>开始工作流</translation>
    </message>
    <message>
        <source>Starts the selected workflow.</source>
        <translation>开始所选工作流。</translation>
    </message>
    <message>
        <source>Previous Step</source>
        <translation>上一步</translation>
    </message>
    <message>
        <source>Returns to the previous step.</source>
        <translation>返回上一个步骤。</translation>
    </message>
    <message>
        <source>Run This Step</source>
        <translation>执行此步</translation>
    </message>
    <message>
        <source>Runs the current step's action.</source>
        <translation>执行当前步骤的操作。</translation>
    </message>
    <message>
        <source>Next Step</source>
        <translation>下一步</translation>
    </message>
    <message>
        <source>Continue to the next step after finishing the current one.</source>
        <translation>完成当前步骤后进入下一步。</translation>
    </message>
    <message>
        <source>&lt;b&gt;Workflow Complete!&lt;/b&gt;</source>
        <translation>&lt;b&gt;工作流完成！&lt;/b&gt;</translation>
    </message>
    <message>
        <source>&lt;p&gt;Congratulations! You have completed the &lt;b&gt;%1&lt;/b&gt; workflow.&lt;/p&gt;&lt;p&gt;You can now try other workflows or experiment with different parameters.&lt;/p&gt;</source>
        <translation>&lt;p&gt;恭喜！你已完成&lt;b&gt;%1&lt;/b&gt;工作流。&lt;/p&gt;&lt;p&gt;现在可以尝试其他工作流，或调整参数反复练习。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>&lt;b&gt;Step %1/%2: %3&lt;/b&gt;</source>
        <translation>&lt;b&gt;第 %1/%2 步：%3&lt;/b&gt;</translation>
    </message>
    <message>
        <source>Spectral Analysis</source>
        <translation>光谱分析</translation>
    </message>
    <message>
        <source>Learn to analyze spectral characteristics of different land cover types using vegetation indices and band math.</source>
        <translation>学习使用植被指数与波段运算分析不同地物类型的光谱特征。</translation>
    </message>
    <message>
        <source>Load Sample Data</source>
        <translation>加载示例数据</translation>
    </message>
    <message>
        <source>Load the sample Landsat image</source>
        <translation>加载示例 Landsat 影像</translation>
    </message>
    <message>
        <source>&lt;p&gt;First, load the sample multi-band Landsat image:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;File &gt; Add Raster Layer...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Navigate to the &lt;code&gt;data/samples/&lt;/code&gt; directory&lt;/li&gt;&lt;li&gt;Select &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Open&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;This is a 7-band Landsat-like image with vegetation, water, urban, and bare soil areas.&lt;/p&gt;</source>
        <translation>&lt;p&gt;首先加载示例多波段 Landsat 影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;文件 &gt; 添加栅格图层...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;进入 &lt;code&gt;data/samples/&lt;/code&gt; 目录&lt;/li&gt;&lt;li&gt;选择 &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;打开&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;这是一幅 7 波段类 Landsat 影像，包含植被、水体、城镇和裸土区域。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>The image should appear in the map canvas and layer panel.</source>
        <translation>影像应显示在地图画布和图层面板中。</translation>
    </message>
    <message>
        <source>Examine Spectral Profiles</source>
        <translation>观察光谱剖面</translation>
    </message>
    <message>
        <source>Click on different land cover types to see their spectral signatures</source>
        <translation>点击不同地物类型查看其光谱特征曲线</translation>
    </message>
    <message>
        <source>&lt;p&gt;Use the Identify tool to examine spectral characteristics:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;View &gt; Identify&lt;/b&gt; (or press Ctrl+Shift+I)&lt;/li&gt;&lt;li&gt;Click on different areas of the image:&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;Dark area&lt;/b&gt; (bottom) — Water&lt;/li&gt;&lt;li&gt;&lt;b&gt;Green area&lt;/b&gt; (middle) — Vegetation&lt;/li&gt;&lt;li&gt;&lt;b&gt;Bright area&lt;/b&gt; (top-left) — Urban&lt;/li&gt;&lt;li&gt;&lt;b&gt;Brown area&lt;/b&gt; (right) — Bare soil&lt;/li&gt;&lt;/ul&gt;&lt;li&gt;Observe the spectral profile in the Identify Results panel&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Note how vegetation has high NIR (band 5) reflectance!&lt;/p&gt;</source>
        <translation>&lt;p&gt;使用识别工具观察光谱特征：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;视图 &gt; 识别&lt;/b&gt;（或按 Ctrl+Shift+I）&lt;/li&gt;&lt;li&gt;点击影像不同区域：&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;深色区域&lt;/b&gt;（下方）— 水体&lt;/li&gt;&lt;li&gt;&lt;b&gt;绿色区域&lt;/b&gt;（中部）— 植被&lt;/li&gt;&lt;li&gt;&lt;b&gt;明亮区域&lt;/b&gt;（左上）— 城镇&lt;/li&gt;&lt;li&gt;&lt;b&gt;棕色区域&lt;/b&gt;（右侧）— 裸土&lt;/li&gt;&lt;/ul&gt;&lt;li&gt;在识别结果面板观察光谱剖面&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;注意植被在近红外（第 5 波段）具有高反射率！&lt;/p&gt;</translation>
    </message>
    <message>
        <source>You should see different spectral curves for different land cover types.</source>
        <translation>你应该能看到不同地物类型的光谱曲线各不相同。</translation>
    </message>
    <message>
        <source>Calculate NDVI</source>
        <translation>计算 NDVI</translation>
    </message>
    <message>
        <source>Compute the Normalized Difference Vegetation Index</source>
        <translation>计算归一化植被指数</translation>
    </message>
    <message>
        <source>&lt;p&gt;NDVI highlights vegetation:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Vegetation Index...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select &lt;b&gt;NDVI&lt;/b&gt; from the dropdown&lt;/li&gt;&lt;li&gt;Set Red band = &lt;b&gt;Band 4&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set NIR band = &lt;b&gt;Band 5&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set output file (e.g., &lt;code&gt;ndvi_result.tif&lt;/code&gt;)&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;NDVI = (NIR - Red) / (NIR + Red). Values range from -1 to 1.&lt;/p&gt;</source>
        <translation>&lt;p&gt;NDVI 用于突出植被：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 植被指数...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;在下拉框选择 &lt;b&gt;NDVI&lt;/b&gt;&lt;/li&gt;&lt;li&gt;红光波段设为&lt;b&gt;第 4 波段&lt;/b&gt;&lt;/li&gt;&lt;li&gt;近红外波段设为&lt;b&gt;第 5 波段&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置输出文件（如 &lt;code&gt;ndvi_result.tif&lt;/code&gt;）&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;NDVI = (NIR - Red) / (NIR + Red)，取值范围 -1 到 1。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>NDVI values: Vegetation &gt; 0.3, Water &lt; 0, Bare soil ≈ 0.</source>
        <translation>NDVI 参考值：植被 &gt; 0.3，水体 &lt; 0，裸土 ≈ 0。</translation>
    </message>
    <message>
        <source>Custom Band Ratio</source>
        <translation>自定义波段比值</translation>
    </message>
    <message>
        <source>Use Band Math to create a custom spectral index</source>
        <translation>用波段运算创建自定义光谱指数</translation>
    </message>
    <message>
        <source>&lt;p&gt;Try a band ratio (NIR/Red):&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Band Math...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Enter expression: &lt;code&gt;b5 / b4&lt;/code&gt;&lt;/li&gt;&lt;li&gt;Set output file (e.g., &lt;code&gt;band_ratio.tif&lt;/code&gt;)&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Band ratios can enhance differences between land cover types.&lt;/p&gt;</source>
        <translation>&lt;p&gt;尝试波段比值（NIR/Red）：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 波段运算...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;输入表达式：&lt;code&gt;b5 / b4&lt;/code&gt;&lt;/li&gt;&lt;li&gt;设置输出文件（如 &lt;code&gt;band_ratio.tif&lt;/code&gt;）&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;波段比值可以放大不同地物类型之间的差异。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>The ratio image should show vegetation areas with high values.</source>
        <translation>比值影像中植被区域应呈现高值。</translation>
    </message>
    <message>
        <source>Compare Results</source>
        <translation>对比结果</translation>
    </message>
    <message>
        <source>Use the comparison tool to view results side-by-side</source>
        <translation>使用对比工具并排查看结果</translation>
    </message>
    <message>
        <source>&lt;p&gt;Compare the original image with NDVI:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;View &gt; Compare Layers...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select the original image as left layer&lt;/li&gt;&lt;li&gt;Select NDVI result as right layer&lt;/li&gt;&lt;li&gt;Use &lt;b&gt;Split Screen&lt;/b&gt; mode to compare&lt;/li&gt;&lt;li&gt;Try &lt;b&gt;Flicker&lt;/b&gt; mode to see differences&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Can you identify which areas have the most vegetation?&lt;/p&gt;</source>
        <translation>&lt;p&gt;将原始影像与 NDVI 对比：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;视图 &gt; 图层对比...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;左侧图层选择原始影像&lt;/li&gt;&lt;li&gt;右侧图层选择 NDVI 结果&lt;/li&gt;&lt;li&gt;使用&lt;b&gt;分屏对比&lt;/b&gt;模式查看&lt;/li&gt;&lt;li&gt;试试&lt;b&gt;闪烁对比&lt;/b&gt;发现差异&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;你能找出植被最茂密的区域吗？&lt;/p&gt;</translation>
    </message>
    <message>
        <source>High NDVI values correspond to green vegetation areas.</source>
        <translation>高 NDVI 值对应绿色植被区域。</translation>
    </message>
    <message>
        <source>Image Enhancement</source>
        <translation>影像增强</translation>
    </message>
    <message>
        <source>Learn contrast enhancement and spatial filtering techniques.</source>
        <translation>学习对比度增强与空间滤波技术。</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load the sample Landsat image: &lt;code&gt;data/samples/landsat_sample.tif&lt;/code&gt;&lt;/p&gt;</source>
        <translation>&lt;p&gt;加载示例 Landsat 影像：&lt;code&gt;data/samples/landsat_sample.tif&lt;/code&gt;&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Image loaded in map canvas.</source>
        <translation>影像已加载到地图画布。</translation>
    </message>
    <message>
        <source>Contrast Stretch</source>
        <translation>对比度拉伸</translation>
    </message>
    <message>
        <source>&lt;p&gt;Enhance image contrast:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Enhancement &gt; Contrast Stretch...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Try different methods:&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;Linear Stretch&lt;/b&gt; — Simple min-max mapping&lt;/li&gt;&lt;li&gt;&lt;b&gt;Percent Clip&lt;/b&gt; — Remove 2% outliers&lt;/li&gt;&lt;li&gt;&lt;b&gt;StdDev Stretch&lt;/b&gt; — Mean ± 2σ&lt;/li&gt;&lt;li&gt;&lt;b&gt;Histogram Equalization&lt;/b&gt; — Uniform distribution&lt;/li&gt;&lt;/ul&gt;&lt;li&gt;Compare the results&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;增强影像对比度：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 增强 &gt; 对比度拉伸...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;尝试不同方法：&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;线性拉伸&lt;/b&gt; — 简单最小-最大映射&lt;/li&gt;&lt;li&gt;&lt;b&gt;百分比裁剪&lt;/b&gt; — 去除 2% 极端值&lt;/li&gt;&lt;li&gt;&lt;b&gt;标准差拉伸&lt;/b&gt; — 均值 ± 2σ&lt;/li&gt;&lt;li&gt;&lt;b&gt;直方图均衡化&lt;/b&gt; — 均匀分布&lt;/li&gt;&lt;/ul&gt;&lt;li&gt;对比各种结果&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Enhanced images show more detail.</source>
        <translation>增强后的影像能显示更多细节。</translation>
    </message>
    <message>
        <source>Spatial Filtering</source>
        <translation>空间滤波</translation>
    </message>
    <message>
        <source>&lt;p&gt;Apply spatial filters:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Enhancement &gt; Spatial Filter...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Try different filters:&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;Mean 3×3&lt;/b&gt; — Smooth noise&lt;/li&gt;&lt;li&gt;&lt;b&gt;Median 3×3&lt;/b&gt; — Remove salt-and-pepper noise&lt;/li&gt;&lt;li&gt;&lt;b&gt;Sobel&lt;/b&gt; — Detect edges&lt;/li&gt;&lt;li&gt;&lt;b&gt;Laplacian&lt;/b&gt; — Enhance edges&lt;/li&gt;&lt;/ul&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;应用空间滤波：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 增强 &gt; 空间滤波...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;尝试不同滤波器：&lt;/li&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;均值 3×3&lt;/b&gt; — 平滑噪声&lt;/li&gt;&lt;li&gt;&lt;b&gt;中值 3×3&lt;/b&gt; — 去除椒盐噪声&lt;/li&gt;&lt;li&gt;&lt;b&gt;Sobel&lt;/b&gt; — 检测边缘&lt;/li&gt;&lt;li&gt;&lt;b&gt;Laplacian&lt;/b&gt; — 增强边缘&lt;/li&gt;&lt;/ul&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Edge detection highlights boundaries between land cover types.</source>
        <translation>边缘检测能突出不同地物类型之间的边界。</translation>
    </message>
    <message>
        <source>Image Classification</source>
        <translation>影像分类</translation>
    </message>
    <message>
        <source>Learn supervised and unsupervised classification methods.</source>
        <translation>学习监督分类与非监督分类方法。</translation>
    </message>
    <message>
        <source>Load Data</source>
        <translation>加载数据</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load both the image and training samples:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;File &gt; Add Vector Layer... → &lt;code&gt;training_samples.shp&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;同时加载影像与训练样本：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;文件 &gt; 添加矢量图层... → &lt;code&gt;training_samples.shp&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Both layers visible in map canvas.</source>
        <translation>两个图层均显示在地图画布中。</translation>
    </message>
    <message>
        <source>Supervised Classification</source>
        <translation>监督分类</translation>
    </message>
    <message>
        <source>&lt;p&gt;Classify the image using training samples:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Classification...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select &lt;code&gt;landsat_sample.tif&lt;/code&gt; as input&lt;/li&gt;&lt;li&gt;Select &lt;code&gt;training_samples.shp&lt;/code&gt; as training data&lt;/li&gt;&lt;li&gt;Choose &lt;b&gt;NormalBayes&lt;/b&gt; classifier&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;使用训练样本对影像分类：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 分类...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;输入选择 &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;训练数据选择 &lt;code&gt;training_samples.shp&lt;/code&gt;&lt;/li&gt;&lt;li&gt;分类器选择&lt;b&gt;正态贝叶斯&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Classified image shows different land cover classes.</source>
        <translation>分类图中呈现不同的土地覆盖类别。</translation>
    </message>
    <message>
        <source>Accuracy Assessment</source>
        <translation>精度评价</translation>
    </message>
    <message>
        <source>&lt;p&gt;Evaluate classification accuracy:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;In the Classification window, click &lt;b&gt;Accuracy Assessment&lt;/b&gt;&lt;/li&gt;&lt;li&gt;View the confusion matrix&lt;/li&gt;&lt;li&gt;Note Overall Accuracy and Kappa coefficient&lt;/li&gt;&lt;li&gt;Export results to CSV&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;评价分类精度：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;在分类窗口点击&lt;b&gt;精度评价&lt;/b&gt;&lt;/li&gt;&lt;li&gt;查看混淆矩阵&lt;/li&gt;&lt;li&gt;记录总体精度与 Kappa 系数&lt;/li&gt;&lt;li&gt;将结果导出为 CSV&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Overall accuracy &gt; 80% is good for this simple example.</source>
        <translation>对这个简单示例而言，总体精度大于 80% 即属良好。</translation>
    </message>
    <message>
        <source>Change Detection</source>
        <translation>变化检测</translation>
    </message>
    <message>
        <source>Detect changes between two time periods.</source>
        <translation>检测两个时相之间的变化。</translation>
    </message>
    <message>
        <source>Load Before/After Images</source>
        <translation>加载前/后时相影像</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load both time period images:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;change_before.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;change_after.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;加载两期影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;change_before.tif&lt;/code&gt;&lt;/li&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;change_after.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Both images loaded.</source>
        <translation>两期影像均已加载。</translation>
    </message>
    <message>
        <source>Visual Comparison</source>
        <translation>目视对比</translation>
    </message>
    <message>
        <source>&lt;p&gt;Compare images visually:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;View &gt; Compare Layers...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Use &lt;b&gt;Flicker&lt;/b&gt; mode to spot changes&lt;/li&gt;&lt;li&gt;Use &lt;b&gt;Split Screen&lt;/b&gt; to compare side-by-side&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;目视对比两期影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;视图 &gt; 图层对比...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;用&lt;b&gt;闪烁对比&lt;/b&gt;发现变化&lt;/li&gt;&lt;li&gt;用&lt;b&gt;分屏对比&lt;/b&gt;并排查看&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>You should see a dark patch in the 'after' image.</source>
        <translation>你应该能在“后期”影像中看到一块深色图斑。</translation>
    </message>
    <message>
        <source>Run Change Detection</source>
        <translation>运行变化检测</translation>
    </message>
    <message>
        <source>&lt;p&gt;Compute change automatically:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Change Detection...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select &lt;b&gt;Normalized Difference&lt;/b&gt; method&lt;/li&gt;&lt;li&gt;Set 'Before' image&lt;/li&gt;&lt;li&gt;Set 'After' image&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;自动计算变化：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 变化检测...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择&lt;b&gt;归一化差值&lt;/b&gt;方法&lt;/li&gt;&lt;li&gt;设置“前期”影像&lt;/li&gt;&lt;li&gt;设置“后期”影像&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Change map highlights areas of change.</source>
        <translation>变化图会突出发生变化的区域。</translation>
    </message>
    <message>
        <source>Terrain Analysis</source>
        <translation>地形分析</translation>
    </message>
    <message>
        <source>Analyze terrain characteristics from DEM data.</source>
        <translation>从 DEM 数据分析地形特征。</translation>
    </message>
    <message>
        <source>Load DEM</source>
        <translation>加载 DEM</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load the sample DEM:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;dem_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;加载示例 DEM：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;dem_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>DEM loaded with elevation values.</source>
        <translation>DEM 已加载并显示高程值。</translation>
    </message>
    <message>
        <source>Generate Hillshade</source>
        <translation>生成山体阴影</translation>
    </message>
    <message>
        <source>&lt;p&gt;Create a hillshade for 3D visualization:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Terrain Analysis &gt; Slope/Aspect/Hillshade...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Check &lt;b&gt;Hillshade&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set azimuth = 315°, elevation = 45°&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;生成用于立体展示的山体阴影：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 地形分析 &gt; 坡度/坡向/山体阴影...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;勾选&lt;b&gt;山体阴影&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置方位角 = 315°、太阳高度角 = 45°&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Hillshade creates a 3D-like appearance.</source>
        <translation>山体阴影能营造立体视觉效果。</translation>
    </message>
    <message>
        <source>Calculate Slope</source>
        <translation>计算坡度</translation>
    </message>
    <message>
        <source>&lt;p&gt;Compute slope from DEM:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Terrain Analysis&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Check &lt;b&gt;Slope&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Slope values range from 0° (flat) to 90° (vertical).&lt;/p&gt;</source>
        <translation>&lt;p&gt;由 DEM 计算坡度：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 地形分析&lt;/b&gt;&lt;/li&gt;&lt;li&gt;勾选&lt;b&gt;坡度&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;坡度取值范围 0°（平坦）到 90°（垂直）。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Steep slopes appear bright in the slope image.</source>
        <translation>坡度图中陡坡呈现高亮值。</translation>
    </message>
    <message>
        <source>Atmospheric Correction</source>
        <translation>大气校正</translation>
    </message>
    <message>
        <source>Remove atmospheric effects from satellite imagery using DOS methods.</source>
        <translation>使用 DOS 系列方法去除卫星影像的大气影响。</translation>
    </message>
    <message>
        <source>Load Satellite Image</source>
        <translation>加载卫星影像</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load the sample Landsat image:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Atmospheric correction converts DN values to surface reflectance.&lt;/p&gt;</source>
        <translation>&lt;p&gt;加载示例 Landsat 影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;大气校正将 DN 值转换为地表反射率。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Image loaded with DN values.</source>
        <translation>影像已加载（DN 值）。</translation>
    </message>
    <message>
        <source>DOS1 Atmospheric Correction</source>
        <translation>DOS1 大气校正</translation>
    </message>
    <message>
        <source>&lt;p&gt;Apply Dark Object Subtraction (DOS1):&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Atmospheric Correction...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select method: &lt;b&gt;DOS1&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set gain and bias for each band (or use defaults)&lt;/li&gt;&lt;li&gt;Set output file (e.g., &lt;code&gt;dos1_corrected.tif&lt;/code&gt;)&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;DOS1 assumes the darkest pixel in the scene has zero reflectance.&lt;/p&gt;</source>
        <translation>&lt;p&gt;应用暗目标减除法（DOS1）：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 大气校正...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择方法：&lt;b&gt;DOS1&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置各波段增益与偏置（或使用默认值）&lt;/li&gt;&lt;li&gt;设置输出文件（如 &lt;code&gt;dos1_corrected.tif&lt;/code&gt;）&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;DOS1 假设场景中最暗像元的反射率为零。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Corrected values represent surface reflectance (0-1).</source>
        <translation>校正后的数值代表地表反射率（0–1）。</translation>
    </message>
    <message>
        <source>Compare Before/After</source>
        <translation>对比校正前后</translation>
    </message>
    <message>
        <source>&lt;p&gt;Compare original and corrected images:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;View &gt; Compare Layers...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Use &lt;b&gt;Split Screen&lt;/b&gt; to compare&lt;/li&gt;&lt;li&gt;Notice how atmospheric haze is reduced&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Surface reflectance is more suitable for quantitative analysis.&lt;/p&gt;</source>
        <translation>&lt;p&gt;对比原始影像与校正结果：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;视图 &gt; 图层对比...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;用&lt;b&gt;分屏对比&lt;/b&gt;查看&lt;/li&gt;&lt;li&gt;注意大气霾的影响被削弱&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;地表反射率更适合定量分析。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Corrected image shows clearer surface features.</source>
        <translation>校正后的影像地表特征更清晰。</translation>
    </message>
    <message>
        <source>Image Fusion</source>
        <translation>影像融合</translation>
    </message>
    <message>
        <source>Combine high-resolution panchromatic with multispectral imagery.</source>
        <translation>将高分辨率全色影像与多光谱影像融合。</translation>
    </message>
    <message>
        <source>Understanding Fusion</source>
        <translation>认识影像融合</translation>
    </message>
    <message>
        <source>&lt;p&gt;Image fusion combines:&lt;/p&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;Panchromatic&lt;/b&gt;: High spatial resolution, single band&lt;/li&gt;&lt;li&gt;&lt;b&gt;Multispectral&lt;/b&gt;: Lower resolution, multiple bands&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;Result: High resolution multispectral image&lt;/p&gt;&lt;p&gt;For this demo, we'll use the sample Landsat image as both inputs.&lt;/p&gt;</source>
        <translation>&lt;p&gt;影像融合组合：&lt;/p&gt;&lt;ul&gt;&lt;li&gt;&lt;b&gt;全色影像&lt;/b&gt;：高空间分辨率、单波段&lt;/li&gt;&lt;li&gt;&lt;b&gt;多光谱影像&lt;/b&gt;：分辨率较低、多波段&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;结果：高分辨率多光谱影像&lt;/p&gt;&lt;p&gt;本演示中，两个输入都使用示例 Landsat 影像。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Understanding the concept of image fusion.</source>
        <translation>理解影像融合的概念。</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load the sample image:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;In practice, you would load separate panchromatic and multispectral images.&lt;/p&gt;</source>
        <translation>&lt;p&gt;加载示例影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;实际应用中应分别加载全色与多光谱影像。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Image loaded.</source>
        <translation>影像已加载。</translation>
    </message>
    <message>
        <source>Brovey Fusion</source>
        <translation>Brovey 融合</translation>
    </message>
    <message>
        <source>&lt;p&gt;Apply Brovey fusion:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Image Fusion...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select method: &lt;b&gt;Brovey&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set high-resolution and multispectral inputs&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Brovey: R_fused = R_ms * Pan / (R_ms + G_ms + B_ms)&lt;/p&gt;</source>
        <translation>&lt;p&gt;应用 Brovey 融合：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 影像融合...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择方法：&lt;b&gt;Brovey&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置高分辨率与多光谱输入&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Brovey：R_fused = R_ms * Pan / (R_ms + G_ms + B_ms)&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Fused image has higher spatial detail.</source>
        <translation>融合后的影像空间细节更丰富。</translation>
    </message>
    <message>
        <source>IHS Fusion</source>
        <translation>IHS 融合</translation>
    </message>
    <message>
        <source>&lt;p&gt;Apply IHS fusion:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Image Fusion...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Select method: &lt;b&gt;IHS&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set inputs and output&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;IHS: Convert RGB→IHS, replace I with Pan, convert back.&lt;/p&gt;</source>
        <translation>&lt;p&gt;应用 IHS 融合：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 影像融合...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择方法：&lt;b&gt;IHS&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置输入与输出&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;IHS：先 RGB→IHS 变换，用全色替换 I 分量，再逆变换回来。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>IHS fusion preserves spectral characteristics well.</source>
        <translation>IHS 融合能较好地保持光谱特征。</translation>
    </message>
    <message>
        <source>PCA Analysis</source>
        <translation>主成分分析</translation>
    </message>
    <message>
        <source>Dimensionality reduction using Principal Component Analysis.</source>
        <translation>使用主成分分析进行降维。</translation>
    </message>
    <message>
        <source>Load Multi-band Image</source>
        <translation>加载多波段影像</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load the sample Landsat image:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;File &gt; Add Raster Layer... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PCA reduces the number of bands while preserving most information.&lt;/p&gt;</source>
        <translation>&lt;p&gt;加载示例 Landsat 影像：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;文件 &gt; 添加栅格图层... → &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PCA 能在保留大部分信息的前提下减少波段数量。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>7-band image loaded.</source>
        <translation>已加载 7 波段影像。</translation>
    </message>
    <message>
        <source>Run PCA</source>
        <translation>运行 PCA</translation>
    </message>
    <message>
        <source>&lt;p&gt;Perform PCA:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Enhancement &gt; PCA...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set number of components: &lt;b&gt;3&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Set output file (e.g., &lt;code&gt;pca_result.tif&lt;/code&gt;)&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PC1 contains the most variance, PC2 the second most, etc.&lt;/p&gt;</source>
        <translation>&lt;p&gt;执行主成分分析：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 增强 &gt; PCA...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;主成分个数设为 &lt;b&gt;3&lt;/b&gt;&lt;/li&gt;&lt;li&gt;设置输出文件（如 &lt;code&gt;pca_result.tif&lt;/code&gt;）&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PC1 包含最多方差，PC2 次之，依此类推。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>PCA result has 3 bands instead of 7.</source>
        <translation>PCA 结果为 3 个波段，而非原来的 7 个。</translation>
    </message>
    <message>
        <source>Analyze PCA Results</source>
        <translation>分析 PCA 结果</translation>
    </message>
    <message>
        <source>&lt;p&gt;Analyze the PCA output:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;View the PCA result image&lt;/li&gt;&lt;li&gt;PC1: Contains ~80% of variance (brightness)&lt;/li&gt;&lt;li&gt;PC2: Contains ~15% of variance (vegetation vs soil)&lt;/li&gt;&lt;li&gt;PC3: Contains ~5% of variance (noise or subtle features)&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PCA is useful for data compression and noise reduction.&lt;/p&gt;</source>
        <translation>&lt;p&gt;分析 PCA 输出：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;查看 PCA 结果影像&lt;/li&gt;&lt;li&gt;PC1：约含 80% 方差（亮度信息）&lt;/li&gt;&lt;li&gt;PC2：约含 15% 方差（植被与土壤差异）&lt;/li&gt;&lt;li&gt;PC3：约含 5% 方差（噪声或微弱特征）&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;PCA 常用于数据压缩与噪声抑制。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>PC1 shows the main patterns in the data.</source>
        <translation>PC1 展现了数据的主要格局。</translation>
    </message>
    <message>
        <source>Image Mosaic</source>
        <translation>影像镶嵌</translation>
    </message>
    <message>
        <source>Combine multiple images into a single mosaic.</source>
        <translation>将多景影像拼接为一幅镶嵌影像。</translation>
    </message>
    <message>
        <source>Understanding Mosaic</source>
        <translation>认识影像镶嵌</translation>
    </message>
    <message>
        <source>&lt;p&gt;Image mosaic combines multiple images:&lt;/p&gt;&lt;ul&gt;&lt;li&gt;Adjacent scenes from same sensor&lt;/li&gt;&lt;li&gt;Different times of same area&lt;/li&gt;&lt;li&gt;Creates seamless coverage&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;Key considerations: CRS alignment, color balancing, seamline.&lt;/p&gt;</source>
        <translation>&lt;p&gt;影像镶嵌组合多景影像：&lt;/p&gt;&lt;ul&gt;&lt;li&gt;同一传感器的相邻场景&lt;/li&gt;&lt;li&gt;同一地区不同时相&lt;/li&gt;&lt;li&gt;形成无缝覆盖&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;关键要点：CRS 一致、色彩平衡、镶嵌线。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Understanding mosaic concepts.</source>
        <translation>理解镶嵌的基本概念。</translation>
    </message>
    <message>
        <source>Open Mosaic Tool</source>
        <translation>打开镶嵌工具</translation>
    </message>
    <message>
        <source>&lt;p&gt;Open the mosaic dialog:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &gt; Mosaic...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Add input images&lt;/li&gt;&lt;li&gt;Set output file&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Run&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;Note: All input images must have the same CRS.&lt;/p&gt;</source>
        <translation>&lt;p&gt;打开镶嵌对话框：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &gt; 镶嵌...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;添加输入影像&lt;/li&gt;&lt;li&gt;设置输出文件&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;运行&lt;/b&gt;&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;注意：所有输入影像必须使用相同 CRS。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Mosaic created from input images.</source>
        <translation>已由输入影像生成镶嵌影像。</translation>
    </message>
    <message>
        <source>Object-Based Classification (OBIA)</source>
        <translation>面向对象分类 (OBIA)</translation>
    </message>
    <message>
        <source>Segment the image into objects, label segments, and classify by spectral shape features.</source>
        <translation>将影像分割为对象，标注图斑，并基于光谱与形状特征分类。</translation>
    </message>
    <message>
        <source>&lt;p&gt;Load bundled lab datasets from &lt;code&gt;data/samples/&lt;/code&gt;:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Help &amp;gt; Load Sample Data&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Confirm &lt;code&gt;landsat_sample.tif&lt;/code&gt; appears on the map&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;加载内置实验数据（&lt;code&gt;data/samples/&lt;/code&gt;）：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;帮助 &amp;gt; 加载示例数据&lt;/b&gt;&lt;/li&gt;&lt;li&gt;确认 &lt;code&gt;landsat_sample.tif&lt;/code&gt; 显示在地图上&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>Sample raster layers are visible in the layer tree.</source>
        <translation>示例栅格图层已显示在图层树中。</translation>
    </message>
    <message>
        <source>Open OBIA Window</source>
        <translation>打开 OBIA 窗口</translation>
    </message>
    <message>
        <source>&lt;p&gt;Launch the object-based classification workspace:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Go to &lt;b&gt;Raster &amp;gt; Classification &amp;gt; Object-based Classification (OBIA)...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Load Raster&lt;/b&gt; and select &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</source>
        <translation>&lt;p&gt;启动面向对象分类工作区：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;打开&lt;b&gt;栅格 &amp;gt; 分类 &amp;gt; 面向对象分类 (OBIA)...&lt;/b&gt;&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;加载栅格&lt;/b&gt;并选择 &lt;code&gt;landsat_sample.tif&lt;/code&gt;&lt;/li&gt;&lt;/ol&gt;</translation>
    </message>
    <message>
        <source>OBIA window is open with the raster loaded.</source>
        <translation>OBIA 窗口已打开且栅格已加载。</translation>
    </message>
    <message>
        <source>Segment and Classify</source>
        <translation>分割与分类</translation>
    </message>
    <message>
        <source>&lt;p&gt;Run the OBIA pipeline in the OBIA window:&lt;/p&gt;&lt;ol&gt;&lt;li&gt;Adjust segmentation parameters if needed, then click &lt;b&gt;Segment&lt;/b&gt; (or hierarchical segment)&lt;/li&gt;&lt;li&gt;Click objects on the map and assign classes, or use &lt;b&gt;Import ROI&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Choose a classifier and click &lt;b&gt;Classify&lt;/b&gt;&lt;/li&gt;&lt;li&gt;Review &lt;b&gt;Accuracy Assessment&lt;/b&gt; (training OA / Kappa / confusion matrix)&lt;/li&gt;&lt;li&gt;Click &lt;b&gt;Load to Main View&lt;/b&gt; to place the result on the main canvas&lt;/li&gt;&lt;li&gt;Optional: &lt;b&gt;Export&lt;/b&gt; polygons from the class raster&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;When OTB is installed, MeanShift is preferred; otherwise a built-in segmenter is used. Pipeline JSON labs: &lt;code&gt;data/pipelines/obia_*.json&lt;/code&gt; / workflow id &lt;code&gt;lab.obia&lt;/code&gt;.&lt;/p&gt;</source>
        <translation>&lt;p&gt;在 OBIA 窗口中运行完整流程：&lt;/p&gt;&lt;ol&gt;&lt;li&gt;按需调整分割参数，点击&lt;b&gt;分割&lt;/b&gt;（或分级分割）&lt;/li&gt;&lt;li&gt;在地图上点击对象并赋类别，或使用&lt;b&gt;导入 ROI&lt;/b&gt;&lt;/li&gt;&lt;li&gt;选择分类器并点击&lt;b&gt;分类&lt;/b&gt;&lt;/li&gt;&lt;li&gt;查看&lt;b&gt;精度评价&lt;/b&gt;（训练 OA / Kappa / 混淆矩阵）&lt;/li&gt;&lt;li&gt;点击&lt;b&gt;加载到主视图&lt;/b&gt;将结果显示到主画布&lt;/li&gt;&lt;li&gt;可选：从分类栅格&lt;b&gt;导出&lt;/b&gt;矢量面&lt;/li&gt;&lt;/ol&gt;&lt;p&gt;安装 OTB 时优先使用 MeanShift，否则使用内置分割器。流程 JSON 实验：&lt;code&gt;data/pipelines/obia_*.json&lt;/code&gt; / 工作流 id &lt;code&gt;lab.obia&lt;/code&gt;。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Class map produced; accuracy reviewed; result available on main map.</source>
        <translation>已生成分类图；查看精度；结果可在主地图中查看。</translation>
    </message>
</context>
<context>
    <name>HelpCenterDialog</name>
    <message>
        <source>Search help topics (Chinese / English / Help ID)...</source>
        <translation>搜索帮助主题（中文 / English / Help ID）…</translation>
    </message>
    <message>
        <source>Theme</source>
        <translation>主题</translation>
    </message>
    <message>
        <source>Topic not found</source>
        <translation>未找到主题</translation>
    </message>
    <message>
        <source>%1 results (%2 ms)</source>
        <translation>%1 个结果（%2 ms）</translation>
    </message>
    <message>
        <source>Help Center</source>
        <translation>帮助中心</translation>
    </message>
    <message>
        <source>Search above or browse the catalog on the left. Focus any UI element and press F1 to jump to the related topic.</source>
        <translation>在上方搜索，或从左侧目录浏览。聚焦任意界面元素后按 F1 可直接跳到相关主题。</translation>
    </message>
    <message>
        <source>%1 topics are included.</source>
        <translation>共收录 %1 个主题。</translation>
    </message>
    <message>
        <source>When to Use</source>
        <translation>用途</translation>
    </message>
    <message>
        <source>Prerequisites</source>
        <translation>前提</translation>
    </message>
    <message>
        <source>Suggested Next Step</source>
        <translation>建议下一步</translation>
    </message>
    <message>
        <source>Meaning</source>
        <translation>含义</translation>
    </message>
    <message>
        <source>Unit</source>
        <translation>单位</translation>
    </message>
    <message>
        <source>Recommended Value</source>
        <translation>推荐值</translation>
    </message>
    <message>
        <source>Trade-offs</source>
        <translation>权衡</translation>
    </message>
    <message>
        <source>Performance</source>
        <translation>性能</translation>
    </message>
    <message>
        <source>Notes</source>
        <translation>注意事项</translation>
    </message>
    <message>
        <source>How It Works</source>
        <translation>原理</translation>
    </message>
    <message>
        <source>Applicability</source>
        <translation>适用</translation>
    </message>
    <message>
        <source>Input Requirements</source>
        <translation>输入要求</translation>
    </message>
    <message>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>Assumptions</source>
        <translation>假设</translation>
    </message>
    <message>
        <source>Value Range</source>
        <translation>数值域</translation>
    </message>
    <message>
        <source>Limitations</source>
        <translation>局限</translation>
    </message>
    <message>
        <source>Typical Failure Modes</source>
        <translation>典型失败模式</translation>
    </message>
    <message>
        <source>Next Step</source>
        <translation>下一步</translation>
    </message>
    <message>
        <source>Recommended Action</source>
        <translation>推荐操作</translation>
    </message>
    <message>
        <source>What Happened</source>
        <translation>发生了什么</translation>
    </message>
    <message>
        <source>Why It Matters</source>
        <translation>为什么重要</translation>
    </message>
    <message>
        <source>How to Fix</source>
        <translation>如何解决</translation>
    </message>
    <message>
        <source>Technical Details</source>
        <translation>技术细节</translation>
    </message>
    <message>
        <source>Related Topics</source>
        <translation>相关主题</translation>
    </message>
</context>
<context>
    <name>HelpViewerDialog</name>
    <message>
        <source>RS Studio User Manual and Help</source>
        <translation>RS Studio 用户手册与帮助文档</translation>
    </message>
    <message>
        <source>&lt;b&gt;Documentation&lt;/b&gt;</source>
        <translation>&lt;b&gt;文档目录&lt;/b&gt;</translation>
    </message>
    <message>
        <source>Filter the section catalog...</source>
        <translation>过滤章节目录…</translation>
    </message>
    <message>
        <source>Find in Document...</source>
        <translation>在文档正文中查找…</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>下一个</translation>
    </message>
    <message>
        <source>Previous</source>
        <translation>上一个</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>100%</source>
        <translation>100%</translation>
    </message>
    <message>
        <source>External Browser</source>
        <translation>外部浏览器</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source># RS Studio (exp-rs) Comprehensive User Manual and Operation Guide

&gt; **Version**: v2.0 Professional  
&gt; **System document code**: DOC-RS-STUDIO-USERGUIDE-CN  

# Chapter 1: System Overview and Quick Start
RS Studio is a new-generation desktop intelligent geospatial analysis platform for modern remote-sensing research, university teaching and industrial production.

# Chapter 2: Loading and Managing Remote-Sensing Data
Provides automatic multi-source satellite product import, STAC cloud search and Data Manager asset management.

# Chapter 3: Viewport Visualization and Multi-Source Linkage
Provides linked split viewports, swipe comparison, band composition and real-time display stretching.

# Chapter 4: The Full Pixel-Level Classification Workflow
Provides a complete 7-step guided workflow: class scheme, ROI collection, JM-distance separability evaluation, model training, confusion-matrix accuracy assessment, post-classification and result export.

# Chapter 5: Object-Based Image Analysis (OBIA)
Provides multiresolution segmentation, hierarchical topology trees, GLCM texture and geometric feature extraction, and object classification.

# Chapter 6: Spectral Analysis and Hyperspectral Tools
Provides spectral profiles, continuum removal, library SAM / SID matching, linear unmixing and the RX anomaly detector.

# Chapter 7: Remote-Sensing Preprocessing and Image Enhancement
Covers radiometric calibration, atmospheric correction (DOS1, DOS2, QUAC), cloud/snow QA masking, image registration and spatial filtering.

# Chapter 8: AI Copilot Assistant
LLM-powered natural-language remote-sensing analysis chat, tool calls and automated DAG pipeline orchestration.

# Chapter 9: Troubleshooting and Diagnostics
Covers startup dependencies, projection anomalies, out-of-memory tiling optimization and network connectivity troubleshooting.

# Chapter 10: Shortcut and Operation Quick Reference
A quick reference of shortcuts for projects, viewport navigation, vector editing and image registration.
</source>
        <translation># RS Studio (exp-rs) 综合用户手册与操作指南

&gt; **版本**：v2.0 Professional  
&gt; **系统文档代码**：DOC-RS-STUDIO-USERGUIDE-CN  

# 第 1 章：系统概述与快速入门
RS Studio 是面向现代遥感科研、高校教学与工业生产的新一代桌面智能地理空间分析平台。

# 第 2 章：遥感数据加载与管理
支持多源卫星产品自动识别导入、STAC 云端检索与 Data Manager 资产管理。

# 第 3 章：视口可视化与多源联动
提供双视口分屏同步联动、卷帘对比 (Swipe)、波段合成与实时显示拉伸。

# 第 4 章：像素级遥感分类全流程
提供完整的 7 步引导流程：类别体系、ROI 采集、JM 距离可分性评价、模型训练、混淆矩阵精度评定、分类后处理与成果导出。

# 第 5 章：面向对象影像分析 (OBIA)
提供多尺度分割、多层级拓扑树、GLCM 纹理与几何特征提取及对象分类。

# 第 6 章：波谱分析与高光谱工具
提供光谱剖面图、连续统去除 (Continuum Removal)、光谱库 SAM / SID 匹配、线性解混与 RX 异常探测。

# 第 7 章：遥感预处理与图像增强
包括辐射定标、大气校正 (DOS1, DOS2, QUAC)、云雪 QA 掩膜、影像配准与空间滤波。

# 第 8 章：AI Copilot 智能助手
基于大语言模型的自然语言遥感分析对话、工具调用与 DAG 流程自动化编排。

# 第 9 章：常见问题排查与诊断
启动依赖、坐标投影异常、内存溢出分块优化与网络连通性排查。

# 第 10 章：快捷键与操作速查表
汇总全局工程、视口漫游、矢量编辑与影像配准快捷键速查表。
</translation>
    </message>
</context>
<context>
    <name>HistogramStretchWidget</name>
    <message>
        <source>Histogram: drag control points for a piecewise linear stretch; double-click adds and right-click removes a point.</source>
        <translation>直方图：拖动控制点做分段线性拉伸；双击添加、右键删除控制点。</translation>
    </message>
    <message>
        <source>Choose the histogram channel mode: master RGB, a single channel, or single-band grayscale.</source>
        <translation>选择直方图通道模式：RGB 综合、单通道或单波段灰度。</translation>
    </message>
    <message>
        <source>Master RGB</source>
        <translation>RGB 综合通道 (Master RGB)</translation>
    </message>
    <message>
        <source>Red Channel</source>
        <translation>红通道 (Red)</translation>
    </message>
    <message>
        <source>Green Channel</source>
        <translation>绿通道 (Green)</translation>
    </message>
    <message>
        <source>Blue Channel</source>
        <translation>蓝通道 (Blue)</translation>
    </message>
    <message>
        <source>Single Band / Grayscale</source>
        <translation>单波段 / 灰度 (Single Band)</translation>
    </message>
    <message>
        <source>Channel Mode:</source>
        <translation>通道模式 (Channel):</translation>
    </message>
    <message>
        <source>Chooses the band number in single-band mode.</source>
        <translation>选择单波段模式下的波段号。</translation>
    </message>
    <message>
        <source>Single Band (Band):</source>
        <translation>单波段选择 (Band):</translation>
    </message>
    <message>
        <source>Stretch algorithms: piecewise linear / PS levels / 2% clip / 2σ / full-range linear / histogram equalization / none.</source>
        <translation>拉伸算法：分段线性/PS 色阶/2% 剪裁/2σ/全阶线性/均衡化/无增强。</translation>
    </message>
    <message>
        <source>Piecewise Linear Stretch</source>
        <translation>分段线性拉伸 (Piecewise Linear)</translation>
    </message>
    <message>
        <source>Photoshop Levels Adjustment</source>
        <translation>Photoshop 色阶调整 (PS Levels)</translation>
    </message>
    <message>
        <source>2% Cumulative-Clip Linear Stretch</source>
        <translation>2% 累计剪裁线性拉伸 (2% Percent Clip)</translation>
    </message>
    <message>
        <source>2σ Std-Dev Stretch</source>
        <translation>2σ 标准差拉伸 (StdDev)</translation>
    </message>
    <message>
        <source>Full-Range Linear Stretch (Min-Max)</source>
        <translation>全阶线性拉伸 (Linear Min-Max)</translation>
    </message>
    <message>
        <source>Histogram Equalization</source>
        <translation>直方图均衡化</translation>
    </message>
    <message>
        <source>No Enhancement</source>
        <translation>无增强 (No Enhancement)</translation>
    </message>
    <message>
        <source>Stretch Algorithm:</source>
        <translation>拉伸算法 (Algorithm):</translation>
    </message>
    <message>
        <source>Piecewise interaction: double-click to add a control point, drag to move, right-click to delete.</source>
        <translation>分段交互: 双击添加控制点，拖拽移动，右键删除。</translation>
    </message>
    <message>
        <source>Clip ratio: keeps the middle N% of pixels for the linear stretch (used by the 2% clip algorithm).</source>
        <translation>剪裁比例：保留中间 N% 像素做线性拉伸（2% 剪裁算法用）。</translation>
    </message>
    <message>
        <source>98%</source>
        <translation>98%</translation>
    </message>
    <message>
        <source>Clip %:</source>
        <translation>剪裁比例 (Clip %):</translation>
    </message>
    <message>
        <source>Shadows (minimum): pixels below this map to black.</source>
        <translation>阴影（最小值）：低于此值的像素映射为黑。</translation>
    </message>
    <message>
        <source>Gamma (midtones): 1.0 is linear; &lt;1 brightens, &gt;1 darkens.</source>
        <translation>Gamma（中间调）：1.0 为线性；&lt;1 提亮，&gt;1 压暗。</translation>
    </message>
    <message>
        <source>Highlights (maximum): pixels above this map to white.</source>
        <translation>高光（最大值）：高于此值的像素映射为白。</translation>
    </message>
    <message>
        <source>Shadows (Min):</source>
        <translation>阴影 (Min):</translation>
    </message>
    <message>
        <source>Gamma (midtones):</source>
        <translation>Gamma（中间调）：</translation>
    </message>
    <message>
        <source>Highlights (Max):</source>
        <translation>高光 (Max):</translation>
    </message>
    <message>
        <source>Apply to Display</source>
        <translation>应用到显示</translation>
    </message>
    <message>
        <source>Applies the current stretch parameters to the map display.</source>
        <translation>把当前拉伸参数应用到地图显示。</translation>
    </message>
    <message>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <source>Resets the stretch parameters to their defaults.</source>
        <translation>重置拉伸参数为默认值。</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Std-dev factor:</source>
        <translation>标准差系数:</translation>
    </message>
    <message>
        <source>2.0σ</source>
        <translation>2.0σ</translation>
    </message>
    <message>
        <source>Keep %:</source>
        <translation>保留比例 (Keep %):</translation>
    </message>
    <message>
        <source>%1σ</source>
        <translation>%1σ</translation>
    </message>
    <message>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
</context>
<context>
    <name>HistogramWidget</name>
    <message>
        <source>Cannot open raster</source>
        <translation>无法打开栅格</translation>
    </message>
    <message>
        <source>No histogram data available</source>
        <translation>无可用直方图数据</translation>
    </message>
    <message>
        <source>No raster layer selected</source>
        <translation>未选择栅格图层</translation>
    </message>
    <message>
        <source>RGB Combined Histogram</source>
        <translation>RGB 综合直方图</translation>
    </message>
    <message>
        <source>Band %1 Histogram</source>
        <translation>第 %1 波段直方图</translation>
    </message>
    <message>
        <source>Real Data Range: [%1 ~ %2] | Mean: %3</source>
        <translation>实际数值域：[%1 ~ %2] | 均值：%3</translation>
    </message>
</context>
<context>
    <name>ImageEnhancementPanel</name>
    <message>
        <source>Image Enhancement</source>
        <translation>影像增强</translation>
    </message>
    <message>
        <source>Enhancement Type</source>
        <translation>增强类型</translation>
    </message>
    <message>
        <source>Choose an enhancement type; the parameter pages below follow the selection.</source>
        <translation>选择一类增强；下方参数页随类型切换。</translation>
    </message>
    <message>
        <source>Contrast Stretch</source>
        <translation>对比度拉伸</translation>
    </message>
    <message>
        <source>Spatial Filtering</source>
        <translation>空间滤波</translation>
    </message>
    <message>
        <source>Band Ratio / IHS</source>
        <translation>波段比值 / IHS</translation>
    </message>
    <message>
        <source>Speckle Filtering (SAR)</source>
        <translation>斑点滤波 (SAR)</translation>
    </message>
    <message>
        <source>Contrast stretch / spatial filtering / band ratio · IHS / SAR speckle filtering.</source>
        <translation>对比度拉伸 / 空间滤波 / 波段比值·IHS / SAR 斑点滤波。</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Contrast Stretch Parameters</source>
        <translation>对比度拉伸参数</translation>
    </message>
    <message>
        <source>Spatial Filtering Parameters</source>
        <translation>空间滤波参数</translation>
    </message>
    <message>
        <source>Band Ratio / IHS Parameters</source>
        <translation>波段比值 / IHS 参数</translation>
    </message>
    <message>
        <source>Speckle Filtering Parameters</source>
        <translation>斑点滤波参数</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Linear Min-Max</source>
        <translation>线性最小-最大</translation>
    </message>
    <message>
        <source>Percentage Clip</source>
        <translation>百分比裁剪</translation>
    </message>
    <message>
        <source>Standard Deviation</source>
        <translation>标准差</translation>
    </message>
    <message>
        <source>Histogram Equalization</source>
        <translation>直方图均衡化</translation>
    </message>
    <message>
        <source>Linear / percent clip / std dev / histogram equalization.</source>
        <translation>线性 / 百分比裁剪 / 标准差 / 直方图均衡。</translation>
    </message>
    <message>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <source>Clip percentage at both tails; 1–2% is typical.</source>
        <translation>两端裁剪百分比。常用 1–2%。</translation>
    </message>
    <message>
        <source>Clip %:</source>
        <translation>剪裁比例 (Clip %):</translation>
    </message>
    <message>
        <source>Std-dev multiplier K; 2 is typical.</source>
        <translation>标准差倍数 K。常用 2。</translation>
    </message>
    <message>
        <source>StdDev ×:</source>
        <translation>标准差倍数：</translation>
    </message>
    <message>
        <source>Mean</source>
        <translation>均值</translation>
    </message>
    <message>
        <source>Gaussian</source>
        <translation>高斯</translation>
    </message>
    <message>
        <source>Median</source>
        <translation>中值</translation>
    </message>
    <message>
        <source>Sobel (Edge)</source>
        <translation>Sobel（边缘）</translation>
    </message>
    <message>
        <source>Laplacian (Edge)</source>
        <translation>Laplacian（边缘）</translation>
    </message>
    <message>
        <source>Smoothing (mean/Gaussian/median) or edge (Sobel/Laplacian).</source>
        <translation>平滑（均值/高斯/中值）或边缘（Sobel/Laplacian）。</translation>
    </message>
    <message>
        <source>Filter:</source>
        <translation>滤波器：</translation>
    </message>
    <message>
        <source>Convolution kernel size.</source>
        <translation>卷积核大小。</translation>
    </message>
    <message>
        <source>Kernel Size:</source>
        <translation>卷积核大小：</translation>
    </message>
    <message>
        <source>Gaussian filter std dev σ.</source>
        <translation>高斯滤波标准差 σ。</translation>
    </message>
    <message>
        <source>Sigma:</source>
        <translation>σ：</translation>
    </message>
    <message>
        <source>e.g., 0 -1 0 -1 5 -1 0 -1 0 (3x3 row-major)</source>
        <translation>例如：0 -1 0 -1 5 -1 0 -1 0（3x3 按行排列）</translation>
    </message>
    <message>
        <source>Custom kernel: coefficients separated by spaces in row-major order.</source>
        <translation>自定义核：按行主序空格分隔系数。</translation>
    </message>
    <message>
        <source>Custom Kernel:</source>
        <translation>自定义卷积核：</translation>
    </message>
    <message>
        <source>Band Ratio</source>
        <translation>波段比值 (Band Ratio)</translation>
    </message>
    <message>
        <source>IHS Transform</source>
        <translation>IHS 变换</translation>
    </message>
    <message>
        <source>Band ratio or IHS transform.</source>
        <translation>波段比值或 IHS 变换。</translation>
    </message>
    <message>
        <source>Ratio numerator or IHS red band.</source>
        <translation>比值分子或 IHS 红色波段。</translation>
    </message>
    <message>
        <source>Ratio denominator or IHS green band.</source>
        <translation>比值分母或 IHS 绿色波段。</translation>
    </message>
    <message>
        <source>IHS blue band.</source>
        <translation>IHS 蓝色波段。</translation>
    </message>
    <message>
        <source>Band 1 / R:</source>
        <translation>波段 1 / 红：</translation>
    </message>
    <message>
        <source>Band 2 / G:</source>
        <translation>波段 2 / 绿：</translation>
    </message>
    <message>
        <source>Band 3 / B:</source>
        <translation>波段 3 / 蓝：</translation>
    </message>
    <message>
        <source>Red (R):</source>
        <translation>红 (R)：</translation>
    </message>
    <message>
        <source>Band 1:</source>
        <translation>波段 1：</translation>
    </message>
    <message>
        <source>Green (G):</source>
        <translation>绿 (G)：</translation>
    </message>
    <message>
        <source>Band 2:</source>
        <translation>波段 2：</translation>
    </message>
    <message>
        <source>Lee</source>
        <translation>Lee</translation>
    </message>
    <message>
        <source>Frost</source>
        <translation>Frost</translation>
    </message>
    <message>
        <source>Kuan</source>
        <translation>Kuan</translation>
    </message>
    <message>
        <source>Gamma MAP</source>
        <translation>Gamma MAP</translation>
    </message>
    <message>
        <source>SAR speckle filtering: Lee / Frost / Kuan / Gamma-MAP.</source>
        <translation>SAR 斑点滤波：Lee / Frost / Kuan / Gamma-MAP。</translation>
    </message>
    <message>
        <source>Filter window size.</source>
        <translation>滤波窗口大小。</translation>
    </message>
    <message>
        <source>Noise variance (non-Frost).</source>
        <translation>噪声方差（非 Frost）。</translation>
    </message>
    <message>
        <source>Noise Variance:</source>
        <translation>噪声方差：</translation>
    </message>
    <message>
        <source>Frost damping factor.</source>
        <translation>Frost 阻尼因子。</translation>
    </message>
    <message>
        <source>Damping (Frost):</source>
        <translation>阻尼因子（Frost）：</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Please select a raster layer.</source>
        <translation>请选择一个栅格图层。</translation>
    </message>
    <message>
        <source>Processing...</source>
        <translation>处理中…</translation>
    </message>
    <message>
        <source>Please select valid distinct bands.</source>
        <translation>请选择两个不同的有效波段。</translation>
    </message>
    <message>
        <source>Please select valid RGB bands for IHS transform.</source>
        <translation>请为 IHS 变换选择有效的 RGB 波段。</translation>
    </message>
    <message>
        <source>Please select distinct bands for IHS transform.</source>
        <translation>IHS 变换的三个波段必须互不相同。</translation>
    </message>
    <message>
        <source>IHS transform requires an image with at least 3 bands.</source>
        <translation>IHS 变换要求影像至少有 3 个波段。</translation>
    </message>
</context>
<context>
    <name>LogPanel</name>
    <message>
        <source>System Log</source>
        <translation>系统日志</translation>
    </message>
    <message>
        <source>Level:</source>
        <translation>级别：</translation>
    </message>
    <message>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>警告</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Succeeded</source>
        <translation>成功</translation>
    </message>
    <message>
        <source>Tags:</source>
        <translation>标签：</translation>
    </message>
    <message>
        <source>Search:</source>
        <translation>搜索：</translation>
    </message>
    <message>
        <source>Filter log messages...</source>
        <translation>过滤日志消息...</translation>
    </message>
    <message>
        <source>Auto-Scroll</source>
        <translation>自动滚动</translation>
    </message>
    <message>
        <source>0 messages</source>
        <translation>0 条消息</translation>
    </message>
    <message>
        <source>Clear</source>
        <translation>清空</translation>
    </message>
    <message>
        <source>No system log yet</source>
        <translation>暂无系统日志</translation>
    </message>
    <message>
        <source>The system is running normally and no log messages have been produced yet. Detailed records appear here as algorithms and operations run.</source>
        <translation>系统运行正常，当前尚未产生日志消息。执行算法或操作时将在此显示详细记录。</translation>
    </message>
    <message>
        <source>%1 messages</source>
        <translation>%1 条消息</translation>
    </message>
</context>
<context>
    <name>MeasureTool</name>
    <message>
        <source>Distance: %1 %2</source>
        <translation>距离：%1 %2</translation>
    </message>
    <message>
        <source>Area: %1 %2</source>
        <translation>面积：%1 %2</translation>
    </message>
    <message>
        <source>Measurement Result</source>
        <translation>量测结果</translation>
    </message>
</context>
<context>
    <name>MosaicDialog</name>
    <message>
        <source>Input Image List</source>
        <translation>输入影像列表</translation>
    </message>
    <message>
        <source>Add at least 2 raster files; a common spatial reference and resolution are recommended. Overlaps are merged with the default strategy.</source>
        <translation>至少添加 2 个栅格文件；建议统一空间参考与分辨率。重叠区按默认策略拼接合并。</translation>
    </message>
    <message>
        <source>List of raster files taking part in the mosaic.</source>
        <translation>参与镶嵌的栅格文件列表。</translation>
    </message>
    <message>
        <source>Add Files...</source>
        <translation>添加文件…</translation>
    </message>
    <message>
        <source>Adds one or more raster files to the mosaic input list.</source>
        <translation>添加一个或多个栅格文件到待镶嵌列表。</translation>
    </message>
    <message>
        <source>Remove Selected</source>
        <translation>移除选中</translation>
    </message>
    <message>
        <source>Removes the selected raster files from the mosaic input list.</source>
        <translation>从待镶嵌列表中移除选中的栅格文件。</translation>
    </message>
    <message>
        <source>Tip: align all images to the same CRS and pixel resolution before mosaicking.</source>
        <translation>提示：建议在镶嵌前统一各影像的坐标参考系 (CRS) 与像元分辨率。</translation>
    </message>
    <message>
        <source>Add Input Raster</source>
        <translation>添加输入栅格</translation>
    </message>
    <message>
        <source>Rasters (*.tif *.tiff *.img *.asc);;All Files (*)</source>
        <translation>栅格 (*.tif *.tiff *.img *.asc);;所有文件 (*)</translation>
    </message>
    <message>
        <source>At least 2 input rasters are required.</source>
        <translation>至少需要 2 个输入栅格。</translation>
    </message>
    <message>
        <source>Specify the output file path.</source>
        <translation>请指定输出文件路径。</translation>
    </message>
    <message>
        <source>Image Mosaic</source>
        <translation>影像镶嵌</translation>
    </message>
</context>
<context>
    <name>OrthorectificationDialog</name>
    <message>
        <source>Orthorectification Parameters</source>
        <translation>正射参数</translation>
    </message>
    <message>
        <source>Terrain-corrects the image using RPC/GCPs and an optional DEM. The input raster must carry RPC metadata or GCPs.</source>
        <translation>基于 RPC/GCP 与可选 DEM 对影像做地形纠正。输入栅格必须携带 RPC 元数据或 GCP。</translation>
    </message>
    <message>
        <source>Target CRS (e.g. EPSG:4326, EPSG:32650). Left empty, the CRS carried by the RPC / GCPs is used.</source>
        <translation>目标 CRS（如 EPSG:4326、EPSG:32650）。留空使用 RPC/GCP 自带 CRS。</translation>
    </message>
    <message>
        <source>Target CRS</source>
        <translation>目标 CRS</translation>
    </message>
    <message>
        <source>DEM raster (optional, for terrain correction)</source>
        <translation>DEM 栅格（可选，用于地形纠正）</translation>
    </message>
    <message>
        <source>Path of the elevation raster for terrain correction (DEM/DSM)</source>
        <translation>指定用于地形校正的高程栅格路径（DEM/DSM）</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the DEM elevation raster file</source>
        <translation>浏览并选择 DEM 高程栅格文件</translation>
    </message>
    <message>
        <source>DEM raster</source>
        <translation>DEM 栅格</translation>
    </message>
    <message>
        <source>Bilinear (default)</source>
        <translation>双线性（默认）</translation>
    </message>
    <message>
        <source>Nearest Neighbour</source>
        <translation>最邻近</translation>
    </message>
    <message>
        <source>Cubic Convolution</source>
        <translation>三次卷积</translation>
    </message>
    <message>
        <source>Cubic Spline</source>
        <translation>三次样条</translation>
    </message>
    <message>
        <source>Lanczos</source>
        <translation>Lanczos</translation>
    </message>
    <message>
        <source>Raster resampling interpolation: bilinear or cubic convolution for continuous imagery; nearest neighbour for classification / discrete rasters</source>
        <translation>栅格重采样插值算法：连续影像建议双线性或三次卷积，分类/离散栅格建议最邻近</translation>
    </message>
    <message>
        <source>Resampling Method</source>
        <translation>重采样方法</translation>
    </message>
    <message>
        <source>Automatic</source>
        <translation>自动</translation>
    </message>
    <message>
        <source>Output pixel size (in target CRS units); 0 = automatic.</source>
        <translation>输出像元尺寸（目标 CRS 单位）；0 = 自动。</translation>
    </message>
    <message>
        <source>Output Resolution</source>
        <translation>输出分辨率</translation>
    </message>
    <message>
        <source>None</source>
        <translation>不使用</translation>
    </message>
    <message>
        <source>Constant elevation (m) when no DEM is available.</source>
        <translation>无 DEM 时的恒定高程（米）。</translation>
    </message>
    <message>
        <source>Constant Elevation</source>
        <translation>恒定高程</translation>
    </message>
    <message>
        <source>Specify NoData value</source>
        <translation>指定 NoData 值</translation>
    </message>
    <message>
        <source>Specify a custom NoData value for the output orthophoto</source>
        <translation>是否为输出正射影像指定自定义的无效像元值 (NoData)</translation>
    </message>
    <message>
        <source>Fill pixel value for invalid / uncovered areas of the output orthophoto</source>
        <translation>输出正射影像中无效/未覆盖区域的填充像元值</translation>
    </message>
    <message>
        <source>NoData Settings</source>
        <translation>NoData 设置</translation>
    </message>
    <message>
        <source>The input carries RPC metadata; RPC orthorectification will be enabled.</source>
        <translation>输入含 RPC 元数据，将启用 RPC 正射。</translation>
    </message>
    <message>
        <source>The input carries GCPs; correction will be GCP-based.</source>
        <translation>输入含 GCP，将基于 GCP 校正。</translation>
    </message>
    <message>
        <source>The input has neither RPC metadata nor GCPs; gdal:orthorectification will refuse to run.</source>
        <translation>输入无 RPC 元数据且无 GCP；gdal:orthorectification 将拒绝执行。</translation>
    </message>
    <message>
        <source>Select DEM Raster</source>
        <translation>选择 DEM 栅格</translation>
    </message>
    <message>
        <source>Raster Files (*.tif *.tiff *.img);;All Files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Select a valid raster layer first.</source>
        <translation>请先选择一个有效的栅格图层。</translation>
    </message>
    <message>
        <source>Orthorectification</source>
        <translation>正射校正</translation>
    </message>
</context>
<context>
    <name>PcaDialog</name>
    <message>
        <source>PCA Transform Parameters</source>
        <translation>PCA 变换参数</translation>
    </message>
    <message>
        <source>The number of components cannot exceed the total band count of the input image; the first components usually capture most of the variance.</source>
        <translation>主成分个数不得超过输入影像的波段总数；前几个主成分通常聚集了绝大部分方差信息。</translation>
    </message>
    <message>
        <source>Number of output components, 0 = all bands; must be ≤ the input band count.The first PCs usually hold most of the variance; used for band decorrelation and dimensionality-reduction compression.</source>
        <translation>输出主成分个数，0 = 全部波段；必须 ≤ 输入波段数。前几个 PC 通常含大部分方差，用于波段去相关与降维压缩。</translation>
    </message>
    <message>
        <source>Number of Components</source>
        <translation>主成分个数</translation>
    </message>
    <message>
        <source>Select a valid raster layer first.</source>
        <translation>请先选择一个有效的栅格图层。</translation>
    </message>
    <message>
        <source>The number of components (%1) exceeds the total band count of the input raster (%2).</source>
        <translation>指定的主成分数 (%1) 超出输入栅格的波段总数 (%2)。</translation>
    </message>
    <message>
        <source>Principal Component Analysis (PCA)</source>
        <translation>主成分分析 (PCA)</translation>
    </message>
</context>
<context>
    <name>PluginManagerDialog</name>
    <message>
        <source>Plugin Manager</source>
        <translation>插件管理器</translation>
    </message>
    <message>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Version</source>
        <translation>版本</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <source>Diagnostics of the selected plugin</source>
        <translation>选中插件的诊断信息</translation>
    </message>
    <message>
        <source>Enable</source>
        <translation>启用</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation>禁用</translation>
    </message>
    <message>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <source>Found %1 plugins (%2 usable, %3 broken). The scan only reads plugin.json,Plugin binaries are not loaded.</source>
        <translation>已发现 %1 个插件（可用 %2，异常 %3）。扫描仅读取 plugin.json，不会加载插件二进制。</translation>
    </message>
    <message>
        <source>Plugin In Use</source>
        <translation>插件正在使用中</translation>
    </message>
    <message>
        <source>The plugin is executing and cannot be disabled.</source>
        <translation>插件正在执行，无法禁用。</translation>
    </message>
    <message>
        <source>Plugin Failed to Load</source>
        <translation>插件加载失败</translation>
    </message>
    <message>
        <source>Enablement saved, but the plugin failed to load — see the diagnostics.</source>
        <translation>启用已保存，但插件加载失败——查看诊断信息。</translation>
    </message>
    <message>
        <source>No diagnostics.</source>
        <translation>无诊断信息。</translation>
    </message>
</context>
<context>
    <name>PostClassificationDialog</name>
    <message>
        <source>Two-date classification result input</source>
        <translation>双时相分类结果输入</translation>
    </message>
    <message>
        <source>Earlier classification raster (theme map).</source>
        <translation>前期分类栅格（主题图）。</translation>
    </message>
    <message>
        <source>Later classification raster (theme map).</source>
        <translation>后期分类栅格（主题图）。</translation>
    </message>
    <message>
        <source>Earlier classification band.</source>
        <translation>前期分类波段。</translation>
    </message>
    <message>
        <source>Later classification band.</source>
        <translation>后期分类波段。</translation>
    </message>
    <message>
        <source>Earlier Classification</source>
        <translation>前期分类</translation>
    </message>
    <message>
        <source>Earlier Band</source>
        <translation>前期波段</translation>
    </message>
    <message>
        <source>Later Classification</source>
        <translation>后期分类</translation>
    </message>
    <message>
        <source>Later Band</source>
        <translation>后期波段</translation>
    </message>
    <message>
        <source>Automatic (max observed class + 1)</source>
        <translation>自动（按观测最大类 + 1）</translation>
    </message>
    <message>
        <source>Compares two classification dates: outputs a per-class transition matrix (rows = earlier classes, columns = later classes),Per-class gains / losses and a change-type map.</source>
        <translation>比较两期分类结果：输出逐类转移矩阵（行=前时相类，列=后时相类）、逐类增益/损失与变化类型图。</translation>
    </message>
    <message>
        <source>Total classes (the change code before*classCount+after must fit a UInt16, hence ≤ 255).0 = inferred automatically from the maximum class observed across the two images.</source>
        <translation>类别总数（变化码 before*classCount+after 须装入 UInt16，故 ≤255）。0 = 按两期影像中观测到的最大类别自动推断。</translation>
    </message>
    <message>
        <source>Total Classes</source>
        <translation>类别总数</translation>
    </message>
    <message>
        <source>After running, the change statistics summary and transition matrix appear here.</source>
        <translation>运行后在此显示变化统计摘要与转移矩阵。</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Specify the output file.</source>
        <translation>请指定输出文件。</translation>
    </message>
    <message>
        <source>Select the earlier and later classification rasters.</source>
        <translation>请选择前期与后期分类栅格。</translation>
    </message>
    <message>
        <source>The earlier or later classification raster is invalid.</source>
        <translation>前期或后期分类栅格无效。</translation>
    </message>
    <message>
        <source>The pixel grids of the two classification rasters are incompatible; cannot compare:
%1</source>
        <translation>两张分类栅格的像元网格不兼容，无法比较：
%1</translation>
    </message>
    <message>
        <source>The selected classification raster is invalid.</source>
        <translation>所选分类栅格无效。</translation>
    </message>
    <message>
        <source>Changed pixels: %1 / %2 (%3%)</source>
        <translation>变化像元：%1 / %2（%3%）</translation>
    </message>
    <message>
        <source>Per-class pixel counts and share changes (earlier → later):</source>
        <translation>类别像元计数与占比变化（前期 → 后期）：</translation>
    </message>
    <message>
        <source>  class %1: %2 (%3%) → %4 (%5%) [net change: %6%7]</source>
        <translation>  类别 %1: %2 (%3%) → %4 (%5%) [净变化: %6%7]</translation>
    </message>
    <message>
        <source>Transition matrix (rows = earlier epoch, columns = later epoch; non-zero transitions only):</source>
        <translation>转移矩阵（行=前时相，列=后时相；仅列出非零转移）：</translation>
    </message>
    <message>
        <source>  class %1 → class %2: %3 pixels</source>
        <translation>  类别 %1 → 类别 %2: %3 像元</translation>
    </message>
    <message>
        <source>Run finished (no summary data).</source>
        <translation>运行完成（无摘要数据）。</translation>
    </message>
    <message>
        <source>Post-Classification Comparison</source>
        <translation>分类后比较</translation>
    </message>
</context>
<context>
    <name>PreferencesDialog</name>
    <message>
        <source>Preferences</source>
        <translation>偏好设置</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>System-wide tabs such as General / external tool paths / About.</source>
        <translation>常规 / 外部工具路径 / 关于 等系统全局配置选项卡。</translation>
    </message>
    <message>
        <source>Interface theme style: light mode or dark mode.</source>
        <translation>界面主题风格：浅色模式或深色模式。</translation>
    </message>
    <message>
        <source>Default projected CRS for new remote-sensing projects.</source>
        <translation>新建遥感工程时的默认投影坐标参考系 (CRS)。</translation>
    </message>
    <message>
        <source>Interface and Default CRS</source>
        <translation>界面与默认坐标系</translation>
    </message>
    <message>
        <source>Light Theme</source>
        <translation>浅色主题 (Light)</translation>
    </message>
    <message>
        <source>Dark Theme</source>
        <translation>深色主题 (Dark)</translation>
    </message>
    <message>
        <source>System interface theme: light mode or dark mode.</source>
        <translation>系统界面外观主题：浅色模式或深色模式。</translation>
    </message>
    <message>
        <source>Interface Theme</source>
        <translation>界面主题</translation>
    </message>
    <message>
        <source>EPSG:4326 - WGS 84 (geographic)</source>
        <translation>EPSG:4326 - WGS 84 (地理坐标系)</translation>
    </message>
    <message>
        <source>EPSG:3857 - WGS 84 / Pseudo-Mercator</source>
        <translation>EPSG:3857 - WGS 84 / 伪墨卡托</translation>
    </message>
    <message>
        <source>EPSG:32649 - WGS 84 / UTM zone 49N</source>
        <translation>EPSG:32649 - WGS 84 / UTM 49N 带</translation>
    </message>
    <message>
        <source>EPSG:32650 - WGS 84 / UTM zone 50N</source>
        <translation>EPSG:32650 - WGS 84 / UTM 50N 带</translation>
    </message>
    <message>
        <source>EPSG:32651 - WGS 84 / UTM zone 51N</source>
        <translation>EPSG:32651 - WGS 84 / UTM 51N 带</translation>
    </message>
    <message>
        <source>EPSG:4490 - CGCS2000 (China Geodetic Coordinate System 2000)</source>
        <translation>EPSG:4490 - CGCS2000 (国家2000大地坐标系)</translation>
    </message>
    <message>
        <source>Default CRS for new remote-sensing projects.</source>
        <translation>新建遥感工程时的默认坐标参考系。</translation>
    </message>
    <message>
        <source>Default CRS</source>
        <translation>默认坐标系</translation>
    </message>
    <message>
        <source>Run Log Settings</source>
        <translation>运行日志配置</translation>
    </message>
    <message>
        <source>Write log file</source>
        <translation>启用日志文件写入</translation>
    </message>
    <message>
        <source>Whether to save system and algorithm run logs to a local disk file.</source>
        <translation>是否将系统与算法运行日志输出保存到本地磁盘文件。</translation>
    </message>
    <message>
        <source>Full path of the log file...</source>
        <translation>日志文件完整路径…</translation>
    </message>
    <message>
        <source>Full file path where the log is saved.</source>
        <translation>保存日志记录的完整文件路径。</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Log File Path</source>
        <translation>日志文件路径</translation>
    </message>
    <message>
        <source>General Settings</source>
        <translation>常规设置</translation>
    </message>
    <message>
        <source>External Tool Directory Settings</source>
        <translation>外部工具目录配置</translation>
    </message>
    <message>
        <source>Specify external command-line tool paths to enable advanced algorithms.</source>
        <translation>指定外部命令行工具路径以启用高级算法功能。</translation>
    </message>
    <message>
        <source>GDAL tools directory (containing gdal_translate, gdalwarp, etc.)...</source>
        <translation>GDAL 工具目录（包含 gdal_translate、gdalwarp 等）…</translation>
    </message>
    <message>
        <source>Path to the GDAL tools directory, used for low-level raster format conversion and reprojection.</source>
        <translation>GDAL 工具目录路径，用于底层栅格格式转换与投影变换。</translation>
    </message>
    <message>
        <source>GDAL Tools Path</source>
        <translation>GDAL 工具路径</translation>
    </message>
    <message>
        <source>Orfeo ToolBox application directory...</source>
        <translation>OTB 应用程序目录路径…</translation>
    </message>
    <message>
        <source>Orfeo ToolBox directory used by the OTB wrapper algorithms.</source>
        <translation>Orfeo ToolBox 工具目录，供 OTB 包装算法调用。</translation>
    </message>
    <message>
        <source>OTB Tools Path</source>
        <translation>OTB 工具路径</translation>
    </message>
    <message>
        <source>External Tools</source>
        <translation>外部工具</translation>
    </message>
    <message>
        <source>Version 1.0.0</source>
        <translation>版本 1.0.0</translation>
    </message>
    <message>
        <source>Integrated remote-sensing image processing and spatial analysis platform</source>
        <translation>遥感影像综合处理与空间分析平台</translation>
    </message>
    <message>
        <source>Built on the QGIS core engine with external GDAL / OTB algorithm libraries</source>
        <translation>基于 QGIS 核心引擎与 GDAL / OTB 外部算法库构建</translation>
    </message>
    <message>
        <source>About</source>
        <translation>关于</translation>
    </message>
    <message>
        <source>Select GDAL Tools Directory</source>
        <translation>选择 GDAL 工具目录</translation>
    </message>
    <message>
        <source>Select OTB Tools Directory</source>
        <translation>选择 OTB 工具目录</translation>
    </message>
    <message>
        <source>Select Log File</source>
        <translation>选择日志文件</translation>
    </message>
    <message>
        <source>Log files (*.log);;All files (*)</source>
        <translation>日志文件 (*.log);;所有文件 (*)</translation>
    </message>
</context>
<context>
    <name>ProductImportDialog</name>
    <message>
        <source>%1 product directory (Landsat: *_MTL.txt; Sentinel-2: .SAFE)</source>
        <translation>%1 产品目录（Landsat 含 *_MTL.txt；Sentinel-2 含 .SAFE）</translation>
    </message>
    <message>
        <source>%1 product directory (product type auto-detected).</source>
        <translation>%1 产品目录路径（自动识别产品类型）。</translation>
    </message>
    <message>
        <source>Browse and choose the %1 product directory.</source>
        <translation>浏览选择 %1 产品目录。</translation>
    </message>
    <message>
        <source>Parses the product metadata and lists importable sub-items and bands.</source>
        <translation>解析产品元数据，列出可导入的子项与波段。</translation>
    </message>
    <message>
        <source>Product Data Source Directory</source>
        <translation>产品数据源目录</translation>
    </message>
    <message>
        <source>Specify the root directory containing the satellite metadata and per-band rasters.</source>
        <translation>指定包含卫星元数据与各波段栅格的根目录。</translation>
    </message>
    <message>
        <source>Product directory (Landsat / Sentinel-2 / MODIS auto-detected)</source>
        <translation>产品目录（自动识别 Landsat / Sentinel-2 / MODIS）</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Detection and Identification</source>
        <translation>探测识别</translation>
    </message>
    <message>
        <source>Discovered Data Sub-items and Band Preview</source>
        <translation>发现的数据子项与波段预览</translation>
    </message>
    <message>
        <source>Tick the sub-items and bands to import into the project asset manager.</source>
        <translation>勾选需要导入到工程资产管理器的子项和波段。</translation>
    </message>
    <message>
        <source>Sub-items (grid groups)</source>
        <translation>子项（网格组）</translation>
    </message>
    <message>
        <source>Included Bands</source>
        <translation>包含波段</translation>
    </message>
    <message>
        <source>Preview the discovered sub-items (grid groups) and bands; tick the bands to import.</source>
        <translation>预览探测到的子项（网格组）与波段；勾选需导入的波段。</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Import Selected</source>
        <translation>导入所选</translation>
    </message>
    <message>
        <source>Imports the selected bands into the project (available after successful detection).</source>
        <translation>导入选中的波段到工程（探测成功后可用）。</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Closes the dialog without importing.</source>
        <translation>关闭对话框，不执行导入。</translation>
    </message>
    <message>
        <source>Choose the %1 product directory</source>
        <translation>选择%1产品目录</translation>
    </message>
    <message>
        <source>Import Failed</source>
        <translation>导入失败</translation>
    </message>
    <message>
        <source>The data manager is unavailable; cannot probe.</source>
        <translation>数据管理器不可用，无法探测。</translation>
    </message>
    <message>
        <source>Choose the product directory first.</source>
        <translation>请先选择产品目录。</translation>
    </message>
    <message>
        <source>Product detection failed.</source>
        <translation>产品探测失败。</translation>
    </message>
    <message>
        <source>Found %1 bands / grid groups; tick the bands to import.</source>
        <translation>发现 %1 个波段/网格组；勾选要导入的波段。</translation>
    </message>
    <message>
        <source>The data manager is unavailable; cannot import.</source>
        <translation>数据管理器不可用，无法导入。</translation>
    </message>
    <message>
        <source>Tick at least one band group before importing.</source>
        <translation>请至少勾选一个波段组再导入。</translation>
    </message>
    <message>
        <source>Import failed.</source>
        <translation>导入失败。</translation>
    </message>
    <message>
        <source>Imported %1 bands into collection "%2".</source>
        <translation>已将 %1 个波段导入集合 "%2"。</translation>
    </message>
    <message>
        <source>Import %1</source>
        <translation>导入 %1</translation>
    </message>
</context>
<context>
    <name>ProgressDialog</name>
    <message>
        <source>Initializing...</source>
        <translation>正在初始化...</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Cancels the current operation (it may not stop immediately).</source>
        <translation>取消当前操作（操作可能不会立即停止）。</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>取消中…</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <source>Remove Layer</source>
        <translation>移除图层</translation>
    </message>
    <message>
        <source>Open Path</source>
        <translation>打开路径</translation>
    </message>
    <message>
        <source>Open Raster</source>
        <translation>打开栅格</translation>
    </message>
    <message>
        <source>Open Vector</source>
        <translation>打开矢量</translation>
    </message>
    <message>
        <source>The active Display View context is unavailable</source>
        <translation>活动显示视图上下文不可用</translation>
    </message>
    <message>
        <source>Data Asset is not in the project catalog</source>
        <translation>数据资产不在工程目录中</translation>
    </message>
    <message>
        <source>The QGIS display adapter was not created</source>
        <translation>未创建 QGIS 显示适配器</translation>
    </message>
    <message>
        <source>The source could not be registered</source>
        <translation>无法注册该数据源</translation>
    </message>
    <message>
        <source>Loaded: %1 (%2x%3, %4 bands)</source>
        <translation>已加载：%1（%2×%3，%4 个波段）</translation>
    </message>
    <message>
        <source>Loaded: %1 (%2 features)</source>
        <translation>已加载：%1（%2 个要素）</translation>
    </message>
    <message>
        <source>Raster Layers</source>
        <translation>栅格图层</translation>
    </message>
    <message>
        <source>Vector Layers</source>
        <translation>矢量图层</translation>
    </message>
    <message>
        <source>The operation failed without details</source>
        <translation>操作失败（无详细信息）</translation>
    </message>
    <message>
        <source>No layer selected</source>
        <translation>未选中任何图层</translation>
    </message>
    <message>
        <source>Remove the selected %1 layers from the display?
(The data assets stay in the project; only the display is removed;External QGIS layers will be removed from the project.)</source>
        <translation>从显示移除选中的 %1 个图层？
（数据资产保留在工程中，仅移除显示；外部 QGIS 图层将从工程移除。）</translation>
    </message>
    <message>
        <source>
... and %1 more</source>
        <translation>
…及其余 %1 个</translation>
    </message>
    <message>
        <source>Remove Display Layer</source>
        <translation>移除显示图层</translation>
    </message>
    <message>
        <source>Removed from view (data kept)</source>
        <translation>已从显示移除（数据保留）</translation>
    </message>
    <message>
        <source>Algorithm completed in %1 seconds</source>
        <translation>算法在 %1 秒内完成</translation>
    </message>
    <message>
        <source>Algorithm failed after %1 seconds: %2</source>
        <translation>算法在 %1 秒后失败：%2</translation>
    </message>
    <message>
        <source>Cannot open the images; pixel-grid compatibility cannot be checked.</source>
        <translation>无法打开影像，无法检查像元网格兼容性。</translation>
    </message>
    <message>
        <source>Skipping auto-load for unsupported output type '%1' (%2).</source>
        <translation>输出类型“%1”不支持自动加载（%2），已跳过。</translation>
    </message>
    <message>
        <source>Output file not found: %1</source>
        <translation>未找到输出文件：%1</translation>
    </message>
    <message>
        <source>Result ready: %1</source>
        <translation>结果就绪：%1</translation>
    </message>
    <message>
        <source>RPC warp destination CRS %1 is not EPSG:4326; output grid is in WGS84 degrees — forcing output CRS to EPSG:4326 to avoid mislabeling. Reproject afterwards if a projected CRS is required.</source>
        <translation>RPC 校正目标 CRS %1 不是 EPSG:4326；输出网格位于 WGS84 经纬度——已强制输出 CRS 为 EPSG:4326 以避免坐标误标。如需投影坐标系请事后重投影。</translation>
    </message>
    <message>
        <source>DEM CRS (%1) differs from target CRS (%2); RPC results may shift</source>
        <translation>DEM 的 CRS（%1）与目标 CRS（%2）不一致，RPC 结果可能出现偏移</translation>
    </message>
    <message>
        <source>I2I Correction</source>
        <translation>I2I 校正</translation>
    </message>
    <message>
        <source>I2M Correction</source>
        <translation>I2M 校正</translation>
    </message>
    <message>
        <source>Running</source>
        <translation>运行中</translation>
    </message>
    <message>
        <source>Done</source>
        <translation>完成</translation>
    </message>
    <message>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Quick Start</source>
        <translation>快速上手</translation>
    </message>
    <message>
        <source>Data and Project</source>
        <translation>数据与工程</translation>
    </message>
    <message>
        <source>Optical Remote Sensing</source>
        <translation>光学遥感</translation>
    </message>
    <message>
        <source>SAR</source>
        <translation>SAR</translation>
    </message>
    <message>
        <source>Time Series Analysis</source>
        <translation>时序分析</translation>
    </message>
    <message>
        <source>Classification</source>
        <translation>分类</translation>
    </message>
    <message>
        <source>Change Detection</source>
        <translation>变化检测</translation>
    </message>
    <message>
        <source>Terrain</source>
        <translation>地形</translation>
    </message>
    <message>
        <source>Workspace</source>
        <translation>工作区</translation>
    </message>
    <message>
        <source>Cartography</source>
        <translation>制图</translation>
    </message>
    <message>
        <source>Agent / Pi</source>
        <translation>智能体 / Pi</translation>
    </message>
    <message>
        <source>Errors and Diagnostics</source>
        <translation>错误与诊断</translation>
    </message>
    <message>
        <source>Shortcuts</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <source>Processing Framework</source>
        <translation>处理框架</translation>
    </message>
    <message>
        <source>Start Here</source>
        <translation>从这里开始</translation>
    </message>
    <message>
        <source>No guidance content has been configured for this panel yet.</source>
        <translation>尚未为此面板配置引导内容。</translation>
    </message>
    <message>
        <source>Learn More (F1)</source>
        <translation>了解更多 (F1)</translation>
    </message>
    <message>
        <source>Add Raster Layer</source>
        <translation>添加栅格图层</translation>
    </message>
    <message>
        <source>Raster Files (*.tif *.tiff *.img *.dat *.pix *.vrt *.nc *.hdf *.h5 *.png *.jpg *.jpeg);;All Files (*.*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.dat *.pix *.vrt *.nc *.hdf *.h5 *.png *.jpg *.jpeg);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Add Vector Layer</source>
        <translation>添加矢量图层</translation>
    </message>
    <message>
        <source>Vector Files (*.shp *.gpkg *.geojson *.kml *.tab *.mif);;All Files (*.*)</source>
        <translation>矢量文件 (*.shp *.gpkg *.geojson *.kml *.tab *.mif);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Add Raster Layer...</source>
        <translation>添加栅格图层...</translation>
    </message>
    <message>
        <source>Open and load a multiband remote-sensing raster layer</source>
        <translation>打开并加载多波段遥感栅格影像图层</translation>
    </message>
    <message>
        <source>Add a raster imagery layer to the current project</source>
        <translation>添加栅格影像图层到当前工程</translation>
    </message>
    <message>
        <source>Add Vector Layer...</source>
        <translation>添加矢量图层...</translation>
    </message>
    <message>
        <source>Open and load a vector feature layer (Shapefile / GeoPackage)</source>
        <translation>打开并加载矢量要素图层 (Shapefile / GeoPackage)</translation>
    </message>
    <message>
        <source>Add a vector layer to the current project</source>
        <translation>添加矢量图层到当前工程</translation>
    </message>
    <message>
        <source>Zoom to Native Resolution (1:1)</source>
        <translation>缩放到原始分辨率 (1:1)</translation>
    </message>
    <message>
        <source>Display the current raster at 1:1 native pixel resolution</source>
        <translation>以 1:1 原始像元分辨率显示当前栅格</translation>
    </message>
    <message>
        <source>Zoom to native pixel resolution</source>
        <translation>缩放到原始像元分辨率</translation>
    </message>
    <message>
        <source>Export Resolution</source>
        <translation>导出分辨率</translation>
    </message>
    <message>
        <source>Resolution (DPI):</source>
        <translation>分辨率（DPI）：</translation>
    </message>
    <message>
        <source>Export Too Large</source>
        <translation>导出尺寸过大</translation>
    </message>
    <message>
        <source>The export would be %1 × %2 pixels, exceeding the %3 pixel edge limit. Please reduce the DPI.</source>
        <translation>导出尺寸将达 %1 × %2 像素，超过 %3 像素的边长上限，请调低 DPI。</translation>
    </message>
    <message>
        <source>The export would need roughly %1 GB of memory, above the %2 GB safety limit. Please reduce the DPI.</source>
        <translation>导出约需 %1 GB 内存，超过 %2 GB 的安全上限，请调低 DPI。</translation>
    </message>
    <message>
        <source>Large Export</source>
        <translation>大尺寸导出</translation>
    </message>
    <message>
        <source>This export needs roughly %1 GB of memory and may take a while. Continue?</source>
        <translation>本次导出约需 %1 GB 内存且耗时较长，是否继续？</translation>
    </message>
    <message>
        <source>Change Opacity</source>
        <translation>更改不透明度</translation>
    </message>
    <message>
        <source>Change Rotation</source>
        <translation>更改旋转</translation>
    </message>
    <message>
        <source>Show Items</source>
        <translation>显示项</translation>
    </message>
    <message>
        <source>Hide Items</source>
        <translation>隐藏项</translation>
    </message>
    <message>
        <source>Lock Items</source>
        <translation>锁定项</translation>
    </message>
    <message>
        <source>Unlock Items</source>
        <translation>解锁项</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Current digitizing technique</source>
        <translation>当前数字化方式</translation>
    </message>
    <message>
        <source>Current shape map tool</source>
        <translation>当前形状地图工具</translation>
    </message>
    <message>
        <source>Default map tool for given shape category</source>
        <translation>给定形状类别的默认地图工具</translation>
    </message>
    <message>
        <source>Circle from 2 points</source>
        <translation>两点画圆</translation>
    </message>
    <message>
        <source>Circle from 2 tangents and a point</source>
        <translation>两切线一点画圆</translation>
    </message>
    <message>
        <source>Circle from 3 points</source>
        <translation>三点画圆</translation>
    </message>
    <message>
        <source>Circle from 3 tangents</source>
        <translation>三切线画圆</translation>
    </message>
    <message>
        <source>Circle by a center point and another point</source>
        <translation>圆心加一点画圆</translation>
    </message>
    <message>
        <source>Circular string by radius</source>
        <translation>按半径绘制圆弧串</translation>
    </message>
    <message>
        <source>Ellipse from center and 2 points</source>
        <translation>圆心加两点绘制椭圆</translation>
    </message>
    <message>
        <source>Ellipse from center and a point</source>
        <translation>圆心加一点绘制椭圆</translation>
    </message>
    <message>
        <source>Ellipse from Extent</source>
        <translation>按范围绘制椭圆</translation>
    </message>
    <message>
        <source>Ellipse from Foci</source>
        <translation>按焦点绘制椭圆</translation>
    </message>
    <message>
        <source>Rectangle from 3 points (distance)</source>
        <translation>三点绘制矩形（距离）</translation>
    </message>
    <message>
        <source>Rectangle from 3 points (projected)</source>
        <translation>三点绘制矩形（投影）</translation>
    </message>
    <message>
        <source>Rectangle from center and a point</source>
        <translation>中心点加一点绘制矩形</translation>
    </message>
    <message>
        <source>Rectangle from extent</source>
        <translation>按范围绘制矩形</translation>
    </message>
    <message>
        <source>Regular polygon from 2 points</source>
        <translation>两点绘制正多边形</translation>
    </message>
    <message>
        <source>Regular polygon from center and a corner</source>
        <translation>中心点加角点绘制正多边形</translation>
    </message>
    <message>
        <source>Regular polygon from center and a point</source>
        <translation>中心点加一点绘制正多边形</translation>
    </message>
    <message>
        <source>Forest</source>
        <translation>林地</translation>
    </message>
    <message>
        <source>Grassland</source>
        <translation>草地</translation>
    </message>
    <message>
        <source>Water</source>
        <translation>水体</translation>
    </message>
    <message>
        <source>Built-up</source>
        <translation>建成区</translation>
    </message>
    <message>
        <source>Cropland</source>
        <translation>耕地</translation>
    </message>
    <message>
        <source>Bare Soil</source>
        <translation>裸地</translation>
    </message>
    <message>
        <source>Cannot open %1</source>
        <translation>无法打开 %1</translation>
    </message>
    <message>
        <source>Empty features CSV: %1</source>
        <translation>要素 CSV 为空：%1</translation>
    </message>
    <message>
        <source>Features CSV has no segment_id column</source>
        <translation>要素 CSV 缺少 segment_id 列</translation>
    </message>
    <message>
        <source>Features CSV has no per-band columns</source>
        <translation>要素 CSV 缺少逐波段列</translation>
    </message>
    <message>
        <source>Malformed features CSV row %1</source>
        <translation>要素 CSV 第 %1 行格式错误</translation>
    </message>
    <message>
        <source>Features CSV contains no data rows</source>
        <translation>要素 CSV 没有数据行</translation>
    </message>
    <message>
        <source>Empty label CSV: %1</source>
        <translation>标签 CSV 为空：%1</translation>
    </message>
    <message>
        <source>Label CSV must have segment_id,class_id columns</source>
        <translation>标签 CSV 必须包含 segment_id 与 class_id 列</translation>
    </message>
    <message>
        <source>Cannot rehydrate fine level from %1</source>
        <translation>无法从 %1 恢复细层级</translation>
    </message>
    <message>
        <source>Cannot rehydrate coarse level from %1</source>
        <translation>无法从 %1 恢复粗层级</translation>
    </message>
    <message>
        <source>Cannot open parents CSV %1</source>
        <translation>无法打开父级 CSV %1</translation>
    </message>
    <message>
        <source>Malformed parents CSV line %1</source>
        <translation>父级 CSV 第 %1 行格式错误</translation>
    </message>
    <message>
        <source>Raster</source>
        <translation>栅格</translation>
    </message>
    <message>
        <source>Vector</source>
        <translation>矢量</translation>
    </message>
    <message>
        <source>Remote Map</source>
        <translation>远程地图</translation>
    </message>
    <message>
        <source>Virtual Raster</source>
        <translation>虚拟栅格</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>Single-band raster</source>
        <translation>单波段栅格</translation>
    </message>
    <message>
        <source>Multiband Raster</source>
        <translation>多波段栅格</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Registered</source>
        <translation>已注册</translation>
    </message>
    <message>
        <source>Parsing</source>
        <translation>解析中</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Source Missing</source>
        <translation>源缺失</translation>
    </message>
    <message>
        <source>Source Unavailable</source>
        <translation>源不可用</translation>
    </message>
    <message>
        <source>Offline</source>
        <translation>离线</translation>
    </message>
    <message>
        <source>Authentication Required</source>
        <translation>需要认证</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Stale</source>
        <translation>过期</translation>
    </message>
    <message>
        <source>Project Persistent</source>
        <translation>工程持久</translation>
    </message>
    <message>
        <source>Session Temporary</source>
        <translation>会话临时</translation>
    </message>
    <message>
        <source>Task Temporary</source>
        <translation>任务临时</translation>
    </message>
    <message>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Temporary Files</source>
        <translation>临时文件</translation>
    </message>
    <message>
        <source>Memory</source>
        <translation>内存</translation>
    </message>
    <message>
        <source>Remote</source>
        <translation>远程</translation>
    </message>
    <message>
        <source>Renderable</source>
        <translation>可渲染</translation>
    </message>
    <message>
        <source>Readable Pixels</source>
        <translation>可读像素</translation>
    </message>
    <message>
        <source>Band Metadata</source>
        <translation>波段元数据</translation>
    </message>
    <message>
        <source>Band Statistics</source>
        <translation>波段统计</translation>
    </message>
    <message>
        <source>Identifiable</source>
        <translation>可查询要素</translation>
    </message>
    <message>
        <source>Editable</source>
        <translation>可编辑要素</translation>
    </message>
    <message>
        <source>Time Series</source>
        <translation>时序</translation>
    </message>
    <message>
        <source>Offline-cacheable</source>
        <translation>可离线缓存</translation>
    </message>
    <message>
        <source>Exportable</source>
        <translation>可导出</translation>
    </message>
    <message>
        <source>Re-linkable</source>
        <translation>可重定位</translation>
    </message>
    <message>
        <source>Source Removable</source>
        <translation>可删除源</translation>
    </message>
    <message>
        <source>(none)</source>
        <translation>（无）</translation>
    </message>
    <message>
        <source>Drivers</source>
        <translation>驱动</translation>
    </message>
    <message>
        <source>(unknown)</source>
        <translation>（未知）</translation>
    </message>
    <message>
        <source>Size</source>
        <translation>尺寸</translation>
    </message>
    <message>
        <source>Band Count</source>
        <translation>波段数</translation>
    </message>
    <message>
        <source>CRS</source>
        <translation>CRS</translation>
    </message>
    <message>
        <source>Extent</source>
        <translation>范围</translation>
    </message>
    <message>
        <source>Affine Transform</source>
        <translation>仿射变换</translation>
    </message>
    <message>
        <source>None</source>
        <translation>不使用</translation>
    </message>
    <message>
        <source>Type unknown</source>
        <translation>类型未知</translation>
    </message>
    <message>
        <source>(band details not listed)</source>
        <translation>（未列出波段明细）</translation>
    </message>
    <message>
        <source>Raster Structure</source>
        <translation>栅格结构</translation>
    </message>
    <message>
        <source>Band</source>
        <translation>波段</translation>
    </message>
    <message>
        <source>Layer Count</source>
        <translation>图层数</translation>
    </message>
    <message>
        <source>• %1 · %2 · features %3 · %4&lt;br/&gt;%5&lt;br/&gt;</source>
        <translation>• %1 · %2 · 要素 %3 · %4&lt;br/&gt;%5&lt;br/&gt;</translation>
    </message>
    <message>
        <source>Geometry Unknown</source>
        <translation>几何未知</translation>
    </message>
    <message>
        <source>No CRS</source>
        <translation>CRS 无</translation>
    </message>
    <message>
        <source>(sub-layers not listed)</source>
        <translation>（未列出子图层）</translation>
    </message>
    <message>
        <source>Vector Structure</source>
        <translation>矢量结构</translation>
    </message>
    <message>
        <source>Sub-layers</source>
        <translation>子图层</translation>
    </message>
    <message>
        <source>Service Type</source>
        <translation>服务类型</translation>
    </message>
    <message>
        <source>Layer</source>
        <translation>图层</translation>
    </message>
    <message>
        <source>CRS list</source>
        <translation>CRS 列表</translation>
    </message>
    <message>
        <source>Format</source>
        <translation>格式</translation>
    </message>
    <message>
        <source>Pixel Size</source>
        <translation>像元大小</translation>
    </message>
    <message>
        <source>Zoom Level</source>
        <translation>缩放级别</translation>
    </message>
    <message>
        <source>Valid</source>
        <translation>有效</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <source>Remote Map Structure</source>
        <translation>远程地图结构</translation>
    </message>
    <message>
        <source>Structure</source>
        <translation>结构</translation>
    </message>
    <message>
        <source>Not parsed yet / no structural information</source>
        <translation>尚未解析 / 无结构信息</translation>
    </message>
    <message>
        <source>Data Management</source>
        <translation>数据管理</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Persistence</source>
        <translation>持久性</translation>
    </message>
    <message>
        <source>References</source>
        <translation>引用</translation>
    </message>
    <message>
        <source>The color bar on the left shows status (green = available, red = unavailable); the type prefixes the name</source>
        <translation>左侧色条表示状态（绿=可用，红=不可用）；类型作为名称前缀</translation>
    </message>
    <message>
        <source>Filter by name / path / ID...</source>
        <translation>按名称 / 路径 / ID 过滤…</translation>
    </message>
    <message>
        <source>Filter Data Assets</source>
        <translation>过滤数据资产</translation>
    </message>
    <message>
        <source>Previous Page</source>
        <translation>上一页</translation>
    </message>
    <message>
        <source>Asset Pagination Status</source>
        <translation>资产分页状态</translation>
    </message>
    <message>
        <source>Next Page</source>
        <translation>下一页</translation>
    </message>
    <message>
        <source>No data assets yet</source>
        <translation>暂无数据资产</translation>
    </message>
    <message>
        <source>No data assets or collections registered yet. Import remote-sensing imagery, vector files or hyperspectral data to start.</source>
        <translation>暂未登记任何数据资产或集合。导入遥感影像、矢量文件或高光谱数据开始工作。</translation>
    </message>
    <message>
        <source>Import Data Assets...</source>
        <translation>导入数据资产...</translation>
    </message>
    <message>
        <source>Meta Information</source>
        <translation>元信息</translation>
    </message>
    <message>
        <source>Select a data asset or collection to view its meta information.</source>
        <translation>选择数据资产或集合以查看元信息。</translation>
    </message>
    <message>
        <source>Data management: catalog of project data assets and collections; right-click to add to display, promote, unload or view properties.</source>
        <translation>数据管理：工程数据资产与集合目录；右键可添加到显示、提升、卸载、查看属性。</translation>
    </message>
    <message>
        <source>Asset / collection tree. The color bar on the left shows status (green = available, red = unavailable). Double-click = add to display; right-click for more actions.</source>
        <translation>资产/集合树。左侧色条表示状态（绿=可用，红=不可用）。双击=添加到显示；右键更多操作。</translation>
    </message>
    <message>
        <source>Meta information inspector for the selected assets (path, CRS, band / layer structure, etc.).</source>
        <translation>选中资产的元信息检视器（路径、CRS、波段/图层结构等）。</translation>
    </message>
    <message>
        <source>Title of the current inspector item.</source>
        <translation>当前检视项标题。</translation>
    </message>
    <message>
        <source>Drag the splitter to adjust the heights of the catalog tree and the inspector.</source>
        <translation>拖动分隔条调整目录树与检视器的高度。</translation>
    </message>
    <message>
        <source>%1
Status: source missing — recoverable by re-linking
%2</source>
        <translation>%1
状态: 源缺失 — 可通过重定位恢复
%2</translation>
    </message>
    <message>
        <source>Assets</source>
        <translation>资产</translation>
    </message>
    <message>
        <source>Showing first %1 of %2 items — use the filter to narrow down</source>
        <translation>仅显示前 %1 项 / 共 %2 项 — 使用过滤缩小范围</translation>
    </message>
    <message>
        <source>The data manager is unavailable.</source>
        <translation>数据管理器不可用。</translation>
    </message>
    <message>
        <source>Collections</source>
        <translation>集合</translation>
    </message>
    <message>
        <source>Epoch Collection</source>
        <translation>时间相集合</translation>
    </message>
    <message>
        <source>Workspace Records</source>
        <translation>工作区记录</translation>
    </message>
    <message>
        <source>Epoch collection (multitemporal scene collection)</source>
        <translation>时间相集合（多时相场景集合）</translation>
    </message>
    <message>
        <source>Revision %1</source>
        <translation>修订 %1</translation>
    </message>
    <message>
        <source>Items %1–%2 of %3 assets (page %4/%5)</source>
        <translation>第 %1–%2 项 / 共 %3 项资产（第 %4/%5 页）</translation>
    </message>
    <message>
        <source>Page %1/%2 · %3 items in total</source>
        <translation>第 %1/%2 页 · 共 %3 项</translation>
    </message>
    <message>
        <source>Opens and processes this collection in the time series analysis dialog.</source>
        <translation>在时间序列分析对话框中打开并处理该集合。</translation>
    </message>
    <message>
        <source>Precheck Collection</source>
        <translation>预检集合</translation>
    </message>
    <message>
        <source>Checks raster alignment, time and platform consistency of the collection's scenes.</source>
        <translation>检查该集合场景的栅格对齐、时间与平台一致性。</translation>
    </message>
    <message>
        <source>View Collection Info</source>
        <translation>查看集合信息</translation>
    </message>
    <message>
        <source>Shows scene count, time range and platform of this epoch collection.</source>
        <translation>显示该时间相集合的场景数、时间范围与平台。</translation>
    </message>
    <message>
        <source>Remove Collection Record</source>
        <translation>移除集合记录</translation>
    </message>
    <message>
        <source>Removes the record from the workspace (no scene data is deleted).</source>
        <translation>从工作区移除该记录（不删除任何场景数据）。</translation>
    </message>
    <message>
        <source>Precheck result: %1
Total scenes: %2
Valid times: %3
</source>
        <translation>预检结果：%1
场景总数：%2
有效时间：%3
</translation>
    </message>
    <message>
        <source>Passed</source>
        <translation>通过</translation>
    </message>
    <message>
        <source>
Errors:
- </source>
        <translation>
错误：
- </translation>
    </message>
    <message>
        <source>
Warnings:
- </source>
        <translation>
警告：
- </translation>
    </message>
    <message>
        <source>Collection Precheck Report</source>
        <translation>集合预检报告</translation>
    </message>
    <message>
        <source>Precheck Failed</source>
        <translation>预检失败</translation>
    </message>
    <message>
        <source>Cannot parse the collection descriptor: %1</source>
        <translation>无法解析集合描述符：%1</translation>
    </message>
    <message>
        <source>Name: %1
Revision: %2</source>
        <translation>名称：%1
修订：%2</translation>
    </message>
    <message>
        <source>Scenes: %1 (%2 assets bound)</source>
        <translation>场景数：%1（已绑定资产 %2）</translation>
    </message>
    <message>
        <source>Time range: %1 … %2</source>
        <translation>时间范围：%1 … %2</translation>
    </message>
    <message>
        <source>Platform: %1</source>
        <translation>平台：%1</translation>
    </message>
    <message>
        <source>Invalid descriptor: %1</source>
        <translation>描述符无效：%1</translation>
    </message>
    <message>
        <source>Remove Epoch Collection</source>
        <translation>移除时间相集合</translation>
    </message>
    <message>
        <source>Remove collection %1? Scene data will not be deleted.</source>
        <translation>移除集合“%1”？场景数据不会被删除。</translation>
    </message>
    <message>
        <source>Add to Display</source>
        <translation>添加到显示</translation>
    </message>
    <message>
        <source>Add to Display (%1 items)</source>
        <translation>添加到显示（%1 项）</translation>
    </message>
    <message>
        <source>Loads the selected assets as layers into the current view.</source>
        <translation>把选中资产作为图层加载到当前视图。</translation>
    </message>
    <message>
        <source>View Properties</source>
        <translation>查看属性</translation>
    </message>
    <message>
        <source>Refreshes this asset's meta information in the inspector below.</source>
        <translation>在下方检视器中刷新该资产的元信息。</translation>
    </message>
    <message>
        <source>Copy Source Path</source>
        <translation>复制源路径</translation>
    </message>
    <message>
        <source>Copy Source Paths (%1 items)</source>
        <translation>复制源路径（%1 项）</translation>
    </message>
    <message>
        <source>Copies the asset source path (canonicalSource) to the clipboard.</source>
        <translation>把资产源路径（canonicalSource）复制到剪贴板。</translation>
    </message>
    <message>
        <source>Promote to Project Persistent...</source>
        <translation>提升为工程持久…</translation>
    </message>
    <message>
        <source>Promote to Project Persistent (%1 items)...</source>
        <translation>提升为工程持久（%1 项）…</translation>
    </message>
    <message>
        <source>Promotes temporary assets to project-persistent (saved with the project).</source>
        <translation>把临时资产提升为工程持久（随工程保存）。</translation>
    </message>
    <message>
        <source>Re-link Missing Source...</source>
        <translation>重定位缺失源…</translation>
    </message>
    <message>
        <source>Assign a new source location to missing/unavailable assets so they can be resolved again.</source>
        <translation>为缺失/不可用的资产指定新的源位置以重新解析。</translation>
    </message>
    <message>
        <source>Unload...</source>
        <translation>卸载…</translation>
    </message>
    <message>
        <source>Unload (%1 items)...</source>
        <translation>卸载（%1 项）…</translation>
    </message>
    <message>
        <source>Unload the selected assets from the project (a confirmation pops up; dependents are removed cascadingly).</source>
        <translation>从工程卸载选中资产（会弹出确认；若有引用将级联移除）。</translation>
    </message>
    <message>
        <source>Select a data asset or collection to view its meta information. Ctrl / Shift multi-select.</source>
        <translation>选择数据资产或集合以查看元信息。Ctrl/Shift 可多选。</translation>
    </message>
    <message>
        <source>Asset Meta Information — %1</source>
        <translation>资产元信息 — %1</translation>
    </message>
    <message>
        <source>Display Name</source>
        <translation>显示名</translation>
    </message>
    <message>
        <source>Asset ID</source>
        <translation>资产 ID</translation>
    </message>
    <message>
        <source>Revision</source>
        <translation>修订</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>存储</translation>
    </message>
    <message>
        <source>Capabilities</source>
        <translation>能力</translation>
    </message>
    <message>
        <source>Show References</source>
        <translation>显示引用</translation>
    </message>
    <message>
        <source>Collection</source>
        <translation>所属集合</translation>
    </message>
    <message>
        <source>Provider</source>
        <translation>提供者</translation>
    </message>
    <message>
        <source>(automatic)</source>
        <translation>（自动）</translation>
    </message>
    <message>
        <source>Path / URI</source>
        <translation>路径 / URI</translation>
    </message>
    <message>
        <source>Sub-datasets</source>
        <translation>子数据集</translation>
    </message>
    <message>
        <source>Authentication Settings</source>
        <translation>认证配置</translation>
    </message>
    <message>
        <source>Data Options</source>
        <translation>数据选项</translation>
    </message>
    <message>
        <source>Algorithm</source>
        <translation>算法</translation>
    </message>
    <message>
        <source>Algorithm Version</source>
        <translation>算法版本</translation>
    </message>
    <message>
        <source>Parameters</source>
        <translation>参数</translation>
    </message>
    <message>
        <source>Task References</source>
        <translation>任务引用</translation>
    </message>
    <message>
        <source>Finish Time</source>
        <translation>完成时间</translation>
    </message>
    <message>
        <source>Derived from</source>
        <translation>源自</translation>
    </message>
    <message>
        <source>Provenance</source>
        <translation>溯源</translation>
    </message>
    <message>
        <source>No derivation record (registered directly)</source>
        <translation>无派生记录（直接注册）</translation>
    </message>
    <message>
        <source>Derived Artifacts</source>
        <translation>派生产物</translation>
    </message>
    <message>
        <source>Identity and Status</source>
        <translation>标识与状态</translation>
    </message>
    <message>
        <source>Data Source</source>
        <translation>数据源</translation>
    </message>
    <message>
        <source>Provenance and Lineage</source>
        <translation>溯源与谱系</translation>
    </message>
    <message>
        <source>Multiple selection — %1 items</source>
        <translation>多选 — %1 项</translation>
    </message>
    <message>
        <source>Selection Count</source>
        <translation>选中数量</translation>
    </message>
    <message>
        <source>Temporary Assets</source>
        <translation>临时资产</translation>
    </message>
    <message>
        <source>Raster Class</source>
        <translation>栅格类</translation>
    </message>
    <message>
        <source>%1 assets selected</source>
        <translation>已选择 %1 个资产</translation>
    </message>
    <message>
        <source>Summary</source>
        <translation>汇总</translation>
    </message>
    <message>
        <source>List</source>
        <translation>列表</translation>
    </message>
    <message>
        <source>Right-click for batch actions: add to display / promote / unload.</source>
        <translation>右键可批量：添加到显示 / 提升 / 卸载。</translation>
    </message>
    <message>
        <source>Collection Meta Information — %1</source>
        <translation>集合元信息 — %1</translation>
    </message>
    <message>
        <source>Collection ID</source>
        <translation>集合 ID</translation>
    </message>
    <message>
        <source>Sub-asset Count</source>
        <translation>子资产数</translation>
    </message>
    <message>
        <source>Platform</source>
        <translation>平台</translation>
    </message>
    <message>
        <source>Sensor</source>
        <translation>传感器</translation>
    </message>
    <message>
        <source>Product Level</source>
        <translation>产品级别</translation>
    </message>
    <message>
        <source>Acquisition Date</source>
        <translation>获取日期</translation>
    </message>
    <message>
        <source>Processing Level</source>
        <translation>处理级别</translation>
    </message>
    <message>
        <source>Extended Properties</source>
        <translation>扩展属性</translation>
    </message>
    <message>
        <source>(no sub-assets)</source>
        <translation>（无子资产）</translation>
    </message>
    <message>
        <source>Product Metadata</source>
        <translation>产品元数据</translation>
    </message>
    <message>
        <source>Sub-assets</source>
        <translation>子资产</translation>
    </message>
    <message>
        <source>Loading preview...</source>
        <translation>预览加载中…</translation>
    </message>
    <message>
        <source>Data Asset Preview — %1</source>
        <translation>数据资产预览 — %1</translation>
    </message>
    <message>
        <source>Preview Unavailable</source>
        <translation>预览不可用</translation>
    </message>
    <message>
        <source>No active vector layer</source>
        <translation>无活动矢量图层</translation>
    </message>
    <message>
        <source>To select features, choose a vector layer in the layers panel</source>
        <translation>要选择要素，请先在图层面板中选择矢量图层</translation>
    </message>
    <message>
        <source>CRS Exception</source>
        <translation>CRS 异常</translation>
    </message>
    <message>
        <source>Selection extends beyond layer's coordinate system</source>
        <translation>选择范围超出图层坐标系</translation>
    </message>
    <message>
        <source>Error determining selection: %1</source>
        <translation>确定选择集时出错：%1</translation>
    </message>
    <message>
        <source>Waiting for available resources</source>
        <translation>等待可用资源</translation>
    </message>
    <message>
        <source>Starting</source>
        <translation>启动中</translation>
    </message>
    <message>
        <source>Cancellation in progress</source>
        <translation>正在取消</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation>排队</translation>
    </message>
    <message>
        <source>Waiting for Resources</source>
        <translation>等待资源</translation>
    </message>
    <message>
        <source>Scheduling</source>
        <translation>调度中</translation>
    </message>
    <message>
        <source>Cancelling</source>
        <translation>取消中</translation>
    </message>
    <message>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <source>Succeeded</source>
        <translation>成功</translation>
    </message>
    <message>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <source>... (%1 option source truncated to the first %2 items)</source>
        <translation>…（%1 选项源已截断到前 %2 项）</translation>
    </message>
    <message>
        <source>Moved vertices</source>
        <translation>已移动顶点</translation>
    </message>
    <message>
        <source>Simplify transform error caught: %1</source>
        <translation>简化变换发生错误：%1</translation>
    </message>
    <message>
        <source>Elapsed %1 s</source>
        <translation>耗时 %1 s</translation>
    </message>
    <message>
        <source>Cache Hit</source>
        <translation>缓存命中</translation>
    </message>
    <message>
        <source>✓ Processing finished%1</source>
        <translation>✓ 处理完成%1</translation>
    </message>
    <message>
        <source>... (see the raw JSON for the rest)</source>
        <translation>…（其余见原始 JSON）</translation>
    </message>
    <message>
        <source>Double-click to load into the main view</source>
        <translation>双击加载到主图</translation>
    </message>
    <message>
        <source>New Project</source>
        <translation>新建工程</translation>
    </message>
    <message>
        <source>Create an empty project, clearing current layers and view state.</source>
        <translation>创建空白工程，清除当前图层与视图状态。</translation>
    </message>
    <message>
        <source>Project</source>
        <translation>工程</translation>
    </message>
    <message>
        <source>Open Project...</source>
        <translation>打开工程...</translation>
    </message>
    <message>
        <source>Opens a saved project file.</source>
        <translation>打开已保存的工程文件。</translation>
    </message>
    <message>
        <source>Save Project</source>
        <translation>保存工程</translation>
    </message>
    <message>
        <source>Saves the current project to its existing path.</source>
        <translation>保存当前工程到已有路径。</translation>
    </message>
    <message>
        <source>Save Project As...</source>
        <translation>工程另存为...</translation>
    </message>
    <message>
        <source>Saves the project as a new file.</source>
        <translation>将工程另存为新文件。</translation>
    </message>
    <message>
        <source>Import Layer...</source>
        <translation>导入图层...</translation>
    </message>
    <message>
        <source>Import raster or vector layers into the project.</source>
        <translation>导入栅格或矢量图层到工程。</translation>
    </message>
    <message>
        <source>Browse STAC Catalog...</source>
        <translation>浏览 STAC 目录...</translation>
    </message>
    <message>
        <source>Browses STAC catalogs to find remote-sensing data.</source>
        <translation>浏览 STAC 目录检索遥感数据。</translation>
    </message>
    <message>
        <source>New Layout...</source>
        <translation>新建布局...</translation>
    </message>
    <message>
        <source>Create a print layout / map product.</source>
        <translation>创建打印布局 / 出图。</translation>
    </message>
    <message>
        <source>Exit</source>
        <translation>退出</translation>
    </message>
    <message>
        <source>Quits the application.</source>
        <translation>退出应用程序。</translation>
    </message>
    <message>
        <source>Add a raster layer from a file.</source>
        <translation>从文件添加栅格图层。</translation>
    </message>
    <message>
        <source>Add a vector layer from a file.</source>
        <translation>从文件添加矢量图层。</translation>
    </message>
    <message>
        <source>Layer Properties...</source>
        <translation>图层属性...</translation>
    </message>
    <message>
        <source>Opens the current layer properties.</source>
        <translation>打开当前图层属性。</translation>
    </message>
    <message>
        <source>Remove the current layer from the project.</source>
        <translation>从工程中移除当前图层。</translation>
    </message>
    <message>
        <source>Zoom to Layer</source>
        <translation>缩放到图层</translation>
    </message>
    <message>
        <source>Zooms to the current layer's extent.</source>
        <translation>缩放到当前图层范围。</translation>
    </message>
    <message>
        <source>Toggle Editing</source>
        <translation>切换编辑</translation>
    </message>
    <message>
        <source>Toggles editing of the current vector layer.</source>
        <translation>开启/关闭当前矢量图层编辑。</translation>
    </message>
    <message>
        <source>Vector Editing</source>
        <translation>矢量编辑</translation>
    </message>
    <message>
        <source>Save Edits</source>
        <translation>保存编辑</translation>
    </message>
    <message>
        <source>Saves vector edits.</source>
        <translation>保存矢量编辑。</translation>
    </message>
    <message>
        <source>New Shapefile Layer...</source>
        <translation>新建 Shapefile 图层...</translation>
    </message>
    <message>
        <source>Create a new Shapefile vector layer.</source>
        <translation>创建新的 Shapefile 矢量图层。</translation>
    </message>
    <message>
        <source>Open Attribute Table</source>
        <translation>打开属性表</translation>
    </message>
    <message>
        <source>View/edit the current vector layer's attribute table.</source>
        <translation>查看/编辑当前矢量图层属性表。</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zooms the map view in.</source>
        <translation>放大地图视图。</translation>
    </message>
    <message>
        <source>Map</source>
        <translation>地图</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Zooms the map view out.</source>
        <translation>缩小地图视图。</translation>
    </message>
    <message>
        <source>Full Extent</source>
        <translation>全图</translation>
    </message>
    <message>
        <source>Zooms to the extent of all layers.</source>
        <translation>缩放到所有图层范围。</translation>
    </message>
    <message>
        <source>Pan</source>
        <translation>平移</translation>
    </message>
    <message>
        <source>Pans the map.</source>
        <translation>平移地图。</translation>
    </message>
    <message>
        <source>Identify</source>
        <translation>识别</translation>
    </message>
    <message>
        <source>Click the map to query feature / pixel attributes.</source>
        <translation>点击地图查询要素/像元属性。</translation>
    </message>
    <message>
        <source>Measure Distance</source>
        <translation>测距</translation>
    </message>
    <message>
        <source>Measures distance.</source>
        <translation>量测距离。</translation>
    </message>
    <message>
        <source>Measure Area</source>
        <translation>测面</translation>
    </message>
    <message>
        <source>Measures area.</source>
        <translation>量测面积。</translation>
    </message>
    <message>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <source>Refreshes the map rendering.</source>
        <translation>刷新地图渲染。</translation>
    </message>
    <message>
        <source>Layer Comparison...</source>
        <translation>图层对比...</translation>
    </message>
    <message>
        <source>Compare two layers side by side.</source>
        <translation>左右并排对比两个图层。</translation>
    </message>
    <message>
        <source>Swipe Comparison</source>
        <translation>卷帘对比</translation>
    </message>
    <message>
        <source>Drag the divider on the map to compare the layers above and below.</source>
        <translation>在地图上拖动分割线对比上下图层。</translation>
    </message>
    <message>
        <source>Processing History</source>
        <translation>处理历史</translation>
    </message>
    <message>
        <source>View the unified processing history across the Task Center / workflows.</source>
        <translation>查看跨任务中心/工作流的统一处理历史。</translation>
    </message>
    <message>
        <source>Temporal Workbench</source>
        <translation>时序工作台</translation>
    </message>
    <message>
        <source>Browse time series collections, filter dates and preview epochs.</source>
        <translation>浏览时序集合、筛选日期并预览时相。</translation>
    </message>
    <message>
        <source>Datasets and Experiments</source>
        <translation>数据集与实验</translation>
    </message>
    <message>
        <source>Browse dataset versions, samples, runs and metric comparisons.</source>
        <translation>浏览数据集版本、样本、运行与指标对比。</translation>
    </message>
    <message>
        <source>Model Workbench</source>
        <translation>模型工作台</translation>
    </message>
    <message>
        <source>Browse the model catalog, readiness and submit test inference.</source>
        <translation>查看模型目录、就绪状态并提交测试推理。</translation>
    </message>
    <message>
        <source>Classification Workspace...</source>
        <translation>分类工作区...</translation>
    </message>
    <message>
        <source>Opens the interactive supervised/unsupervised classification workspace.</source>
        <translation>打开监督/非监督分类交互工作区。</translation>
    </message>
    <message>
        <source>Image-to-Image Registration (I2I)...</source>
        <translation>影像对影像配准 (I2I)...</translation>
    </message>
    <message>
        <source>Two-canvas SRC|REF ground-point registration with SIFT support. No RPC.</source>
        <translation>双画布 SRC|REF 同名点配准，支持 SIFT。不含 RPC。</translation>
    </message>
    <message>
        <source>Image-to-Map Registration (I2M)...</source>
        <translation>影像对地图配准 (I2M)...</translation>
    </message>
    <message>
        <source>Source image + main-project map picking; RPC Physical supported.</source>
        <translation>源影像 + 主工程地图取点；支持 RPC Physical。</translation>
    </message>
    <message>
        <source>Object-Based Classification (OBIA)...</source>
        <translation>对象级分类 (OBIA)...</translation>
    </message>
    <message>
        <source>Segmentation + object features + object-based classification.</source>
        <translation>分割 + 对象特征 + 面向对象分类。</translation>
    </message>
    <message>
        <source>Processing</source>
        <translation>处理</translation>
    </message>
    <message>
        <source>Band Math...</source>
        <translation>波段运算...</translation>
    </message>
    <message>
        <source>Expression-driven multiband math.</source>
        <translation>表达式驱动的多波段运算。</translation>
    </message>
    <message>
        <source>Spectral Indices...</source>
        <translation>光谱指数...</translation>
    </message>
    <message>
        <source>Common indices such as NDVI / NDWI / NDBI.</source>
        <translation>NDVI / NDWI / NDBI 等常用指数计算。</translation>
    </message>
    <message>
        <source>Contrast Stretch...</source>
        <translation>对比度拉伸...</translation>
    </message>
    <message>
        <source>Linear / percent clip / histogram equalization output.</source>
        <translation>线性 / 百分比裁剪 / 直方图均衡输出。</translation>
    </message>
    <message>
        <source>Spatial Filtering...</source>
        <translation>空间滤波...</translation>
    </message>
    <message>
        <source>Mean / Gaussian / median / Laplacian convolution.</source>
        <translation>均值 / 高斯 / 中值 / 拉普拉斯卷积。</translation>
    </message>
    <message>
        <source>Principal Component Analysis...</source>
        <translation>主成分分析...</translation>
    </message>
    <message>
        <source>Multiband PCA forward and inverse transforms.</source>
        <translation>多波段 PCA 变换与逆变换。</translation>
    </message>
    <message>
        <source>Band Ratio...</source>
        <translation>波段比值...</translation>
    </message>
    <message>
        <source>Two-band ratio / normalized-ratio output.</source>
        <translation>两波段比值 / 归一化比值输出。</translation>
    </message>
    <message>
        <source>Mosaic...</source>
        <translation>镶嵌...</translation>
    </message>
    <message>
        <source>Mosaic multiple rasters into a continuous image.</source>
        <translation>多景栅格镶嵌为连续影像。</translation>
    </message>
    <message>
        <source>Change Detection...</source>
        <translation>变化检测...</translation>
    </message>
    <message>
        <source>Two-date differencing / ratio / CVA detection.</source>
        <translation>双时相差异 / 比值 / CVA 检测。</translation>
    </message>
    <message>
        <source>Atmospheric Correction...</source>
        <translation>大气校正...</translation>
    </message>
    <message>
        <source>6S / DOS reflectance products.</source>
        <translation>6S / DOS 反射率产品。</translation>
    </message>
    <message>
        <source>Generate QA Mask...</source>
        <translation>QA 掩膜生成...</translation>
    </message>
    <message>
        <source> Decodes Landsat/Sentinel QA bands into a mask.</source>
        <translation> Landsat/Sentinel QA 波段解码为掩膜。</translation>
    </message>
    <message>
        <source>Apply Mask...</source>
        <translation>应用掩膜...</translation>
    </message>
    <message>
        <source>Clip with a mask / set NoData.</source>
        <translation>以掩膜裁剪/置 NoData。</translation>
    </message>
    <message>
        <source>Radiometric Calibration...</source>
        <translation>辐射定标...</translation>
    </message>
    <message>
        <source>DN → radiance / reflectance.</source>
        <translation>DN → 辐亮度 / 反射率。</translation>
    </message>
    <message>
        <source>Orthorectification...</source>
        <translation>正射校正...</translation>
    </message>
    <message>
        <source>RPC / GCP geometric correction to map coordinates.</source>
        <translation>RPC / GCP 几何校正到地图坐标。</translation>
    </message>
    <message>
        <source>Terrain Analysis...</source>
        <translation>地形分析...</translation>
    </message>
    <message>
        <source>DEM products such as slope / aspect / hillshade.</source>
        <translation>坡度 / 坡向 / 山体阴影等 DEM 产品。</translation>
    </message>
    <message>
        <source>Image Fusion...</source>
        <translation>影像融合...</translation>
    </message>
    <message>
        <source>Pansharpening (Brovey / IHS / Gram-Schmidt).</source>
        <translation>全色锐化 (Brovey / IHS / Gram-Schmidt)。</translation>
    </message>
    <message>
        <source>Time Series Analysis...</source>
        <translation>时间序列分析...</translation>
    </message>
    <message>
        <source>Time-series NDVI / phenology curve analysis.</source>
        <translation>时序 NDVI / 物候曲线分析。</translation>
    </message>
    <message>
        <source>Speckle Filtering (SAR)...</source>
        <translation>斑点滤波 (SAR)...</translation>
    </message>
    <message>
        <source>Lee / Frost / Kuan / Gamma-MAP. Requires a SAR raster.</source>
        <translation>Lee / Frost / Kuan / Gamma-MAP。需要 SAR 栅格。</translation>
    </message>
    <message>
        <source>A raster layer must be selected</source>
        <translation>需要选中栅格图层</translation>
    </message>
    <message>
        <source>SAR data must be selected</source>
        <translation>需要选中 SAR 数据</translation>
    </message>
    <message>
        <source>Extract Band...</source>
        <translation>提取波段...</translation>
    </message>
    <message>
        <source>Extract and save a single band from a multiband raster.</source>
        <translation>从多波段栅格提取单一波段保存。</translation>
    </message>
    <message>
        <source>New Workflow</source>
        <translation>新建工作流</translation>
    </message>
    <message>
        <source>Creates an empty workflow in the pipeline editor.</source>
        <translation>在流程编辑器中新建空工作流。</translation>
    </message>
    <message>
        <source>Workflow</source>
        <translation>工作流</translation>
    </message>
    <message>
        <source>Open Workflow...</source>
        <translation>打开工作流...</translation>
    </message>
    <message>
        <source>Opens a .json workflow file in the pipeline editor.</source>
        <translation>打开 .json 工作流文件到流程编辑器。</translation>
    </message>
    <message>
        <source>Save Workflow</source>
        <translation>保存工作流</translation>
    </message>
    <message>
        <source>Saves the current workflow as a .json file.</source>
        <translation>保存当前工作流为 .json 文件。</translation>
    </message>
    <message>
        <source>Run Full Pipeline</source>
        <translation>运行全流程</translation>
    </message>
    <message>
        <source>Schedules and runs the current workflow in topological order.</source>
        <translation>按拓扑顺序调度执行当前工作流。</translation>
    </message>
    <message>
        <source>Stop Workflow</source>
        <translation>停止工作流</translation>
    </message>
    <message>
        <source>Stops the running pipeline task.</source>
        <translation>停止正在运行的流程任务。</translation>
    </message>
    <message>
        <source>Features provided by plugin %1.</source>
        <translation>插件 %1 提供的功能。</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Created</source>
        <translation>已创建</translation>
    </message>
    <message>
        <source>Planned</source>
        <translation>规划中</translation>
    </message>
    <message>
        <source>Interrupted (resumable)</source>
        <translation>已中断（可恢复）</translation>
    </message>
    <message>
        <source>... (parameter snapshot truncated)</source>
        <translation>…（参数快照已截断）</translation>
    </message>
    <message>
        <source>Source File Missing</source>
        <translation>源文件缺失</translation>
    </message>
    <message>
        <source>Inputs Unavailable</source>
        <translation>输入不可用</translation>
    </message>
    <message>
        <source>Content Expired</source>
        <translation>内容已过期</translation>
    </message>
    <message>
        <source>A vector layer must be selected</source>
        <translation>需要选中矢量图层</translation>
    </message>
    <message>
        <source>The current layer is not editable</source>
        <translation>当前图层不可编辑</translation>
    </message>
    <message>
        <source>Start an editing session first</source>
        <translation>请先开启编辑会话</translation>
    </message>
    <message>
        <source>A layer must be selected</source>
        <translation>需要选中图层</translation>
    </message>
    <message>
        <source>Governance results must be selected</source>
        <translation>需要选中治理结果</translation>
    </message>
    <message>
        <source>Data assets must be selected</source>
        <translation>需要选中数据资产</translation>
    </message>
    <message>
        <source>An editing session is active — save or discard the edits</source>
        <translation>编辑会话进行中 — 可保存或放弃编辑</translation>
    </message>
    <message>
        <source>Raster selected — processing tools such as spectral indices can run</source>
        <translation>已选中栅格 — 可运行光谱指数等处理工具</translation>
    </message>
    <message>
        <source>Vector layer selected — editing can start</source>
        <translation>已选中矢量图层 — 可开始编辑</translation>
    </message>
    <message>
        <source>Epoch data detected — the Temporal Workbench is available</source>
        <translation>检测到时相数据 — 可进入时相工作台</translation>
    </message>
    <message>
        <source>A task is running — check progress and artifacts in the processing history</source>
        <translation>有任务正在执行 — 可在处理历史中查看进度与产物</translation>
    </message>
    <message>
        <source>Workspace is empty — import or open data to start</source>
        <translation>工作区为空 — 导入或打开数据开始工作</translation>
    </message>
    <message>
        <source>Workspace is empty — import data to start</source>
        <translation>工作区为空 — 导入数据开始</translation>
    </message>
    <message>
        <source>Workspace contains %1 layers</source>
        <translation>工作区包含 %1 个图层</translation>
    </message>
    <message>
        <source>Idle</source>
        <translation>空闲</translation>
    </message>
    <message>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <source>Remote-Sensing Products</source>
        <translation>遥感产品</translation>
    </message>
    <message>
        <source>SRC lacks a usable initial geotransform.Template matching predicts the search area from initial coordinates; first give the source image an approximate CRS / georeference,Or place a few rough GCPs manually first and use the existing-seed mode.</source>
        <translation>SRC 缺少可用的初始地理变换（GeoTransform）。模板匹配依赖初始坐标预测搜索区；请先为源影像指定近似 CRS/地理参考，或先手工打若干粗 GCP 后使用「现有种子点」模式。</translation>
    </message>
    <message>
        <source>REF lacks a usable geotransform; matched points cannot be converted to ground coordinates.</source>
        <translation>REF 缺少可用的地理变换，无法将匹配点转为地面坐标。</translation>
    </message>
    <message>
        <source>The SRC image is too small to generate grid seed points</source>
        <translation>SRC 影像过小，无法生成网格种子点</translation>
    </message>
    <message>
        <source>No matches met the threshold. Increase the search radius or lower the minimum correlation score,Or check that the SRC initial coordinates are roughly correct.</source>
        <translation>未找到满足阈值的匹配点。可增大搜索半径、降低最小相关分数，或检查 SRC 初始坐标是否大致正确。</translation>
    </message>
    <message>
        <source>empty raster or zero bands</source>
        <translation>空栅格或波段数为 0</translation>
    </message>
    <message>
        <source>The %1×%2 raster has no built-in pyramids and exceeds the preview pixel cap %3 (refused to keep the UI responsive)</source>
        <translation>栅格 %1×%2 无内建金字塔，超出预览像素上限 %3（已拒绝以保证界面响应）</translation>
    </message>
    <message>
        <source>too few pixels read (incomplete data)</source>
        <translation>读取的像素不足（数据不完整）</translation>
    </message>
    <message>
        <source>Raster read failed: %1</source>
        <translation>栅格读取失败：%1</translation>
    </message>
    <message>
        <source>Cannot open the vector data (unsupported driver or corrupt file)</source>
        <translation>矢量数据无法打开（驱动不支持或文件损坏）</translation>
    </message>
    <message>
        <source>The feature count %1 exceeds the preview cap %2 (refused to keep the UI responsive)</source>
        <translation>要素数 %1 超出预览上限 %2（已拒绝以保证界面响应）</translation>
    </message>
    <message>
        <source>layer has no valid extent (empty layer)</source>
        <translation>图层无有效范围（空图层）</translation>
    </message>
    <message>
        <source>four Chinese characters</source>
        <translation>四个汉字</translation>
    </message>
    <message>
        <source>Label</source>
        <translation>标签</translation>
    </message>
</context>
<context>
    <name>QaMaskDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the product raster layer to mask.</source>
        <translation>选择待提取掩膜的产品栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Quality band. Chosen automatically by product semantic role by default (SCL → scene classification, QA → quality).</source>
        <translation>质量波段。默认按产品语义角色自动选择（SCL → 场景分类，QA → 质量）。</translation>
    </message>
    <message>
        <source>Quality Band</source>
        <translation>质量波段</translation>
    </message>
    <message>
        <source>Mask Parameters</source>
        <translation>掩膜参数</translation>
    </message>
    <message>
        <source>Auto-detect</source>
        <translation>自动识别</translation>
    </message>
    <message>
        <source>Landsat QA_PIXEL Bit Flags</source>
        <translation>Landsat QA_PIXEL 位标志</translation>
    </message>
    <message>
        <source>Sentinel-2 SCL Classes</source>
        <translation>Sentinel-2 SCL 类别</translation>
    </message>
    <message>
        <source>Generic Bit Mask</source>
        <translation>通用位掩码</translation>
    </message>
    <message>
        <source>• Auto: identify by band role / name (SCL → Sentinel-2; QA → Landsat)
• Landsat QA_PIXEL: Collection 2 bit flags
• Sentinel-2 SCL: by scene classification classes
• Generic bitmask: decided bit by bit from the bits parameter</source>
        <translation>• 自动：按波段角色/名称识别（SCL → Sentinel-2；QA → Landsat）
• Landsat QA_PIXEL：按 Collection 2 位标志
• Sentinel-2 SCL：按场景分类类别
• 通用位掩码：按 bits 参数逐位判断</translation>
    </message>
    <message>
        <source>Choose the classes to turn into the mask.
• Landsat: cloud = bits 1/2/3 (dilated cloud / cirrus / cloud), shadow = bit 4, snow = bit 5, water = bit 7
• Sentinel-2 SCL: cloud = classes 8/9/10, shadow = 3, snow = 11, water = 6</source>
        <translation>选择要置为掩膜的类别。
• Landsat：云=bit1/2/3（膨胀云/卷云/云），云影=bit4，雪=bit5，水体=bit7
• Sentinel-2 SCL：云=类别 8/9/10，云影=3，雪=11，水体=6</translation>
    </message>
    <message>
        <source>Quality Source</source>
        <translation>质量源</translation>
    </message>
    <message>
        <source>Cloud + cloud shadow (recommended)</source>
        <translation>云 + 云影（推荐）</translation>
    </message>
    <message>
        <source>Cloud only (incl. thin cirrus)</source>
        <translation>仅云（含薄卷云）</translation>
    </message>
    <message>
        <source>Cloud shadow only</source>
        <translation>仅云影</translation>
    </message>
    <message>
        <source>Snow</source>
        <translation>雪</translation>
    </message>
    <message>
        <source>Water</source>
        <translation>水体</translation>
    </message>
    <message>
        <source>All invalid/occluded classes</source>
        <translation>全部无效/遮挡类别</translation>
    </message>
    <message>
        <source>Mask Classes</source>
        <translation>掩膜类别</translation>
    </message>
    <message>
        <source>Generic bit mask: pixels where (value &amp; bits) != 0 are masked.</source>
        <translation>通用位掩码：值为 (value &amp; bits) != 0 的像素被掩膜。</translation>
    </message>
    <message>
        <source>Bit Flags (generic)</source>
        <translation>位标志 (通用)</translation>
    </message>
    <message>
        <source>Mask Statistics</source>
        <translation>掩膜统计</translation>
    </message>
    <message>
        <source>After running, mask statistics appear here.</source>
        <translation>运行后在此显示掩膜统计结果。</translation>
    </message>
    <message>
        <source>Select a valid raster layer first.</source>
        <translation>请先选择一个有效的栅格图层。</translation>
    </message>
    <message>
        <source>Masked pixels: %1 / %2 (%3%)</source>
        <translation>掩膜像元：%1 / %2（%3%）</translation>
    </message>
    <message>
        <source>QA Mask</source>
        <translation>QA 掩膜</translation>
    </message>
</context>
<context>
    <name>QgisDesktopWindow</name>
    <message>
        <source>Untitled Project — SICNU GEO RS Remote Sensing Platform</source>
        <translation>未命名工程 — SICNU GEO RS 遥感分析平台</translation>
    </message>
    <message>
        <source>Plugins</source>
        <translation>插件</translation>
    </message>
    <message>
        <source>Plugin '%1' loaded</source>
        <translation>插件“%1”已加载</translation>
    </message>
    <message>
        <source>Plugin Manager...</source>
        <translation>插件管理器…</translation>
    </message>
    <message>
        <source>RS Studio Workbench for Remote-Sensing Image Processing and Analysis</source>
        <translation>RS Studio 遥感影像处理与分析工作台</translation>
    </message>
    <message>
        <source>High-performance rendering, band math, orthorectification and intelligent interpretation for multi-source satellite imagery (optical / hyperspectral / SAR / DEM).
Click the button below to import data, or press Ctrl+O to open an existing project.</source>
        <translation>支持多源遥感卫星影像（光学/高光谱/SAR/DEM）的高性能渲染、波段运算、正射校正与智能解译。
点击下方按钮导入数据，或按 Ctrl+O 打开已有工程。</translation>
    </message>
    <message>
        <source>Import Remote-Sensing Data...</source>
        <translation>导入遥感数据...</translation>
    </message>
    <message>
        <source>Scale %1</source>
        <translation>比例 %1</translation>
    </message>
    <message>
        <source>No Layers</source>
        <translation>无图层</translation>
    </message>
    <message>
        <source>Current Active Layer</source>
        <translation>当前活动图层</translation>
    </message>
    <message>
        <source>Cancelling %1 · Running %2 · Queued %3</source>
        <translation>取消中 %1 · 运行 %2 · 排队 %3</translation>
    </message>
    <message>
        <source>Running %1 · Queued %2</source>
        <translation>运行 %1 · 排队 %2</translation>
    </message>
    <message>
        <source>Processing (%1)...</source>
        <translation>处理中（%1）...</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Render: %1 ms</source>
        <translation>渲染：%1 ms</translation>
    </message>
    <message>
        <source>Project Data</source>
        <translation>工程数据</translation>
    </message>
    <message>
        <source>The QGIS project opened, but some SICNU data relationships could not be restored:
%1</source>
        <translation>QGIS 工程已打开，但部分 SICNU 数据关联未能恢复：
%1</translation>
    </message>
    <message>
        <source>Project data: %1 governance notice(s) — see logs</source>
        <translation>工程数据：%1 条治理提示——详见日志</translation>
    </message>
    <message>
        <source>Project loaded</source>
        <translation>工程已加载</translation>
    </message>
    <message>
        <source>The project could not include the SICNU data catalog:
%1</source>
        <translation>工程未能加载 SICNU 数据目录：
%1</translation>
    </message>
    <message>
        <source>Project saved</source>
        <translation>工程已保存</translation>
    </message>
    <message>
        <source>Display Stretch — %1</source>
        <translation>显示拉伸 — %1</translation>
    </message>
    <message>
        <source>View Layers</source>
        <translation>视图图层</translation>
    </message>
    <message>
        <source>No layers yet</source>
        <translation>暂无图层</translation>
    </message>
    <message>
        <source>Add or open remote-sensing rasters and vector data from the Data Management panel</source>
        <translation>从数据管理面板添加或直接打开遥感影像与矢量数据</translation>
    </message>
    <message>
        <source>Add Layer...</source>
        <translation>添加图层...</translation>
    </message>
    <message>
        <source>File Browser</source>
        <translation>文件浏览</translation>
    </message>
    <message>
        <source>Processing Toolbox</source>
        <translation>处理工具箱</translation>
    </message>
    <message>
        <source>Search algorithms...</source>
        <translation>搜索算法...</translation>
    </message>
    <message>
        <source>Python Console</source>
        <translation>Python 控制台</translation>
    </message>
    <message>
        <source>Python Script Editor</source>
        <translation>Python 脚本编辑器</translation>
    </message>
    <message>
        <source>Remove from Favorites</source>
        <translation>从收藏夹中移除</translation>
    </message>
    <message>
        <source>Add to Favorites</source>
        <translation>添加到收藏夹</translation>
    </message>
    <message>
        <source>Open Algorithm</source>
        <translation>打开算法</translation>
    </message>
    <message>
        <source>Overview Map</source>
        <translation>鹰眼视图</translation>
    </message>
    <message>
        <source>Identify Features</source>
        <translation>要素识别</translation>
    </message>
    <message>
        <source>Click features on the map with the Identify tool to see their details.</source>
        <translation>使用要素识别工具在地图上点击以查看要素详情。</translation>
    </message>
    <message>
        <source>Spectral Curve</source>
        <translation>光谱曲线</translation>
    </message>
    <message>
        <source>Display Stretch</source>
        <translation>显示拉伸</translation>
    </message>
    <message>
        <source>Guided Workflow</source>
        <translation>引导工作流</translation>
    </message>
    <message>
        <source>Initializing Python...</source>
        <translation>正在初始化 Python...</translation>
    </message>
    <message>
        <source>Python ready</source>
        <translation>Python 就绪</translation>
    </message>
    <message>
        <source>Initializing Python script editor...</source>
        <translation>正在初始化 Python 脚本编辑器...</translation>
    </message>
    <message>
        <source>Python script editor ready</source>
        <translation>Python 脚本编辑器就绪</translation>
    </message>
    <message>
        <source>Reset Layout</source>
        <translation>重置布局</translation>
    </message>
    <message>
        <source>Workspace Governance</source>
        <translation>工作区治理</translation>
    </message>
    <message>
        <source>Data Management</source>
        <translation>数据管理</translation>
    </message>
    <message>
        <source>Add to Display</source>
        <translation>添加到显示</translation>
    </message>
    <message>
        <source>Cannot add the data assets to the current view.</source>
        <translation>无法将数据资产添加到当前视图。</translation>
    </message>
    <message>
        <source>Unload this data asset from the project?</source>
        <translation>从工程卸载此数据资产？</translation>
    </message>
    <message>
        <source>This asset is referenced by %1 display / processing leases. Unloading removes the corresponding presentation.

Continue with the cascading unload?</source>
        <translation>该资产正被 %1 个显示/处理租约引用。卸载将移除对应呈现。

继续级联卸载？</translation>
    </message>
    <message>
        <source>Unload the selected %1 data assets from the project?
If display / processing references exist, their presentations are removed cascadingly.</source>
        <translation>从工程卸载选中的 %1 个数据资产？
若存在显示/处理引用，将级联移除对应呈现。</translation>
    </message>
    <message>
        <source>Unload Data Assets</source>
        <translation>卸载数据资产</translation>
    </message>
    <message>
        <source>This data asset cannot be unloaded.</source>
        <translation>无法卸载该数据资产。</translation>
    </message>
    <message>
        <source>Batch Unload</source>
        <translation>批量卸载</translation>
    </message>
    <message>
        <source>Finished: %1 succeeded, %2 failed.</source>
        <translation>完成：成功 %1，失败 %2。</translation>
    </message>
    <message>
        <source>Unloaded %1 data assets</source>
        <translation>已卸载 %1 个数据资产</translation>
    </message>
    <message>
        <source>Promote to Project Persistent</source>
        <translation>提升为工程持久</translation>
    </message>
    <message>
        <source>This temporary data asset cannot be promoted.</source>
        <translation>无法提升该临时数据资产。</translation>
    </message>
    <message>
        <source>Re-link Missing Source</source>
        <translation>重定位缺失源</translation>
    </message>
    <message>
        <source>This data asset cannot be found.</source>
        <translation>找不到该数据资产。</translation>
    </message>
    <message>
        <source>Re-link Missing Source — choose a new source file</source>
        <translation>重定位缺失源 — 选择新的源文件</translation>
    </message>
    <message>
        <source>All Supported Files (*.tif *.tiff *.vrt *.shp *.gpkg *.geojson);;</source>
        <translation>所有支持的文件 (*.tif *.tiff *.vrt *.shp *.gpkg *.geojson);;</translation>
    </message>
    <message>
        <source>Rasters (*.tif *.tiff *.vrt);;</source>
        <translation>栅格 (*.tif *.tiff *.vrt);;</translation>
    </message>
    <message>
        <source>Vectors (*.shp *.gpkg *.geojson);;</source>
        <translation>矢量 (*.shp *.gpkg *.geojson);;</translation>
    </message>
    <message>
        <source>All Files (*)</source>
        <translation>所有文件 (*)</translation>
    </message>
    <message>
        <source>This data asset cannot be re-linked.</source>
        <translation>无法重定位该数据资产。</translation>
    </message>
    <message>
        <source>Re-linked asset %1 → %2</source>
        <translation>已重定位资产 %1 → %2</translation>
    </message>
    <message>
        <source>Tasks</source>
        <translation>任务</translation>
    </message>
    <message>
        <source>No features found at this location.</source>
        <translation>该位置未找到要素。</translation>
    </message>
    <message>
        <source>Unknown Layer</source>
        <translation>未知图层</translation>
    </message>
    <message>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <source>Open Raster Layers (multi-select)</source>
        <translation>打开栅格图层（可多选）</translation>
    </message>
    <message>
        <source>Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;ENVI raster (*.dat *.hdr *.img *.bil *.bsq *.bip);;All files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;ENVI 栅格 (*.dat *.hdr *.img *.bil *.bsq *.bip);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Loaded %1 rasters</source>
        <translation>已加载 %1 个栅格</translation>
    </message>
    <message>
        <source>Raster loading: %1 succeeded, %2 failed</source>
        <translation>栅格加载：成功 %1，失败 %2</translation>
    </message>
    <message>
        <source>Open Vector Layers (multi-select)</source>
        <translation>打开矢量图层（可多选）</translation>
    </message>
    <message>
        <source>Vector Files (*.shp *.gpkg *.geojson *.kml *.gml);;All Files (*.*)</source>
        <translation>矢量文件 (*.shp *.gpkg *.geojson *.kml *.gml);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Loaded %1 vectors</source>
        <translation>已加载 %1 个矢量</translation>
    </message>
    <message>
        <source>Vector loading: %1 succeeded, %2 failed</source>
        <translation>矢量加载：成功 %1，失败 %2</translation>
    </message>
    <message>
        <source>Layer Properties</source>
        <translation>图层属性</translation>
    </message>
    <message>
        <source>No layer selected</source>
        <translation>未选中任何图层</translation>
    </message>
    <message>
        <source>Project CRS set to: %1</source>
        <translation>工程坐标系设置为: %1</translation>
    </message>
    <message>
        <source>&amp;Project</source>
        <translation>工程(&amp;P)</translation>
    </message>
    <message>
        <source>Create an empty project, clearing current layers and view state.</source>
        <translation>创建空白工程，清除当前图层与视图状态。</translation>
    </message>
    <message>
        <source>Opens a saved project file.</source>
        <translation>打开已保存的工程文件。</translation>
    </message>
    <message>
        <source>Saves the current project to its existing path.</source>
        <translation>保存当前工程到已有路径。</translation>
    </message>
    <message>
        <source>Saves the project as a new file.</source>
        <translation>将工程另存为新文件。</translation>
    </message>
    <message>
        <source>Import raster or vector layers into the project.</source>
        <translation>导入栅格或矢量图层到工程。</translation>
    </message>
    <message>
        <source>Import Product...</source>
        <translation>导入产品...</translation>
    </message>
    <message>
        <source>Import Landsat / Sentinel-2 / MODIS scenes as products: preview bands / grid groups and import as data collections.</source>
        <translation>按产品导入 Landsat / Sentinel-2 / MODIS 场景：预览波段/网格组并选择导入为数据集合。</translation>
    </message>
    <message>
        <source>Import Landsat Product...</source>
        <translation>导入 Landsat 产品...</translation>
    </message>
    <message>
        <source>Import a Landsat scene as a product (directory containing *_MTL.txt).</source>
        <translation>按产品导入 Landsat 场景（含 *_MTL.txt 的目录）。</translation>
    </message>
    <message>
        <source>Import Sentinel-2 Product...</source>
        <translation>导入 Sentinel-2 产品...</translation>
    </message>
    <message>
        <source>Import a Sentinel-2 SAFE product (a .SAFE directory containing MTD_MSI*.xml).</source>
        <translation>按产品导入 Sentinel-2 SAFE 产品（含 MTD_MSI*.xml 的 .SAFE 目录）。</translation>
    </message>
    <message>
        <source>Browses STAC catalogs to find remote-sensing data.</source>
        <translation>浏览 STAC 目录检索遥感数据。</translation>
    </message>
    <message>
        <source>Create a print layout / map product.</source>
        <translation>创建打印布局 / 出图。</translation>
    </message>
    <message>
        <source>Export Experiment Report...</source>
        <translation>导出实验报告...</translation>
    </message>
    <message>
        <source>Export course / lab reports.</source>
        <translation>导出课程/实验报告。</translation>
    </message>
    <message>
        <source>Quits the application.</source>
        <translation>退出应用程序。</translation>
    </message>
    <message>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <source>Toggles editing of the current vector layer.</source>
        <translation>开启/关闭当前矢量图层编辑。</translation>
    </message>
    <message>
        <source>Save Edits</source>
        <translation>保存编辑</translation>
    </message>
    <message>
        <source>Saves vector edits.</source>
        <translation>保存矢量编辑。</translation>
    </message>
    <message>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <source>Undoes the last edit.</source>
        <translation>撤销上一步编辑。</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
    <message>
        <source>Redoes the undone edit.</source>
        <translation>重做已撤销的编辑。</translation>
    </message>
    <message>
        <source>Cut Features</source>
        <translation>剪切要素</translation>
    </message>
    <message>
        <source>Cuts the selected features.</source>
        <translation>剪切选中要素。</translation>
    </message>
    <message>
        <source>Copy Features</source>
        <translation>复制要素</translation>
    </message>
    <message>
        <source>Copies the selected features.</source>
        <translation>复制选中要素。</translation>
    </message>
    <message>
        <source>Paste Features</source>
        <translation>粘贴要素</translation>
    </message>
    <message>
        <source>Pastes features.</source>
        <translation>粘贴要素。</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
    <message>
        <source>Selects all features of the current layer.</source>
        <translation>选择当前图层全部要素。</translation>
    </message>
    <message>
        <source>Select Features</source>
        <translation>选择要素</translation>
    </message>
    <message>
        <source>Selects features with a rectangle.</source>
        <translation>矩形选择要素。</translation>
    </message>
    <message>
        <source>Delete Selected</source>
        <translation>删除选中</translation>
    </message>
    <message>
        <source>Deletes the selected features.</source>
        <translation>删除选中要素。</translation>
    </message>
    <message>
        <source>Open Attribute Table...</source>
        <translation>打开属性表...</translation>
    </message>
    <message>
        <source>Opens the attribute table.</source>
        <translation>打开属性表。</translation>
    </message>
    <message>
        <source>Digitizing</source>
        <translation>数字化</translation>
    </message>
    <message>
        <source>Add Feature</source>
        <translation>添加要素</translation>
    </message>
    <message>
        <source>Digitize to add a new feature.</source>
        <translation>数字化添加新要素。</translation>
    </message>
    <message>
        <source>Node Tool</source>
        <translation>节点工具</translation>
    </message>
    <message>
        <source>Edit nodes.</source>
        <translation>编辑节点。</translation>
    </message>
    <message>
        <source>Move Features</source>
        <translation>移动要素</translation>
    </message>
    <message>
        <source>Moves the selected features.</source>
        <translation>移动选中要素。</translation>
    </message>
    <message>
        <source>Rotate Features</source>
        <translation>旋转要素</translation>
    </message>
    <message>
        <source>Rotates the selected features.</source>
        <translation>旋转选中要素。</translation>
    </message>
    <message>
        <source>Scale Features</source>
        <translation>缩放要素</translation>
    </message>
    <message>
        <source>Scales the selected features.</source>
        <translation>缩放选中要素。</translation>
    </message>
    <message>
        <source>Offset Line</source>
        <translation>偏移线</translation>
    </message>
    <message>
        <source>Offsets line features.</source>
        <translation>线要素偏移。</translation>
    </message>
    <message>
        <source>Reverse Line Direction</source>
        <translation>反转线方向</translation>
    </message>
    <message>
        <source>Reverses the direction of line features.</source>
        <translation>反转线要素方向。</translation>
    </message>
    <message>
        <source>Reshape Geometry</source>
        <translation>重塑几何</translation>
    </message>
    <message>
        <source>Reshapes feature geometry.</source>
        <translation>重塑要素几何。</translation>
    </message>
    <message>
        <source>Split Features</source>
        <translation>分割要素</translation>
    </message>
    <message>
        <source>Splits features.</source>
        <translation>分割要素。</translation>
    </message>
    <message>
        <source>Split Part</source>
        <translation>分割部件</translation>
    </message>
    <message>
        <source>Split multipart geometry.</source>
        <translation>分割多部件几何。</translation>
    </message>
    <message>
        <source>Simplify</source>
        <translation>简化</translation>
    </message>
    <message>
        <source>Simplifies geometry.</source>
        <translation>简化几何。</translation>
    </message>
    <message>
        <source>Add Ring</source>
        <translation>挖环</translation>
    </message>
    <message>
        <source>Add an interior ring.</source>
        <translation>添加内环。</translation>
    </message>
    <message>
        <source>Add Part</source>
        <translation>添加部件</translation>
    </message>
    <message>
        <source>Adds a part.</source>
        <translation>添加多部件。</translation>
    </message>
    <message>
        <source>Fill Ring</source>
        <translation>填充环</translation>
    </message>
    <message>
        <source>Filling the ring creates a new feature.</source>
        <translation>填充环生成新要素。</translation>
    </message>
    <message>
        <source>Delete Part</source>
        <translation>删部件</translation>
    </message>
    <message>
        <source>Deletes the part.</source>
        <translation>删除部件。</translation>
    </message>
    <message>
        <source>Delete Ring</source>
        <translation>删除环</translation>
    </message>
    <message>
        <source>Delete an interior ring.</source>
        <translation>删除内环。</translation>
    </message>
    <message>
        <source>Trim/Extend</source>
        <translation>修剪/延伸</translation>
    </message>
    <message>
        <source>Trim or extend features.</source>
        <translation>修剪或延伸要素。</translation>
    </message>
    <message>
        <source>Chamfer/Fillet</source>
        <translation>倒角/圆角</translation>
    </message>
    <message>
        <source>Chamfer or fillet.</source>
        <translation>倒角或圆角。</translation>
    </message>
    <message>
        <source>Feature Array</source>
        <translation>要素阵列</translation>
    </message>
    <message>
        <source>Duplicates features in an array.</source>
        <translation>按阵列复制要素。</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>视图(&amp;V)</translation>
    </message>
    <message>
        <source>Zooms the map view in.</source>
        <translation>放大地图视图。</translation>
    </message>
    <message>
        <source>Zooms the map view out.</source>
        <translation>缩小地图视图。</translation>
    </message>
    <message>
        <source>Zooms to the extent of all layers.</source>
        <translation>缩放到所有图层范围。</translation>
    </message>
    <message>
        <source>Zooms to the current layer's extent.</source>
        <translation>缩放到当前图层范围。</translation>
    </message>
    <message>
        <source>Pans the map.</source>
        <translation>平移地图。</translation>
    </message>
    <message>
        <source>Click the map to query feature / pixel attributes.</source>
        <translation>点击地图查询要素/像元属性。</translation>
    </message>
    <message>
        <source>Measures distance.</source>
        <translation>量测距离。</translation>
    </message>
    <message>
        <source>Measures area.</source>
        <translation>量测面积。</translation>
    </message>
    <message>
        <source>Compare two layers side by side.</source>
        <translation>左右并排对比两个图层。</translation>
    </message>
    <message>
        <source>Drag the divider on the map to compare the layers above and below.</source>
        <translation>在地图上拖动分割线对比上下图层。</translation>
    </message>
    <message>
        <source>Second View</source>
        <translation>第二视图</translation>
    </message>
    <message>
        <source>Opens/closes the second display view (independent layer stack and rendering; can be made the active view).</source>
        <translation>打开/关闭第二显示视图（独立图层栈与渲染，可设为活动视图）。</translation>
    </message>
    <message>
        <source>Activate Main View</source>
        <translation>激活主视图</translation>
    </message>
    <message>
        <source>Routes open / show operations to the main map.</source>
        <translation>打开/显示操作路由到主地图。</translation>
    </message>
    <message>
        <source>Activate Second View</source>
        <translation>激活第二视图</translation>
    </message>
    <message>
        <source>Routes open / show operations to the second view (must be open).</source>
        <translation>打开/显示操作路由到第二视图（需已打开）。</translation>
    </message>
    <message>
        <source>Sync main view layers to the second view</source>
        <translation>同步主视图图层到第二视图</translation>
    </message>
    <message>
        <source>Clones the main view display layers into the second view (independent renderer).</source>
        <translation>将主视图显示图层克隆到第二视图（独立渲染器）。</translation>
    </message>
    <message>
        <source>Linked Viewports</source>
        <translation>双视口联动</translation>
    </message>
    <message>
        <source>When enabled, both viewports pan/zoom in pixel-level sync (layers can still differ per viewport in swipe comparison).</source>
        <translation>启用后，两个视口像素级同步平移/缩放（卷帘对比时各视口仍可独立显示图层）。</translation>
    </message>
    <message>
        <source>Refreshes the map rendering.</source>
        <translation>刷新地图渲染。</translation>
    </message>
    <message>
        <source>&amp;Layer</source>
        <translation>图层(&amp;L)</translation>
    </message>
    <message>
        <source>Add Raster Layer...</source>
        <translation>添加栅格图层...</translation>
    </message>
    <message>
        <source>Add a raster layer from a file.</source>
        <translation>从文件添加栅格图层。</translation>
    </message>
    <message>
        <source>Add Vector Layer...</source>
        <translation>添加矢量图层...</translation>
    </message>
    <message>
        <source>Add a vector layer from a file.</source>
        <translation>从文件添加矢量图层。</translation>
    </message>
    <message>
        <source>New Shapefile Layer...</source>
        <translation>新建 Shapefile 图层...</translation>
    </message>
    <message>
        <source>Create a new Shapefile vector layer.</source>
        <translation>创建新的 Shapefile 矢量图层。</translation>
    </message>
    <message>
        <source>Opens the current layer properties.</source>
        <translation>打开当前图层属性。</translation>
    </message>
    <message>
        <source>Remove the current layer from the project.</source>
        <translation>从工程中移除当前图层。</translation>
    </message>
    <message>
        <source>Set Project CRS...</source>
        <translation>设置工程 CRS...</translation>
    </message>
    <message>
        <source>Sets the project CRS.</source>
        <translation>设置工程坐标系。</translation>
    </message>
    <message>
        <source>&amp;Raster</source>
        <translation>栅格(&amp;R)</translation>
    </message>
    <message>
        <source>Preprocessing</source>
        <translation>预处理</translation>
    </message>
    <message>
        <source>Image Registration</source>
        <translation>影像配准</translation>
    </message>
    <message>
        <source>Image to Image (I2I)...</source>
        <translation>影像对影像 (I2I)...</translation>
    </message>
    <message>
        <source>Two-canvas SRC|REF ground-point registration with SIFT support. No RPC.</source>
        <translation>双画布 SRC|REF 同名点配准，支持 SIFT。不含 RPC。</translation>
    </message>
    <message>
        <source>Image to Map (I2M)...</source>
        <translation>影像对地图 (I2M)...</translation>
    </message>
    <message>
        <source>Source image + main-project map picking; RPC Physical supported.</source>
        <translation>源影像 + 主工程地图取点；支持 RPC Physical。</translation>
    </message>
    <message>
        <source>Mosaic...</source>
        <translation>镶嵌...</translation>
    </message>
    <message>
        <source>Mosaic multiple rasters into a continuous image.</source>
        <translation>多景栅格镶嵌为连续影像。</translation>
    </message>
    <message>
        <source>Extract Band...</source>
        <translation>提取波段...</translation>
    </message>
    <message>
        <source>Extract and save a single band from a multiband raster.</source>
        <translation>从多波段栅格提取单一波段保存。</translation>
    </message>
    <message>
        <source>Band Composition...</source>
        <translation>波段合成...</translation>
    </message>
    <message>
        <source>Multiband composition / merge.</source>
        <translation>多波段合成/合并。</translation>
    </message>
    <message>
        <source>Image Enhancement</source>
        <translation>影像增强</translation>
    </message>
    <message>
        <source>Combined Enhancement Panel...</source>
        <translation>增强综合面板...</translation>
    </message>
    <message>
        <source>Combined panel: contrast stretch, spatial filtering, band ratio / IHS, SAR speckle filtering.</source>
        <translation>综合面板：对比度拉伸、空间滤波、波段比值/IHS、SAR 斑点滤波。</translation>
    </message>
    <message>
        <source>Contrast Stretch...</source>
        <translation>对比度拉伸...</translation>
    </message>
    <message>
        <source>Linear / percent clip / std dev / histogram equalization.</source>
        <translation>线性 / 百分比裁剪 / 标准差 / 直方图均衡。</translation>
    </message>
    <message>
        <source>Spatial Filtering...</source>
        <translation>空间滤波...</translation>
    </message>
    <message>
        <source>Mean / Gaussian / Median / Sobel / Laplacian.</source>
        <translation>均值 / 高斯 / 中值 / Sobel / Laplacian。</translation>
    </message>
    <message>
        <source>Speckle Filtering (SAR)...</source>
        <translation>斑点滤波 (SAR)...</translation>
    </message>
    <message>
        <source>SAR speckle filtering: Lee / Frost / Kuan / Gamma-MAP.</source>
        <translation>SAR 斑点滤波：Lee / Frost / Kuan / Gamma-MAP。</translation>
    </message>
    <message>
        <source>Bands and Transform</source>
        <translation>波段与变换</translation>
    </message>
    <message>
        <source>Band Math...</source>
        <translation>波段运算...</translation>
    </message>
    <message>
        <source>Expression math, e.g. (b1-b2)/(b1+b2).</source>
        <translation>表达式运算，如 (b1-b2)/(b1+b2)。</translation>
    </message>
    <message>
        <source>Band Ratio / IHS...</source>
        <translation>波段比值 / IHS...</translation>
    </message>
    <message>
        <source>Band ratio or IHS transform.</source>
        <translation>波段比值或 IHS 变换。</translation>
    </message>
    <message>
        <source>Principal Component Analysis (PCA)...</source>
        <translation>主成分分析 (PCA)...</translation>
    </message>
    <message>
        <source>PCA: dimensionality reduction and decorrelation.</source>
        <translation>主成分分析：降维与去相关。</translation>
    </message>
    <message>
        <source>&amp;Analysis</source>
        <translation>分析(&amp;A)</translation>
    </message>
    <message>
        <source>Time Series Analysis...</source>
        <translation>时间序列分析...</translation>
    </message>
    <message>
        <source>Multitemporal statistics / compositing / index time series / trends / anomalies / point and ROI series (with scientific prechecks).</source>
        <translation>多时相统计 / 合成 / 指数时序 / 趋势 / 异常 / 点与 ROI 序列（含科学预检）。</translation>
    </message>
    <message>
        <source>Classification</source>
        <translation>分类</translation>
    </message>
    <message>
        <source>Supervised Classification (pixel level)...</source>
        <translation>监督分类（像元级）...</translation>
    </message>
    <message>
        <source>Pixel-level supervised classification: ROIs, algorithms, accuracy assessment.</source>
        <translation>像元级监督分类：ROI、算法、精度评价。</translation>
    </message>
    <message>
        <source>Object-Based Classification (OBIA)...</source>
        <translation>对象级分类 (OBIA)...</translation>
    </message>
    <message>
        <source>Segmentation + object-level classification.</source>
        <translation>分割 + 对象级分类。</translation>
    </message>
    <message>
        <source>Object-Based Classification (OBIA) — not enabled</source>
        <translation>面向对象分类 (OBIA) — 未启用</translation>
    </message>
    <message>
        <source>Classification (OpenCV ml unavailable)</source>
        <translation>分类（OpenCV ml 不可用）</translation>
    </message>
    <message>
        <source>&amp;Remote Sensing</source>
        <translation>遥感(&amp;G)</translation>
    </message>
    <message>
        <source>Products and Preprocessing</source>
        <translation>产品与预处理</translation>
    </message>
    <message>
        <source>Import Remote-Sensing Product...</source>
        <translation>导入遥感产品...</translation>
    </message>
    <message>
        <source>Product-aware import for Sentinel-2 / Landsat / MODIS with band roles and metadata parsed automatically.</source>
        <translation>Sentinel-2 / Landsat / MODIS 产品识别导入，自动解析波段角色与元数据。</translation>
    </message>
    <message>
        <source>Radiometric Calibration...</source>
        <translation>辐射定标...</translation>
    </message>
    <message>
        <source>DN → radiance / TOA reflectance / brightness temperature (sensor metadata auto-detected).</source>
        <translation>DN→辐射亮度 / TOA 反射率 / 亮温（自动探测传感器元数据）。</translation>
    </message>
    <message>
        <source>QA Mask (cloud/shadow/snow)...</source>
        <translation>QA 掩膜（云/云影/雪）...</translation>
    </message>
    <message>
        <source>Generate cloud/shadow/snow masks from Landsat QA_PIXEL / Sentinel-2 SCL.</source>
        <translation>从 Landsat QA_PIXEL / Sentinel-2 SCL 生成云/云影/雪掩膜。</translation>
    </message>
    <message>
        <source>Apply Mask...</source>
        <translation>应用掩膜...</translation>
    </message>
    <message>
        <source>Applies the mask to the product: obscured pixels become NoData, yielding an analysis-ready image.</source>
        <translation>掩膜应用到产品：被遮挡像元置为 NoData，得到分析就绪影像。</translation>
    </message>
    <message>
        <source>Atmospheric Correction...</source>
        <translation>大气校正...</translation>
    </message>
    <message>
        <source>DOS1 / DOS2 / QUAC with parameters auto-filled from metadata.</source>
        <translation>DOS1 / DOS2 / QUAC，元数据自动填充参数。</translation>
    </message>
    <message>
        <source>Orthorectification (RPC/GCP)...</source>
        <translation>正射校正 (RPC/GCP)...</translation>
    </message>
    <message>
        <source>Terrain correction based on RPC/GCPs and an optional DEM.</source>
        <translation>基于 RPC/GCP 与可选 DEM 的地形纠正。</translation>
    </message>
    <message>
        <source>Analysis</source>
        <translation>分析</translation>
    </message>
    <message>
        <source>Spectral Indices...</source>
        <translation>光谱指数...</translation>
    </message>
    <message>
        <source>NDVI / EVI / SAVI / NDWI / NDBI / MNDWI (bands chosen automatically by semantic role).</source>
        <translation>NDVI / EVI / SAVI / NDWI / NDBI / MNDWI（按语义波段角色自动选带）。</translation>
    </message>
    <message>
        <source>Spectral Analysis</source>
        <translation>光谱分析</translation>
    </message>
    <message>
        <source>Spectral Library Matching...</source>
        <translation>光谱库匹配...</translation>
    </message>
    <message>
        <source>Match pixel/ROI spectra against the spectral library (SAM + SID).</source>
        <translation>像元/ROI 谱与光谱库匹配（SAM + SID）。</translation>
    </message>
    <message>
        <source>ROI Mean Spectrum...</source>
        <translation>ROI 均值谱...</translation>
    </message>
    <message>
        <source>Polygon ROI mean spectrum → spectral profile panel.</source>
        <translation>多边形 ROI 均值谱 → 光谱剖面面板。</translation>
    </message>
    <message>
        <source>Change Detection...</source>
        <translation>变化检测...</translation>
    </message>
    <message>
        <source>Differencing / normalized difference / change mask / post-classification comparison.</source>
        <translation>差值 / 归一化差值 / 变化掩膜 / 分类后比较。</translation>
    </message>
    <message>
        <source>Post-Classification Comparison...</source>
        <translation>分类后比较...</translation>
    </message>
    <message>
        <source>Two-date classification comparison: per-class transition matrix, gains/losses and a change-type map.</source>
        <translation>两期分类对比：逐类转移矩阵、增益/损失、变化类型图。</translation>
    </message>
    <message>
        <source>Image Fusion...</source>
        <translation>影像融合...</translation>
    </message>
    <message>
        <source>Pansharpening: Linear / Brovey / IHS / PCA or OTB/GDAL.</source>
        <translation>全色锐化：Linear / Brovey / IHS / PCA 或 OTB/GDAL。</translation>
    </message>
    <message>
        <source>Terrain Analysis...</source>
        <translation>地形分析...</translation>
    </message>
    <message>
        <source>DEM: slope / aspect / hillshade / roughness, etc.</source>
        <translation>DEM：坡度 / 坡向 / 山体阴影 / 粗糙度等。</translation>
    </message>
    <message>
        <source>Preprocessing Workflow (DAG)...</source>
        <translation>预处理工作流 (DAG)...</translation>
    </message>
    <message>
        <source>A reusable analysis-ready pipeline: calibration → QA mask → atmospheric correction → apply mask → NDVI.</source>
        <translation>可复用分析就绪流程：定标 → QA 掩膜 → 大气校正 → 应用掩膜 → NDVI。</translation>
    </message>
    <message>
        <source>&amp;Vector</source>
        <translation>矢量(&amp;T)</translation>
    </message>
    <message>
        <source>Geometry Processing</source>
        <translation>几何处理</translation>
    </message>
    <message>
        <source>Buffer...</source>
        <translation>缓冲区...</translation>
    </message>
    <message>
        <source>Vector buffer analysis.</source>
        <translation>矢量缓冲区分析。</translation>
    </message>
    <message>
        <source>Fusion...</source>
        <translation>融合...</translation>
    </message>
    <message>
        <source>Dissolves features by attribute.</source>
        <translation>按属性融合要素。</translation>
    </message>
    <message>
        <source>Merge...</source>
        <translation>合并...</translation>
    </message>
    <message>
        <source>Merge multiple vector layers.</source>
        <translation>合并多个矢量图层。</translation>
    </message>
    <message>
        <source>Clip...</source>
        <translation>裁剪...</translation>
    </message>
    <message>
        <source>Clips vectors by a boundary.</source>
        <translation>按边界裁剪矢量。</translation>
    </message>
    <message>
        <source>Overlay Analysis</source>
        <translation>叠加分析</translation>
    </message>
    <message>
        <source>Erase...</source>
        <translation>擦除...</translation>
    </message>
    <message>
        <source>Vector erase / difference.</source>
        <translation>矢量擦除 / 差集。</translation>
    </message>
    <message>
        <source>Intersect...</source>
        <translation>相交...</translation>
    </message>
    <message>
        <source>Vector intersection.</source>
        <translation>矢量相交。</translation>
    </message>
    <message>
        <source>Union...</source>
        <translation>联合...</translation>
    </message>
    <message>
        <source>Vector union.</source>
        <translation>矢量联合。</translation>
    </message>
    <message>
        <source>Spatial Selection</source>
        <translation>空间选择</translation>
    </message>
    <message>
        <source>Select by Location...</source>
        <translation>按位置选择...</translation>
    </message>
    <message>
        <source>Select features by spatial relation.</source>
        <translation>按空间关系选择要素。</translation>
    </message>
    <message>
        <source>Extract by Location...</source>
        <translation>按位置提取...</translation>
    </message>
    <message>
        <source>Extract features into a new layer by spatial relation.</source>
        <translation>按空间关系提取要素到新图层。</translation>
    </message>
    <message>
        <source>Properties and Projection</source>
        <translation>属性与投影</translation>
    </message>
    <message>
        <source>Reproject...</source>
        <translation>重投影...</translation>
    </message>
    <message>
        <source>Vector reprojection.</source>
        <translation>矢量重投影。</translation>
    </message>
    <message>
        <source>Field Calculator...</source>
        <translation>字段计算器...</translation>
    </message>
    <message>
        <source>Field calculator.</source>
        <translation>字段计算器。</translation>
    </message>
    <message>
        <source>Nearest Neighbour...</source>
        <translation>最近邻...</translation>
    </message>
    <message>
        <source>Nearest-neighbour analysis.</source>
        <translation>最近邻分析。</translation>
    </message>
    <message>
        <source>Distance Matrix...</source>
        <translation>距离矩阵...</translation>
    </message>
    <message>
        <source>Distance matrix.</source>
        <translation>距离矩阵。</translation>
    </message>
    <message>
        <source>&amp;Processing</source>
        <translation>处理(&amp;O)</translation>
    </message>
    <message>
        <source>Toolbox</source>
        <translation>工具箱</translation>
    </message>
    <message>
        <source>Opens the Processing Toolbox: GDAL / OTB / built-in algorithms.</source>
        <translation>打开处理工具箱：GDAL / OTB / 内置算法。</translation>
    </message>
    <message>
        <source>History</source>
        <translation>历史</translation>
    </message>
    <message>
        <source>View the history of processing algorithms that have run.</source>
        <translation>查看已运行处理算法的历史。</translation>
    </message>
    <message>
        <source>Batch Processing...</source>
        <translation>批量处理...</translation>
    </message>
    <message>
        <source>Run one algorithm over multiple input files in a batch.</source>
        <translation>同一算法批量处理多个输入文件。</translation>
    </message>
    <message>
        <source>&amp;Settings</source>
        <translation>设置(&amp;S)</translation>
    </message>
    <message>
        <source>Options...</source>
        <translation>选项...</translation>
    </message>
    <message>
        <source>Theme, default CRS, logging, GDAL/OTB paths.</source>
        <translation>主题、默认 CRS、日志、GDAL/OTB 路径。</translation>
    </message>
    <message>
        <source>CRS Presets...</source>
        <translation>CRS 预设...</translation>
    </message>
    <message>
        <source>Browse and choose a common CRS preset.</source>
        <translation>浏览并选择常用坐标系预设。</translation>
    </message>
    <message>
        <source>&amp;Window</source>
        <translation>窗口(&amp;W)</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <source>Help Center (F1)</source>
        <translation>帮助中心 (F1)</translation>
    </message>
    <message>
        <source>Opens the Help Center: search help topics, operator descriptions and error diagnostics.</source>
        <translation>打开帮助中心：搜索帮助主题、算子说明与错误诊断。</translation>
    </message>
    <message>
        <source>Help Content</source>
        <translation>帮助内容</translation>
    </message>
    <message>
        <source>Opens the help document.</source>
        <translation>打开帮助文档。</translation>
    </message>
    <message>
        <source>What's This?</source>
        <translation>这是什么？</translation>
    </message>
    <message>
        <source>Enter 'What's This?' mode and click any widget for its explanation.</source>
        <translation>进入「这是什么」模式，点击任意控件查看说明。</translation>
    </message>
    <message>
        <source>Load Sample Data</source>
        <translation>加载示例数据</translation>
    </message>
    <message>
        <source>Load the built-in sample datasets.</source>
        <translation>加载内置示例数据集。</translation>
    </message>
    <message>
        <source>Step-by-step guided experiment workflow.</source>
        <translation>分步引导式实验流程。</translation>
    </message>
    <message>
        <source>Check Version</source>
        <translation>检查版本</translation>
    </message>
    <message>
        <source>Shows current version information.</source>
        <translation>显示当前版本信息。</translation>
    </message>
    <message>
        <source>About</source>
        <translation>关于</translation>
    </message>
    <message>
        <source>About this software.</source>
        <translation>关于本软件。</translation>
    </message>
    <message>
        <source>Navigation and Display</source>
        <translation>导航与显示</translation>
    </message>
    <message>
        <source>Pan</source>
        <translation>平移</translation>
    </message>
    <message>
        <source>Pan (Space)</source>
        <translation>平移 (Space)</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Full Extent</source>
        <translation>全图</translation>
    </message>
    <message>
        <source>Full Extent (Ctrl+Shift+F)</source>
        <translation>全图 (Ctrl+Shift+F)</translation>
    </message>
    <message>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <source>Refresh Map</source>
        <translation>刷新地图</translation>
    </message>
    <message>
        <source>Identify</source>
        <translation>识别</translation>
    </message>
    <message>
        <source>Identify (Ctrl+Shift+I)</source>
        <translation>识别 (Ctrl+Shift+I)</translation>
    </message>
    <message>
        <source>Measure Distance</source>
        <translation>测距</translation>
    </message>
    <message>
        <source>Measure Distance (Ctrl+Shift+D)</source>
        <translation>测距 (Ctrl+Shift+D)</translation>
    </message>
    <message>
        <source>Measure Area</source>
        <translation>测面</translation>
    </message>
    <message>
        <source>Measure Area (Ctrl+Shift+A)</source>
        <translation>测面 (Ctrl+Shift+A)</translation>
    </message>
    <message>
        <source>Display contrast stretch (changes rendering only; no file is exported)</source>
        <translation>显示对比度拉伸（仅改渲染，不导出文件）</translation>
    </message>
    <message>
        <source>Open Current Layer Properties</source>
        <translation>打开当前图层属性</translation>
    </message>
    <message>
        <source>Digitizing Edit Tools</source>
        <translation>数字化编辑工具</translation>
    </message>
    <message>
        <source>Select</source>
        <translation>选择</translation>
    </message>
    <message>
        <source>Select Features (rectangle)</source>
        <translation>选择要素（矩形框选）</translation>
    </message>
    <message>
        <source>Draw New Feature</source>
        <translation>绘制新要素</translation>
    </message>
    <message>
        <source>Node</source>
        <translation>节点</translation>
    </message>
    <message>
        <source>Node tool: drag vertices to edit geometry</source>
        <translation>节点工具：拖动顶点编辑几何</translation>
    </message>
    <message>
        <source>Move</source>
        <translation>移动</translation>
    </message>
    <message>
        <source>Rotate</source>
        <translation>旋转</translation>
    </message>
    <message>
        <source>Reshape</source>
        <translation>整形</translation>
    </message>
    <message>
        <source>Reshape Geometry: modify feature boundaries</source>
        <translation>重塑几何：修改要素边界</translation>
    </message>
    <message>
        <source>Segmentation</source>
        <translation>分割</translation>
    </message>
    <message>
        <source>Offset</source>
        <translation>偏移</translation>
    </message>
    <message>
        <source>Offset Line (parallel line)</source>
        <translation>偏移线（平行线）</translation>
    </message>
    <message>
        <source>Simplify Geometry: thin out vertices</source>
        <translation>简化几何：抽稀顶点</translation>
    </message>
    <message>
        <source>Flip</source>
        <translation>反转</translation>
    </message>
    <message>
        <source>Add Ring (hole inside a polygon)</source>
        <translation>添加环（面内空洞）</translation>
    </message>
    <message>
        <source>Fill Ring (draw a new polygon inside a hole)</source>
        <translation>填充环（在空洞内绘新面）</translation>
    </message>
    <message>
        <source>Delete Selected Part</source>
        <translation>删除多部件之一</translation>
    </message>
    <message>
        <source>Scale —</source>
        <translation>比例 —</translation>
    </message>
    <message>
        <source>Opacity</source>
        <translation>不透明度</translation>
    </message>
    <message>
        <source>Current layer opacity</source>
        <translation>当前图层不透明度</translation>
    </message>
    <message>
        <source>Cache: 0 MB</source>
        <translation>缓存: 0 MB</translation>
    </message>
    <message>
        <source>Preferences saved (file logging takes effect on next start)</source>
        <translation>首选项已保存（文件日志将在下次启动时生效）</translation>
    </message>
    <message>
        <source>Preferences saved</source>
        <translation>首选项已保存</translation>
    </message>
    <message>
        <source>Processing History</source>
        <translation>处理历史</translation>
    </message>
    <message>
        <source>Processing history is unavailable.</source>
        <translation>处理历史记录不可用。</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Version Information</source>
        <translation>版本信息</translation>
    </message>
    <message>
        <source>SICNU GEO RS Remote-Sensing Image Interpretation Platform v0.9.2-dev</source>
        <translation>SICNU GEO RS 遥感图像解译平台 v0.9.2-dev</translation>
    </message>
    <message>
        <source>About RS Studio</source>
        <translation>关于 RS Studio</translation>
    </message>
    <message>
        <source>SICNU GEO RS Remote-Sensing Image Interpretation and Analysis Platform

A professional desktop for remote-sensing data processing and intelligent interpretation
Built on Qt 6 and a modern remote-sensing algorithm architecture

Version: v0.9.2-dev

Key features:
- Full multi-source raster and vector layer support
- High-performance multiband rendering with real-time color stretching
- Smart CRS and projection transformations
- A rich remote-sensing toolbox with asynchronous task scheduling</source>
        <translation>SICNU GEO RS 遥感图像解译与分析平台

专业级遥感数据处理与智能解译桌面端
基于 Qt 6 与现代遥感算法架构构建

版本：v0.9.2-dev

核心特性：
- 完整的多源栅格与矢量图层支持
- 高性能多波段渲染与实时色彩拉伸
- 坐标参考系统与投影智能转换
- 丰富的遥感处理工具箱与异步任务调度</translation>
    </message>
    <message>
        <source>Sample Data</source>
        <translation>示例数据</translation>
    </message>
    <message>
        <source>Sample data directory not found.
Expected data/samples/ at the project root.</source>
        <translation>未找到示例数据目录。
应位于工程根目录的 data/samples/。</translation>
    </message>
    <message>
        <source>Loaded %1 sample datasets (%2 failed)</source>
        <translation>已加载 %1 个示例数据集（失败 %2 个）</translation>
    </message>
    <message>
        <source>Loaded %1 sample datasets</source>
        <translation>已加载 %1 个示例数据集</translation>
    </message>
    <message>
        <source>Panels</source>
        <translation>面板</translation>
    </message>
    <message>
        <source>%1 Panel</source>
        <translation>%1 面板</translation>
    </message>
    <message>
        <source>(no panels)</source>
        <translation>（无面板）</translation>
    </message>
    <message>
        <source>Toolbar</source>
        <translation>工具栏</translation>
    </message>
    <message>
        <source>(no toolbars)</source>
        <translation>（无工具栏）</translation>
    </message>
    <message>
        <source>Tip: toolbars appear below the ribbon (up to two rows)</source>
        <translation>提示：工具栏显示在 Ribbon 下方（最多两行）</translation>
    </message>
    <message>
        <source>Layout reset to Ribbon mode (toolbars optional; the Task Center starts collapsed)</source>
        <translation>布局已重置为 Ribbon 模式（工具栏可选；任务中心默认收起）</translation>
    </message>
    <message>
        <source>Untitled Project</source>
        <translation>未命名工程</translation>
    </message>
    <message>
        <source>%1%2 — SICNU GEO RS Remote Sensing Platform</source>
        <translation>%1%2 — SICNU GEO RS 遥感分析平台</translation>
    </message>
    <message>
        <source>Exit Application</source>
        <translation>退出应用</translation>
    </message>
    <message>
        <source>Algorithm Not Found</source>
        <translation>未找到算法</translation>
    </message>
    <message>
        <source>Could not find processing algorithm: %1</source>
        <translation>未找到处理算法：%1</translation>
    </message>
    <message>
        <source>Please select a raster layer first.</source>
        <translation>请先选择一个栅格图层。</translation>
    </message>
    <message>
        <source>Band Math</source>
        <translation>波段运算</translation>
    </message>
    <message>
        <source>Spectral Index</source>
        <translation>光谱指数</translation>
    </message>
    <message>
        <source>Atmospheric Correction</source>
        <translation>大气校正</translation>
    </message>
    <message>
        <source>Radiometric Calibration</source>
        <translation>辐射定标</translation>
    </message>
    <message>
        <source>Select a raster layer first.</source>
        <translation>请先选择一个栅格图层。</translation>
    </message>
    <message>
        <source>Orthorectification</source>
        <translation>正射校正</translation>
    </message>
    <message>
        <source>QA Mask</source>
        <translation>QA 掩膜</translation>
    </message>
    <message>
        <source>Apply Mask</source>
        <translation>应用掩膜</translation>
    </message>
    <message>
        <source>Post-Classification Comparison</source>
        <translation>分类后比较</translation>
    </message>
    <message>
        <source>ROI Mean Spectrum</source>
        <translation>ROI 均值谱</translation>
    </message>
    <message>
        <source>Contrast Stretch</source>
        <translation>对比度拉伸</translation>
    </message>
    <message>
        <source>Select or load a raster layer first.
This feature only adjusts the map display contrast; it exports no new file.</source>
        <translation>请先选择或加载一个栅格图层。
此功能仅调整地图显示对比度，不导出新文件。</translation>
    </message>
    <message>
        <source>The display stretch panel has not been initialized.</source>
        <translation>显示拉伸面板尚未初始化。</translation>
    </message>
    <message>
        <source>Display stretch: modifies the layer renderer only; affects display and writes no new raster.</source>
        <translation>显示拉伸：修改图层渲染器，仅影响显示，不写出新栅格。</translation>
    </message>
    <message>
        <source>Spatial Filter</source>
        <translation>空间滤波</translation>
    </message>
    <message>
        <source>Speckle Filter</source>
        <translation>斑点滤波</translation>
    </message>
    <message>
        <source>PCA</source>
        <translation>PCA</translation>
    </message>
    <message>
        <source>Band Ratio / IHS</source>
        <translation>波段比值 / IHS</translation>
    </message>
    <message>
        <source>Terrain Analysis</source>
        <translation>地形分析</translation>
    </message>
    <message>
        <source>Image Fusion</source>
        <translation>影像融合</translation>
    </message>
    <message>
        <source>Mosaic</source>
        <translation>镶嵌</translation>
    </message>
    <message>
        <source>Set Layer CRS</source>
        <translation>设置图层 CRS</translation>
    </message>
    <message>
        <source>No layer selected.</source>
        <translation>未选中图层。</translation>
    </message>
    <message>
        <source>Layer CRS set to: %1 for "%2"</source>
        <translation>图层“%2”的 CRS 已设置为 %1</translation>
    </message>
    <message>
        <source>Swipe tool deactivated</source>
        <translation>卷帘工具已关闭</translation>
    </message>
    <message>
        <source>Swipe requires at least two raster layers</source>
        <translation>卷帘对比至少需要两个栅格图层</translation>
    </message>
    <message>
        <source>Swipe tool active — move mouse to compare '%1' vs '%2'</source>
        <translation>卷帘工具已激活——移动鼠标对比“%1”与“%2”</translation>
    </message>
    <message>
        <source>New Project</source>
        <translation>新建工程</translation>
    </message>
    <message>
        <source>The project Data Context is unavailable.</source>
        <translation>工程数据上下文不可用。</translation>
    </message>
    <message>
        <source>Failed to clear the project data context:
%1</source>
        <translation>清除工程数据上下文失败：
%1</translation>
    </message>
    <message>
        <source>New project created</source>
        <translation>已新建工程</translation>
    </message>
    <message>
        <source>Open Project</source>
        <translation>打开工程</translation>
    </message>
    <message>
        <source>QGIS Project Files (*.qgs *.qgz);;All Files (*)</source>
        <translation>QGIS 工程文件 (*.qgs *.qgz);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Failed to release the current project data:
%1</source>
        <translation>释放当前工程数据失败：
%1</translation>
    </message>
    <message>
        <source>Governance store unavailable: workspace state runs in memory-only mode</source>
        <translation>治理存储不可用：工作区状态将以仅内存模式运行</translation>
    </message>
    <message>
        <source>Failed to open project:
%1</source>
        <translation>打开工程失败：
%1</translation>
    </message>
    <message>
        <source>Opened project: %1</source>
        <translation>已打开工程：%1</translation>
    </message>
    <message>
        <source>Save Project</source>
        <translation>保存工程</translation>
    </message>
    <message>
        <source>QGIS Project Files (*.qgs);;All Files (*)</source>
        <translation>QGIS 工程文件 (*.qgs);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Project saved to: %1</source>
        <translation>工程已保存至：%1</translation>
    </message>
    <message>
        <source>All supported files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip *.shp *.gpkg *.geojson *.kml *.gml);;Raster files (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;ENVI raster (*.dat *.hdr *.img *.bil *.bsq *.bip);;Vector files (*.shp *.gpkg *.geojson *.kml *.gml);;All files (*)</source>
        <translation>所有支持的文件 (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip *.shp *.gpkg *.geojson *.kml *.gml);;栅格文件 (*.tif *.tiff *.img *.jp2 *.png *.jpg *.jpeg *.asc *.dat *.hdr *.bil *.bsq *.bip);;ENVI 栅格 (*.dat *.hdr *.img *.bil *.bsq *.bip);;矢量文件 (*.shp *.gpkg *.geojson *.kml *.gml);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Import Data (multi-select)</source>
        <translation>导入数据（可多选）</translation>
    </message>
    <message>
        <source>Imported %1 files</source>
        <translation>已导入 %1 个文件</translation>
    </message>
    <message>
        <source>Import finished: %1 succeeded, %2 failed</source>
        <translation>导入完成：成功 %1，失败 %2</translation>
    </message>
    <message>
        <source>Import Product</source>
        <translation>导入产品</translation>
    </message>
    <message>
        <source>The project data context is unavailable.</source>
        <translation>工程数据上下文不可用。</translation>
    </message>
    <message>
        <source>Export Lab Report</source>
        <translation>导出实验报告</translation>
    </message>
    <message>
        <source>No operations have been recorded yet.</source>
        <translation>尚未记录任何操作。</translation>
    </message>
    <message>
        <source>JSON files (*.json);;CSV files (*.csv);;All files (*.*)</source>
        <translation>JSON 文件 (*.json);;CSV 文件 (*.csv);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Failed to export report:
%1</source>
        <translation>导出报告失败：
%1</translation>
    </message>
    <message>
        <source>Lab report exported: %1</source>
        <translation>实验报告已导出：%1</translation>
    </message>
    <message>
        <source>Unsaved Changes</source>
        <translation>未保存的修改</translation>
    </message>
    <message>
        <source>Layer '%1' has unsaved edits. Save before proceeding?</source>
        <translation>图层“%1”有未保存的编辑，是否先保存再继续？</translation>
    </message>
    <message>
        <source>The project has been modified. Save changes?</source>
        <translation>工程已被修改，是否保存更改？</translation>
    </message>
    <message>
        <source>Nothing to undo</source>
        <translation>没有可撤销的操作</translation>
    </message>
    <message>
        <source>Nothing to redo</source>
        <translation>没有可重做的操作</translation>
    </message>
    <message>
        <source>Select an editable vector layer to cut</source>
        <translation>请选择可编辑的矢量图层进行剪切</translation>
    </message>
    <message>
        <source>Cut features</source>
        <translation>剪切要素</translation>
    </message>
    <message>
        <source>Copied %1 feature(s)</source>
        <translation>已复制 %1 个要素</translation>
    </message>
    <message>
        <source>Select an editable vector layer to paste</source>
        <translation>请选择可编辑的矢量图层进行粘贴</translation>
    </message>
    <message>
        <source>Paste features</source>
        <translation>粘贴要素</translation>
    </message>
    <message>
        <source>Pasted %1 feature(s)</source>
        <translation>已粘贴 %1 个要素</translation>
    </message>
    <message>
        <source>Selected %1 feature(s)</source>
        <translation>已选择 %1 个要素</translation>
    </message>
    <message>
        <source>Created new layer: %1</source>
        <translation>已创建新图层：%1</translation>
    </message>
    <message>
        <source>New Vector Layer</source>
        <translation>新建矢量图层</translation>
    </message>
    <message>
        <source>Failed to create layer: %1</source>
        <translation>创建图层失败：%1</translation>
    </message>
    <message>
        <source>Unsaved Edits</source>
        <translation>未保存的编辑</translation>
    </message>
    <message>
        <source>Layer '%1' has unsaved edits. Save changes before switching?</source>
        <translation>图层“%1”有未保存的编辑，切换前是否保存？</translation>
    </message>
    <message>
        <source>No vector layer selected</source>
        <translation>未选择矢量图层</translation>
    </message>
    <message>
        <source>Stop Editing</source>
        <translation>停止编辑</translation>
    </message>
    <message>
        <source>Do you want to save changes to %1?</source>
        <translation>是否保存对 %1 的更改？</translation>
    </message>
    <message>
        <source>%1 editing %2</source>
        <translation>%1 正在编辑 %2</translation>
    </message>
    <message>
        <source>Failed to save edits: %1</source>
        <translation>保存编辑失败：%1</translation>
    </message>
    <message>
        <source>Edits saved for %1</source>
        <translation>%1 的编辑已保存</translation>
    </message>
    <message>
        <source>Select an editable vector layer first</source>
        <translation>请先选择可编辑的矢量图层</translation>
    </message>
    <message>
        <source>No features selected</source>
        <translation>未选择要素</translation>
    </message>
    <message>
        <source>Deleted %1 feature(s)</source>
        <translation>已删除 %1 个要素</translation>
    </message>
    <message>
        <source>Start Editing</source>
        <translation>开始编辑</translation>
    </message>
    <message>
        <source>%1 is being edited in another view.
Commit or roll back that edit session before editing here.</source>
        <translation>%1 正在另一个视图中编辑。
请先提交或回滚该编辑会话，再在此处编辑。</translation>
    </message>
    <message>
        <source>Distance measure: click to add points; double-click or right-click to finish</source>
        <translation>距离测量：单击添加点，双击或右键结束测量</translation>
    </message>
    <message>
        <source>Area measure: click to add points; double-click or right-click to finish</source>
        <translation>面积测量：单击添加点，双击或右键结束测量</translation>
    </message>
    <message>
        <source>Image Registration · Image 2 Image</source>
        <translation>影像配准 · 影像对影像</translation>
    </message>
    <message>
        <source>Loaded the correction result into the main view: %1</source>
        <translation>已加载校正结果到主图：%1</translation>
    </message>
    <message>
        <source>Failed to load the correction result into the main view: %1</source>
        <translation>加载校正结果到主图失败：%1</translation>
    </message>
    <message>
        <source>Image Registration · Image 2 Map</source>
        <translation>影像配准 · 影像对地图</translation>
    </message>
    <message>
        <source>Loaded the classification result into the main view: %1</source>
        <translation>已加载分类结果到主图：%1</translation>
    </message>
    <message>
        <source>Failed to load the classification result into the main view: %1</source>
        <translation>加载分类结果到主图失败：%1</translation>
    </message>
    <message>
        <source>Classification session not registered as a display view (using session-local layer stack)</source>
        <translation>分类会话未注册为显示视图（使用会话本地图层栈）</translation>
    </message>
    <message>
        <source>Supervised classification requires OpenCV with the ml module.
Install opencv (including opencv-ml) and rebuild:
  cd build &amp;&amp; cmake .. &amp;&amp; make -j$(nproc)</source>
        <translation>监督分类需要带 ml 模块的 OpenCV。
请安装 opencv（含 opencv-ml）并重新编译：
  cd build &amp;&amp; cmake .. &amp;&amp; make -j$(nproc)</translation>
    </message>
    <message>
        <source>Loaded the OBIA classification result into the main view: %1</source>
        <translation>已加载 OBIA 分类结果到主图：%1</translation>
    </message>
    <message>
        <source>Failed to load the OBIA result into the main view: %1</source>
        <translation>加载 OBIA 结果到主图失败：%1</translation>
    </message>
    <message>
        <source>OBIA session not registered as a display view (using session-local layer stack)</source>
        <translation>OBIA 会话未注册为显示视图（使用会话本地图层栈）</translation>
    </message>
    <message>
        <source>OBIA</source>
        <translation>OBIA</translation>
    </message>
    <message>
        <source>Object-based classification requires OpenCV ml module.
Build with SICNU_HAS_OBIA=ON to enable this feature.</source>
        <translation>面向对象分类需要 OpenCV ml 模块。
请以 SICNU_HAS_OBIA=ON 重新编译以启用该功能。</translation>
    </message>
    <message>
        <source>Cannot create the second display view.</source>
        <translation>无法创建第二显示视图。</translation>
    </message>
    <message>
        <source>Second view open. Use 'Active' to switch the display target.</source>
        <translation>第二视图已打开。可用「活动」切换显示目标。</translation>
    </message>
    <message>
        <source>Second view closed</source>
        <translation>第二视图已关闭</translation>
    </message>
    <message>
        <source>Active view: main view</source>
        <translation>活动视图：主视图</translation>
    </message>
    <message>
        <source>Cannot activate the second view</source>
        <translation>无法激活第二视图</translation>
    </message>
    <message>
        <source>Active view: second view (open / show operations route here)</source>
        <translation>活动视图：第二视图（打开/显示将路由到此）</translation>
    </message>
    <message>
        <source>Open the second view first</source>
        <translation>请先打开第二视图</translation>
    </message>
    <message>
        <source>The main view has no display layers to sync</source>
        <translation>主视图没有可同步的显示图层</translation>
    </message>
    <message>
        <source>Cloned %1 main view layers into the second view</source>
        <translation>已将 %1 个主视图图层克隆到第二视图</translation>
    </message>
    <message>
        <source>Open the second view first to enable linked viewports</source>
        <translation>请先打开第二视图以启用双视口联动</translation>
    </message>
    <message>
        <source>Linked viewports enabled</source>
        <translation>双视口联动已启用</translation>
    </message>
    <message>
        <source>Linked viewports paused</source>
        <translation>双视口联动已暂停</translation>
    </message>
    <message>
        <source>Zoom to Layer</source>
        <translation>缩放到图层</translation>
    </message>
    <message>
        <source>Canvas refreshed</source>
        <translation>画布已刷新</translation>
    </message>
    <message>
        <source>Cannot load the artifacts to compare.</source>
        <translation>无法加载待对比的产物。</translation>
    </message>
    <message>
        <source>No layers to compare were found.</source>
        <translation>未找到待对比的图层。</translation>
    </message>
    <message>
        <source>Classification Workspace</source>
        <translation>分类工作区</translation>
    </message>
    <message>
        <source>Image-to-Image Registration</source>
        <translation>影像对影像配准</translation>
    </message>
    <message>
        <source>Image-to-Map Registration</source>
        <translation>影像对地图配准</translation>
    </message>
    <message>
        <source>Object-Level Classification</source>
        <translation>对象级分类</translation>
    </message>
    <message>
        <source>Layout Design</source>
        <translation>布局设计</translation>
    </message>
    <message>
        <source>Command Palette...</source>
        <translation>命令面板...</translation>
    </message>
    <message>
        <source>Search and run any command (keyboard-first).</source>
        <translation>搜索并执行任意命令（键盘优先）。</translation>
    </message>
    <message>
        <source>Tools</source>
        <translation>工具</translation>
    </message>
    <message>
        <source>Command</source>
        <translation>命令</translation>
    </message>
    <message>
        <source>Search</source>
        <translation>检索</translation>
    </message>
    <message>
        <source>Workspace</source>
        <translation>工作区</translation>
    </message>
    <message>
        <source>Inspector</source>
        <translation>检查器</translation>
    </message>
    <message>
        <source>Cannot open the artifacts on the map: %1</source>
        <translation>无法在地图上打开产物：%1</translation>
    </message>
    <message>
        <source>Failed to resume run %1: %2</source>
        <translation>恢复运行 %1 失败：%2</translation>
    </message>
    <message>
        <source>Unknown reason</source>
        <translation>未知原因</translation>
    </message>
    <message>
        <source>The following jobs still have unfinished tasks; %1 will interrupt them:
</source>
        <translation>以下工作仍有未完成的任务，%1 会中断它们：
</translation>
    </message>
    <message>
        <source>• Workspace '%1' has tasks running
</source>
        <translation>• 工作区「%1」正在运行任务
</translation>
    </message>
    <message>
        <source>• The Task Center still has %1 unfinished tasks (queued / waiting for resources)
</source>
        <translation>• 任务中心还有 %1 个未完成任务（含排队/等待资源）
</translation>
    </message>
    <message>
        <source>
Cancel these tasks and continue?</source>
        <translation>
是否取消这些任务并继续？</translation>
    </message>
    <message>
        <source>Cancel Task and Continue</source>
        <translation>取消任务并继续</translation>
    </message>
    <message>
        <source>Stay on Current Operation</source>
        <translation>留在当前操作</translation>
    </message>
</context>
<context>
    <name>QgsAngleMagnetWidget</name>
    <message>
        <source>°</source>
        <translation>°</translation>
    </message>
    <message>
        <source>Snap to </source>
        <translation>捕捉到 </translation>
    </message>
    <message>
        <source>No snapping</source>
        <translation>无捕捉</translation>
    </message>
</context>
<context>
    <name>QgsAttributeTableDialog</name>
    <message>
        <source>Actions</source>
        <translation>操作</translation>
    </message>
    <message>
        <source>Dock Attribute Table</source>
        <translation>停靠属性表</translation>
    </message>
    <message>
        <source>Multiedit is not supported when using custom UI forms</source>
        <translation>使用自定义 UI 表单时不支持多要素编辑</translation>
    </message>
    <message>
        <source>Search is not supported when using custom UI forms</source>
        <translation>使用自定义 UI 表单时不支持搜索</translation>
    </message>
    <message>
        <source> %1 — Features Total: %L2, Filtered: %L3, Selected: %L4</source>
        <translation> %1 — 要素总数：%L2，已过滤：%L3，已选择：%L4</translation>
    </message>
    <message>
        <source>Update All</source>
        <translation>更新全部</translation>
    </message>
    <message>
        <source>Update Filtered</source>
        <translation>更新过滤结果</translation>
    </message>
    <message>
        <source>Update Attributes</source>
        <translation>更新属性</translation>
    </message>
    <message>
        <source>An error occurred while trying to update the field %1</source>
        <translation>更新字段 %1 时发生错误</translation>
    </message>
    <message>
        <source>Calculating field</source>
        <translation>正在计算字段</translation>
    </message>
    <message>
        <source>An error occurred while evaluating the calculation string:
%1</source>
        <translation>计算表达式时发生错误：
%1</translation>
    </message>
    <message>
        <source>Security warning</source>
        <translation>安全警告</translation>
    </message>
    <message>
        <source>The action contains an embedded script which has been denied execution.</source>
        <translation>该动作包含内嵌脚本，已被拒绝执行。</translation>
    </message>
    <message>
        <source>Geometryless feature added</source>
        <translation>已添加无几何要素</translation>
    </message>
    <message>
        <source>Feature Added</source>
        <translation>已添加要素</translation>
    </message>
    <message>
        <source>Attribute added</source>
        <translation>已添加属性</translation>
    </message>
    <message>
        <source>Add Field</source>
        <translation>添加字段</translation>
    </message>
    <message>
        <source>Failed to add field '%1' of type '%2'. Is the field name unique?</source>
        <translation>添加类型为“%2”的字段“%1”失败，字段名是否唯一？</translation>
    </message>
    <message>
        <source>Deleted attribute</source>
        <translation>已删除属性</translation>
    </message>
    <message>
        <source>Attribute error</source>
        <translation>属性错误</translation>
    </message>
    <message>
        <source>The attribute(s) could not be deleted</source>
        <translation>无法删除所选属性</translation>
    </message>
    <message numerus="yes">
        <source>%n feature(s) on layer "%1", </source>
        <translation><numerusform>图层“%1”上有 %n 个要素，</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Delete at least %n feature(s) on other layer(s)</source>
        <translation><numerusform>请至少删除其他图层上的 %n 个要素</numerusform>
        </translation>
    </message>
    <message>
        <source>Delete of feature on layer "%1", %2 as well and all of its other descendants.
Delete these features?</source>
        <translation>将删除图层“%1”上的要素、%2 及其全部下游要素。
是否删除这些要素？</translation>
    </message>
    <message>
        <source>%1 on layer %2. </source>
        <translation>%1（图层 %2）。</translation>
    </message>
    <message numerus="yes">
        <source>%n feature(s) deleted: %1</source>
        <translation><numerusform>已删除 %n 个要素：%1</numerusform>
        </translation>
    </message>
    <message>
        <source>Delete Feature</source>
        <translation>删除要素</translation>
    </message>
</context>
<context>
    <name>QgsAvoidIntersectionsOperation</name>
    <message>
        <source>Avoid overlaps</source>
        <translation>避免重叠</translation>
    </message>
    <message>
        <source>Only the largest of multiple created geometries was preserved.</source>
        <translation>多个生成几何中仅保留了最大的一个。</translation>
    </message>
    <message>
        <source>Restore others</source>
        <translation>恢复其余几何</translation>
    </message>
    <message>
        <source>Restored geometry parts removed by avoid overlaps</source>
        <translation>已恢复因避免重叠而被移除的几何部分</translation>
    </message>
    <message>
        <source>At least one geometry intersected is invalid. These geometries must be manually repaired.</source>
        <translation>至少有一个相交几何无效，这些几何必须手动修复。</translation>
    </message>
</context>
<context>
    <name>QgsChamferFilletUserWidget</name>
    <message>
        <source>Chamfer</source>
        <translation>倒角</translation>
    </message>
    <message>
        <source>Fillet</source>
        <translation>圆角</translation>
    </message>
    <message>
        <source>Distance 1</source>
        <translation>距离 1</translation>
    </message>
    <message>
        <source>Distance 2</source>
        <translation>距离 2</translation>
    </message>
    <message>
        <source>Radius</source>
        <translation>半径</translation>
    </message>
    <message>
        <source>Fillet segments</source>
        <translation>圆角段数</translation>
    </message>
</context>
<context>
    <name>QgsClassificationMainWindow</name>
    <message>
        <source>Classification · Supervised</source>
        <translation>Classification · 监督分类</translation>
    </message>
    <message>
        <source>Forest</source>
        <translation>林地</translation>
    </message>
    <message>
        <source>Grassland</source>
        <translation>草地</translation>
    </message>
    <message>
        <source>Water</source>
        <translation>水体</translation>
    </message>
    <message>
        <source>Built-up Area</source>
        <translation>建成区</translation>
    </message>
    <message>
        <source>Cropland</source>
        <translation>耕地</translation>
    </message>
    <message>
        <source>Bare Soil</source>
        <translation>裸地</translation>
    </message>
    <message>
        <source>No ROIs to export</source>
        <translation>无 ROI 可导出</translation>
    </message>
    <message>
        <source>Export ROIs</source>
        <translation>导出 ROI</translation>
    </message>
    <message>
        <source>ESRI Shapefile (*.shp)</source>
        <translation>ESRI Shapefile (*.shp)</translation>
    </message>
    <message>
        <source>ROIs exported: %1</source>
        <translation>已导出 ROI: %1</translation>
    </message>
    <message>
        <source>ROI export failed: %1</source>
        <translation>ROI 导出失败: %1</translation>
    </message>
    <message>
        <source>Unsaved ROIs</source>
        <translation>未保存的 ROI</translation>
    </message>
    <message>
        <source>ROIs / classes have unsaved changes. Save them?</source>
        <translation>ROI / 类别有未保存的更改。是否保存？</translation>
    </message>
    <message>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Open source raster...</source>
        <translation>打开源栅格...</translation>
    </message>
    <message>
        <source>Load classifier model...</source>
        <translation>加载分类器模型...</translation>
    </message>
    <message>
        <source>Load ROIs...</source>
        <translation>加载 ROI...</translation>
    </message>
    <message>
        <source>Save ROIs...</source>
        <translation>保存 ROI...</translation>
    </message>
    <message>
        <source>Save classification project...</source>
        <translation>保存分类项目...</translation>
    </message>
    <message>
        <source>Load classification project...</source>
        <translation>加载分类项目...</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Edit</source>
        <translation>编辑</translation>
    </message>
    <message>
        <source>View</source>
        <translation>视图</translation>
    </message>
    <message>
        <source>&amp;Processing</source>
        <translation>处理(&amp;O)</translation>
    </message>
    <message>
        <source>Post-Classification</source>
        <translation>分类后处理</translation>
    </message>
    <message>
        <source>One dialog per algorithm; results load into this window's layer management by default</source>
        <translation>每个算法独立对话框；默认加载结果到本窗口图层管理</translation>
    </message>
    <message>
        <source>Quick Preview</source>
        <translation>快速预览</translation>
    </message>
    <message>
        <source>Train and Classify...</source>
        <translation>训练并分类…</translation>
    </message>
    <message>
        <source>Cross-Validation</source>
        <translation>交叉验证</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <source>Help Content</source>
        <translation>帮助内容</translation>
    </message>
    <message>
        <source>What's This? (Shift+F1)</source>
        <translation>这是什么？(Shift+F1)</translation>
    </message>
    <message>
        <source>About the Classification Window...</source>
        <translation>关于分类窗口…</translation>
    </message>
    <message>
        <source>About the Classification Window</source>
        <translation>关于分类窗口</translation>
    </message>
    <message>
        <source>Pixel-level supervised classification window

Define classes -&gt; collect ROIs -&gt; train -&gt; preview/apply -&gt; accuracy assessment.</source>
        <translation>像元级监督分类窗口

定义类别 -&gt; 采集 ROI -&gt; 训练 -&gt; 预览/应用 -&gt; 精度评价。</translation>
    </message>
    <message>
        <source>Sample Editing</source>
        <translation>样本编辑</translation>
    </message>
    <message>
        <source>Pan</source>
        <translation>平移</translation>
    </message>
    <message>
        <source>Pan / drag the canvas</source>
        <translation>漫游 / 拖动画布</translation>
    </message>
    <message>
        <source>Select Features</source>
        <translation>选择要素</translation>
    </message>
    <message>
        <source>Click to select sample features (Shift adds, Ctrl toggles)</source>
        <translation>点击选择样本要素（Shift 加选，Ctrl 切换）</translation>
    </message>
    <message>
        <source>Toggle Editing</source>
        <translation>切换编辑</translation>
    </message>
    <message>
        <source>Toggle editing of the sample vector layer (consistent with QGIS)</source>
        <translation>开启/关闭样本矢量层编辑（与 QGIS 一致）</translation>
    </message>
    <message>
        <source>Add Polygon</source>
        <translation>添加多边形</translation>
    </message>
    <message>
        <source>Delete Selected</source>
        <translation>删除选中</translation>
    </message>
    <message>
        <source>Delete the selected sample features</source>
        <translation>删除选中的样本要素</translation>
    </message>
    <message>
        <source>Magic Wand</source>
        <translation>魔棒</translation>
    </message>
    <message>
        <source>Optional: write the tolerance-grown result into the sample vector layer (non-standard QGIS editing)</source>
        <translation>可选：容差生长后写入样本矢量层（非标准 QGIS 编辑）</translation>
    </message>
    <message>
        <source>Training Samples</source>
        <translation>训练样本</translation>
    </message>
    <message>
        <source>Validation Samples</source>
        <translation>验证样本</translation>
    </message>
    <message>
        <source>Spectra</source>
        <translation>光谱</translation>
    </message>
    <message>
        <source>Separability</source>
        <translation>可分性</translation>
    </message>
    <message>
        <source>Quick preview</source>
        <translation>快速预览</translation>
    </message>
    <message>
        <source>Apply classification...</source>
        <translation>应用分类...</translation>
    </message>
    <message>
        <source>Layer</source>
        <translation>图层</translation>
    </message>
    <message>
        <source>Class Management</source>
        <translation>类别管理</translation>
    </message>
    <message>
        <source>Merge Selected Classes...</source>
        <translation>合并所选类别…</translation>
    </message>
    <message>
        <source>Class Quick List</source>
        <translation>类别快览</translation>
    </message>
    <message>
        <source>Jeffries–Matusita Distance</source>
        <translation>JM 分离度</translation>
    </message>
    <message>
        <source>Spectral Curve</source>
        <translation>光谱曲线</translation>
    </message>
    <message>
        <source>CRS: —</source>
        <translation>CRS：—</translation>
    </message>
    <message>
        <source>Total ROIs: 0, pixels: 0</source>
        <translation>总 ROI: 0, 像元: 0</translation>
    </message>
    <message>
        <source>The magic-wand selection touched the 513×513 search window boundary and was truncated; use the rectangle ROI tool for larger regions</source>
        <translation>魔棒选区触及 513×513 搜索窗口边界，已被截断；更大的区域请改用矩形 ROI 工具</translation>
    </message>
    <message>
        <source>No classification result rasters to merge</source>
        <translation>没有可合并的分类结果栅格</translation>
    </message>
    <message>
        <source>Merge Classes</source>
        <translation>合并类别</translation>
    </message>
    <message>
        <source>Sample layer editing enabled</source>
        <translation>样本层编辑已开启</translation>
    </message>
    <message>
        <source>Sample layer editing closed (committed)</source>
        <translation>样本层编辑已关闭（已提交）</translation>
    </message>
    <message>
        <source>No sample features selected</source>
        <translation>未选中样本要素</translation>
    </message>
    <message>
        <source>Delete Sample</source>
        <translation>删除样本</translation>
    </message>
    <message>
        <source>Select a class in the class table first</source>
        <translation>请先在类别表中选一个类别</translation>
    </message>
    <message>
        <source>Add Training Samples</source>
        <translation>添加训练样本</translation>
    </message>
    <message>
        <source>Failed to add sample</source>
        <translation>添加样本失败</translation>
    </message>
    <message>
        <source>Added sample → %1 (%2 pixels)</source>
        <translation>已添加样本 → %1（%2 像元）</translation>
    </message>
    <message>
        <source>Total samples: %1, pixels: %2</source>
        <translation>总样本: %1, 像元: %2</translation>
    </message>
    <message>
        <source>CRS: %1</source>
        <translation>CRS：%1</translation>
    </message>
    <message>
        <source>Classifier</source>
        <translation>分类器</translation>
    </message>
    <message>
        <source>Workflow</source>
        <translation>工作流</translation>
    </message>
    <message>
        <source>Workflow Steps</source>
        <translation>工作流步骤</translation>
    </message>
    <message>
        <source>Classification Flowchart</source>
        <translation>分类流程图</translation>
    </message>
    <message>
        <source>Open Source Image...</source>
        <translation>打开源影像…</translation>
    </message>
    <message>
        <source>Select the raster image to classify as source data.</source>
        <translation>选择待分类的栅格影像作为源数据。</translation>
    </message>
    <message>
        <source>Add Default 6 Classes</source>
        <translation>添加默认 6 类</translation>
    </message>
    <message>
        <source>Quickly adds the 6 default classes (water / vegetation / built-up / bare soil / road / shadow).</source>
        <translation>快速添加 6 个默认类别（水/植被/建筑/裸土/道路/阴影）。</translation>
    </message>
    <message>
        <source>Open Class Management</source>
        <translation>打开类别管理</translation>
    </message>
    <message>
        <source>Opens the class table panel to edit class names and colors.</source>
        <translation>打开类别表面板，编辑类别名称与颜色。</translation>
    </message>
    <message>
        <source>Edit names and colors in class management; at least 2 classes are needed to continue.</source>
        <translation>在类别管理中编辑名称与颜色；至少 2 个类别后可进入下一步。</translation>
    </message>
    <message>
        <source>Switch to the training role: newly collected ROIs train the classifier.</source>
        <translation>切换到训练样本角色：新采集的 ROI 用于训练分类器。</translation>
    </message>
    <message>
        <source>Switch to the validation role: newly collected ROIs are used for accuracy validation.</source>
        <translation>切换到验证样本角色：新采集的 ROI 用于精度验证。</translation>
    </message>
    <message>
        <source>Export ROIs...</source>
        <translation>导出 ROI…</translation>
    </message>
    <message>
        <source>Exports the current ROI samples as a Shapefile.</source>
        <translation>把当前 ROI 样本导出为 Shapefile。</translation>
    </message>
    <message>
        <source>Load ROI...</source>
        <translation>加载 ROI…</translation>
    </message>
    <message>
        <source>Load ROIs from a Shapefile (replaces current samples).</source>
        <translation>从 Shapefile 加载 ROI（将替换当前样本）。</translation>
    </message>
    <message>
        <source>Recompute Spectral Curves</source>
        <translation>重算光谱曲线</translation>
    </message>
    <message>
        <source>Recomputes per-class mean spectral curves from the current samples.</source>
        <translation>根据当前样本重算各类别光谱均值曲线。</translation>
    </message>
    <message>
        <source>Recompute JM Separability</source>
        <translation>重算 JM 分离度</translation>
    </message>
    <message>
        <source>Computes the Jeffries–Matusita separability matrix between class pairs.</source>
        <translation>计算各类别间的 Jeffries-Matusita 可分性矩阵。</translation>
    </message>
    <message>
        <source>Open the Spectral Curve Panel</source>
        <translation>打开光谱曲线面板</translation>
    </message>
    <message>
        <source>Shows and raises the spectral curve panel.</source>
        <translation>显示并置顶光谱曲线面板。</translation>
    </message>
    <message>
        <source>Open the JM Panel</source>
        <translation>打开 JM 面板</translation>
    </message>
    <message>
        <source>Shows and raises the JM separability matrix panel.</source>
        <translation>显示并置顶 JM 分离度矩阵面板。</translation>
    </message>
    <message>
        <source>Mark as Reviewed</source>
        <translation>标记已审阅</translation>
    </message>
    <message>
        <source>Confirm separability has been checked and mark this step done.</source>
        <translation>确认已检查可分性，标记本步完成。</translation>
    </message>
    <message>
        <source>After checking JM and spectral separability, press 'Mark as Reviewed' to finish this step.</source>
        <translation>检查 JM 与光谱可分性后点「标记已审阅」以完成本步。</translation>
    </message>
    <message>
        <source>Classifier type, bands and training ratio are set in the Classifier toolbar at the bottom.</source>
        <translation>分类器类型、波段与训练比例在底部 Classifier 工具栏设置。</translation>
    </message>
    <message>
        <source>Cross-Validation (CV)</source>
        <translation>交叉验证 (CV)</translation>
    </message>
    <message>
        <source>Runs K-fold cross-validation on the current samples to assess generalization accuracy.</source>
        <translation>对当前样本做 K 折交叉验证，评估泛化精度。</translation>
    </message>
    <message>
        <source>Classify preview for the current viewport only (no file is written).</source>
        <translation>仅对当前视口做分类预览（不写出文件）。</translation>
    </message>
    <message>
        <source>Apply Classification...</source>
        <translation>应用分类…</translation>
    </message>
    <message>
        <source>Applies the classification to the whole image and writes the result raster.</source>
        <translation>对整幅影像应用分类并输出结果栅格。</translation>
    </message>
    <message>
        <source>The preview covers the current viewport only and does not count as step completion; accuracy assessment follows the full-image Apply.</source>
        <translation>预览仅当前视口，不计入本步完成；全图 Apply 完成后进入精度评定。</translation>
    </message>
    <message>
        <source>Pop Out Full Window</source>
        <translation>弹出完整窗口</translation>
    </message>
    <message>
        <source>Accuracy comes from the holdout / validation split of the full-image Apply; export as CSV or open the enlarged view.</source>
        <translation>精度来自全图 Apply 的 holdout/验证划分；可导出 CSV 或弹出大图查看。</translation>
    </message>
    <message>
        <source>Skip Post-Processing</source>
        <translation>跳过后处理</translation>
    </message>
    <message>
        <source>Post-processing skipped</source>
        <translation>已跳过后处理</translation>
    </message>
    <message>
        <source>Export Selected Artifacts</source>
        <translation>导出所选产物</translation>
    </message>
    <message>
        <source>Classified GeoTIFF</source>
        <translation>分类 GeoTIFF</translation>
    </message>
    <message>
        <source>Post-Processing Raster</source>
        <translation>后处理栅格</translation>
    </message>
    <message>
        <source>Post-Processing Vector</source>
        <translation>后处理矢量</translation>
    </message>
    <message>
        <source>ROI</source>
        <translation>ROI</translation>
    </message>
    <message>
        <source>Accuracy CSV</source>
        <translation>精度 CSV</translation>
    </message>
    <message>
        <source>Classification Project .rscproj</source>
        <translation>分类项目 .rscproj</translation>
    </message>
    <message>
        <source>Export Selected</source>
        <translation>导出所选</translation>
    </message>
    <message>
        <source>Load Classification Result into Main Window</source>
        <translation>加载分类结果到主窗口</translation>
    </message>
    <message>
        <source>Same as main window vector editing: toggle editing → add polygon → double-click to finish;Selectable for deletion; samples show on the vector layer.</source>
        <translation>与主窗口矢量编辑一致：切换编辑 → 添加多边形 → 双击结束；选择后可删除；样本显示在矢量图层上。</translation>
    </message>
    <message>
        <source>Digitize polygon samples: left-click adds points; right-click / double-click finishes.The cls_id attribute takes the current class automatically.</source>
        <translation>数字化多边形样本：左键加点，右键/双击结束。属性 cls_id 自动取当前类别。</translation>
    </message>
    <message>
        <source>The digitizing tools (point / rectangle / polygon / freehand / magic wand) are in the toolbar above;Select a class in the class quick list before digitizing.</source>
        <translation>数字化工具（点/矩形/多边形/自由绘/魔棒）在上方工具栏；先在类别快览中选中类别再勾绘。</translation>
    </message>
    <message>
        <source>Post-processing uses one dialog per algorithm. Use the buttons below or the menu 'Processing → Post-Classification'.
By default results load into this window's layer management on the left; you can also skip this step and go to output.</source>
        <translation>后处理为「一算法一对话框」。请用下方按钮或菜单「处理 → 分类后处理」。
默认会将结果加载到本窗口左侧图层管理。也可跳过本步进入输出。</translation>
    </message>
    <message>
        <source>Tick artifacts and press 'Export Selected'; classification / post-processing rasters can be loaded into the main window layer tree.Any successful export or load completes this step.</source>
        <translation>勾选产物后点「导出所选」；可将分类/后处理栅格加载到主窗口图层树。任一成功导出或加载即完成本步。</translation>
    </message>
    <message>
        <source>The class scheme already exists and was not overwritten</source>
        <translation>类别方案已存在，未覆盖</translation>
    </message>
    <message>
        <source>Added the 6 default classes</source>
        <translation>已添加默认 6 类</translation>
    </message>
    <message>
        <source>Current role: training samples (digitizing tools write to the training set)</source>
        <translation>当前角色：训练样本（数字化工具写入训练集）</translation>
    </message>
    <message>
        <source>Current role: validation samples (UI marker; ROIs still share the collection)</source>
        <translation>当前角色：验证样本（UI 标记；ROI 仍共享集合）</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Main Operation Available</source>
        <translation>可进行主操作</translation>
    </message>
    <message>
        <source>Still needed: %1</source>
        <translation>还需：%1</translation>
    </message>
    <message>
        <source>No source image open</source>
        <translation>未打开源影像</translation>
    </message>
    <message>
        <source>Source image: %1</source>
        <translation>源影像：%1</translation>
    </message>
    <message>
        <source>%1
Number of classes: %2</source>
        <translation>%1
类别数：%2</translation>
    </message>
    <message>
        <source>Train</source>
        <translation>训练</translation>
    </message>
    <message>
        <source>Validation</source>
        <translation>验证</translation>
    </message>
    <message>
        <source>Current role: %1
ROIs: %2 · pixels: %3 · classes with pixels: %4</source>
        <translation>当前角色：%1
ROI 数：%2 · 像元：%3 · 有像元类别：%4</translation>
    </message>
    <message>
        <source>No sample collection</source>
        <translation>无样本集合</translation>
    </message>
    <message>
        <source>Classification task running...</source>
        <translation>分类任务运行中…</translation>
    </message>
    <message>
        <source>Soft gate: %1 more needed</source>
        <translation>软门禁：还需 %1</translation>
    </message>
    <message>
        <source>Open source raster</source>
        <translation>打开源栅格</translation>
    </message>
    <message>
        <source>Raster (*.tif *.tiff *.img *.jp2);;All files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.jp2);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Failed to open raster: %1</source>
        <translation>打开栅格失败：%1</translation>
    </message>
    <message>
        <source>Loaded source image: %1 (%2×%3, %4 bands)</source>
        <translation>已加载源影像：%1（%2×%3，%4 个波段）</translation>
    </message>
    <message>
        <source>Cancelling classification...</source>
        <translation>正在取消分类…</translation>
    </message>
    <message>
        <source>First use File → Open source raster...</source>
        <translation>请先 File → Open source raster...</translation>
    </message>
    <message>
        <source>No bands available</source>
        <translation>无可用波段</translation>
    </message>
    <message>
        <source>Output classified raster</source>
        <translation>输出分类栅格</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif)</source>
        <translation>GeoTIFF (*.tif)</translation>
    </message>
    <message>
        <source>Use Loaded Model (skip training)</source>
        <translation>使用已加载模型 (跳过训练)</translation>
    </message>
    <message>
        <source>Not enough training samples (&lt; 10 pixels) — draw ROIs or load saved samples first</source>
        <translation>训练样本不足（&lt; 10 像元）— 请先勾画 ROI 或加载已保存样本</translation>
    </message>
    <message>
        <source>Feature normalization failed</source>
        <translation>特征标准化失败</translation>
    </message>
    <message>
        <source>Save classifier model (optional)</source>
        <translation>保存分类器模型（可选）</translation>
    </message>
    <message>
        <source>OpenCV YAML (*.yml *.yaml);;All files (*)</source>
        <translation>OpenCV YAML (*.yml *.yaml);;所有文件 (*)</translation>
    </message>
    <message>
        <source> (classification)</source>
        <translation> (分类)</translation>
    </message>
    <message>
        <source>Classification finished: %1 (%2 ms)</source>
        <translation>分类完成: %1 (%2 ms)</translation>
    </message>
    <message>
        <source>Classification cancelled</source>
        <translation>分类已取消</translation>
    </message>
    <message>
        <source>Classification failed: %1</source>
        <translation>分类失败: %1</translation>
    </message>
    <message>
        <source>Classifying...</source>
        <translation>分类中…</translation>
    </message>
    <message>
        <source>The viewport is outside the image extent</source>
        <translation>视口不在影像范围内</translation>
    </message>
    <message>
        <source>Preview finished (%1 ms)</source>
        <translation>预览完成 (%1 ms)</translation>
    </message>
    <message>
        <source>Preview cancelled</source>
        <translation>预览已取消</translation>
    </message>
    <message>
        <source>Preview failed: %1</source>
        <translation>预览失败: %1</translation>
    </message>
    <message>
        <source>Previewing...</source>
        <translation>预览中…</translation>
    </message>
    <message>
        <source>A task is currently running; please wait</source>
        <translation>当前有任务进行中，请稍候</translation>
    </message>
    <message>
        <source> (post-processing)</source>
        <translation> (后处理)</translation>
    </message>
    <message>
        <source> (vector)</source>
        <translation> (矢量)</translation>
    </message>
    <message>
        <source>Post-processing finished (%1 ms)</source>
        <translation>后处理完成 (%1 ms)</translation>
    </message>
    <message>
        <source>; vectors %1</source>
        <translation>；矢量 %1</translation>
    </message>
    <message>
        <source>(loaded into layers)</source>
        <translation>（已加载到图层）</translation>
    </message>
    <message>
        <source>Post-processing cancelled</source>
        <translation>后处理已取消</translation>
    </message>
    <message>
        <source>Post-processing failed: %1</source>
        <translation>后处理失败: %1</translation>
    </message>
    <message>
        <source>Post-processing...</source>
        <translation>后处理中…</translation>
    </message>
    <message>
        <source>Open the source raster first</source>
        <translation>请先打开源栅格…</translation>
    </message>
    <message>
        <source>CV requires ≥ 25 pixels</source>
        <translation>CV 需要 ≥ 25 像元</translation>
    </message>
    <message>
        <source>K-Means CV</source>
        <translation>K 均值交叉验证</translation>
    </message>
    <message>
        <source>K-means cross-validation is not applicable (cluster ↔ class labels do not align).
Use Normal Bayes or SVM.</source>
        <translation>K-Means 交叉验证不适用 (cluster ↔ class 标签不齐)。
请用 NormalBayes 或 SVM。</translation>
    </message>
    <message>
        <source>5-fold Cross-Validation</source>
        <translation>5-fold 交叉验证</translation>
    </message>
    <message>
        <source>Fold %1: %2%</source>
        <translation>第 %1 折：%2%</translation>
    </message>
    <message>
        <source>Mean accuracy: %1% ± %2%</source>
        <translation>平均精度：%1% ± %2%</translation>
    </message>
    <message>
        <source>5-fold Cross Validation</source>
        <translation>5 折交叉验证</translation>
    </message>
    <message>
        <source>Cross-validation finished</source>
        <translation>交叉验证完成</translation>
    </message>
    <message>
        <source>Cross-validation cancelled</source>
        <translation>交叉验证已取消</translation>
    </message>
    <message>
        <source>Cross-validation failed: %1</source>
        <translation>交叉验证失败: %1</translation>
    </message>
    <message>
        <source>5-fold CV running...</source>
        <translation>5-fold CV 运行中…</translation>
    </message>
    <message>
        <source>Load ROIs</source>
        <translation>加载 ROI</translation>
    </message>
    <message>
        <source>Load ROI</source>
        <translation>加载 ROI</translation>
    </message>
    <message>
        <source>Loading new ROIs will replace the current %1 training samples. Continue?</source>
        <translation>加载新 ROI 将替换当前 %1 个训练样本，是否继续？</translation>
    </message>
    <message>
        <source>Loaded %1 samples successfully</source>
        <translation>成功加载 %1 个样本</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Failed to load ROIs from %1</source>
        <translation>从 %1 加载 ROI 失败</translation>
    </message>
    <message>
        <source>Load failed</source>
        <translation>加载失败</translation>
    </message>
    <message>
        <source>Cannot load the model: %1</source>
        <translation>无法加载模型：%1</translation>
    </message>
    <message>
        <source>The model sidecar file is corrupt or incompatible: %1
Loading refused; retry with a matching model and meta.json.</source>
        <translation>模型侧车文件损坏或不兼容：%1
已拒绝加载，请用匹配的模型与 meta.json 重试。</translation>
    </message>
    <message>
        <source>Model loading failed: meta.json is corrupt</source>
        <translation>模型加载失败：meta.json 损坏</translation>
    </message>
    <message>
        <source>Model loaded (no meta.json; features will not be scaled) — the next Apply skips training</source>
        <translation>已加载模型（无 meta.json，将不缩放特征）— 下次 Apply 将跳过训练</translation>
    </message>
    <message>
        <source>Model loaded — the next Apply skips training and predicts directly</source>
        <translation>已加载模型 — 下次 Apply 将跳过训练，直接 predict</translation>
    </message>
    <message>
        <source>Source file does not exist: %1</source>
        <translation>源文件不存在: %1</translation>
    </message>
    <message>
        <source>All files (*)</source>
        <translation>所有文件 (*)</translation>
    </message>
    <message>
        <source>Copy failed: %1</source>
        <translation>复制失败: %1</translation>
    </message>
    <message>
        <source>Exported: %1</source>
        <translation>已导出: %1</translation>
    </message>
    <message>
        <source>Export Classified GeoTIFF</source>
        <translation>导出分类 GeoTIFF</translation>
    </message>
    <message>
        <source>Export Post-Processing Raster</source>
        <translation>导出后处理栅格</translation>
    </message>
    <message>
        <source>Export Post-Processing Vector</source>
        <translation>导出后处理矢量</translation>
    </message>
    <message>
        <source>No accuracy results to export</source>
        <translation>无精度结果可导出</translation>
    </message>
    <message>
        <source>Tick at least one item to export</source>
        <translation>请至少勾选一项导出内容</translation>
    </message>
    <message>
        <source>Export Selected Finished</source>
        <translation>导出所选完成</translation>
    </message>
    <message>
        <source>No classification / post-processing raster paths available</source>
        <translation>无可用的分类/后处理栅格路径</translation>
    </message>
    <message>
        <source>Requested to load the result into the main view: %1</source>
        <translation>已请求将结果加载到主图: %1</translation>
    </message>
    <message>
        <source>Save Classification Project</source>
        <translation>保存分类项目</translation>
    </message>
    <message>
        <source>Classification project (*.rscproj);;All files (*)</source>
        <translation>分类项目 (*.rscproj);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Saving project failed: %1</source>
        <translation>保存项目失败: %1</translation>
    </message>
    <message>
        <source>Project saved: %1</source>
        <translation>已保存项目: %1</translation>
    </message>
    <message>
        <source>Load Classification Project</source>
        <translation>加载分类项目</translation>
    </message>
    <message>
        <source>Cannot load project: %1</source>
        <translation>无法加载项目: %1</translation>
    </message>
    <message>
        <source>Project loaded: %1</source>
        <translation>已加载项目: %1</translation>
    </message>
</context>
<context>
    <name>QgsClipboard</name>
    <message>
        <source>Paste features</source>
        <translation>粘贴要素</translation>
    </message>
    <message>
        <source>No features in clipboard.</source>
        <translation>剪贴板中没有要素。</translation>
    </message>
    <message>
        <source>Multiple geometry types found, features with geometry different from %1 will be created without geometry.</source>
        <translation>检测到多种几何类型，几何类型与 %1 不同的要素将不以几何方式创建。</translation>
    </message>
    <message>
        <source>Cannot create new layer.</source>
        <translation>无法创建新图层。</translation>
    </message>
    <message>
        <source>Cannot create field %1 (%2,%3), falling back to string type</source>
        <translation>无法创建字段 %1（%2,%3），回退为字符串类型</translation>
    </message>
    <message>
        <source>Cannot create field %1 (%2,%3)</source>
        <translation>无法创建字段 %1（%2,%3）</translation>
    </message>
</context>
<context>
    <name>QgsFeatureAction</name>
    <message>
        <source>Run Actions</source>
        <translation>运行动作</translation>
    </message>
</context>
<context>
    <name>QgsFeatureArrayUserWidget</name>
    <message>
        <source>Feature Count</source>
        <translation>要素数量</translation>
    </message>
    <message>
        <source>Spacing</source>
        <translation>间距</translation>
    </message>
    <message>
        <source>Spacing and Feature Count</source>
        <translation>间距与要素数量</translation>
    </message>
</context>
<context>
    <name>QgsFixAttributeDialog</name>
    <message>
        <source>%1 - Fix Pasted Features</source>
        <translation>%1 - 修复粘贴的要素</translation>
    </message>
    <message>
        <source>Discard All</source>
        <translation>全部放弃</translation>
    </message>
    <message>
        <source>Discard All Invalid</source>
        <translation>放弃全部无效项</translation>
    </message>
    <message>
        <source>Paste All (Including Invalid)</source>
        <translation>全部粘贴（含无效项）</translation>
    </message>
    <message>
        <source>Skip</source>
        <translation>跳过</translation>
    </message>
    <message>
        <source>Paste Anyway</source>
        <translation>仍然粘贴</translation>
    </message>
    <message>
        <source>%1 of %2 features processed (%3 fixed, %4 skipped)</source>
        <translation>已处理 %2 个要素中的 %1 个（修复 %3，跳过 %4）</translation>
    </message>
</context>
<context>
    <name>QgsGCPListModel</name>
    <message>
        <source>m</source>
        <translation>m</translation>
    </message>
    <message>
        <source>px</source>
        <translation>px</translation>
    </message>
    <message>
        <source>Map coordinates in the source image layer CRS</source>
        <translation>源影像图层坐标系下的地图坐标</translation>
    </message>
    <message>
        <source>Pixel column/row in the source image (col=column, row=row, from the top-left)</source>
        <translation>源影像像元行列号（列=col，行=row，自左上角）</translation>
    </message>
    <message>
        <source>Coordinates in the reference image / map layer CRS (Base)</source>
        <translation>参考影像/地图图层坐标系下的坐标（Base）</translation>
    </message>
    <message>
        <source>Pixel column/row in the reference image (col=column, row=row)</source>
        <translation>参考影像像元行列号（列=col，行=row）</translation>
    </message>
    <message>
        <source>Residuals (map units)</source>
        <translation>残差（地图单位）</translation>
    </message>
    <message>
        <source>Residuals (source image pixels)</source>
        <translation>残差（源影像像元）</translation>
    </message>
    <message>
        <source>Enable</source>
        <translation>启用</translation>
    </message>
    <message>
        <source>#</source>
        <translation>#</translation>
    </message>
    <message>
        <source>X src (map)</source>
        <translation>X源(map)</translation>
    </message>
    <message>
        <source>Y src (map)</source>
        <translation>Y源(map)</translation>
    </message>
    <message>
        <source>Col src</source>
        <translation>列源</translation>
    </message>
    <message>
        <source>Row src</source>
        <translation>行源</translation>
    </message>
    <message>
        <source>X ref (map)</source>
        <translation>X参(map)</translation>
    </message>
    <message>
        <source>Y ref (map)</source>
        <translation>Y参(map)</translation>
    </message>
    <message>
        <source>Col ref</source>
        <translation>列参</translation>
    </message>
    <message>
        <source>Row ref</source>
        <translation>行参</translation>
    </message>
    <message>
        <source>ΔX(%1)</source>
        <translation>ΔX(%1)</translation>
    </message>
    <message>
        <source>ΔY(%1)</source>
        <translation>ΔY(%1)</translation>
    </message>
    <message>
        <source>RMS(%1)</source>
        <translation>RMS(%1)</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
</context>
<context>
    <name>QgsGCPListWidget</name>
    <message>
        <source>Locate Source Point</source>
        <translation>定位到源点</translation>
    </message>
    <message>
        <source>Pan the source image canvas to this GCP's source position</source>
        <translation>将源影像画布平移到该 GCP 的源位置</translation>
    </message>
    <message>
        <source>Locate Target Point</source>
        <translation>定位到目标点</translation>
    </message>
    <message>
        <source>Pan the reference / map canvas to this GCP's target position</source>
        <translation>将参考/地图画布平移到该 GCP 的目标位置</translation>
    </message>
    <message>
        <source>Locate on Both Images</source>
        <translation>两侧定位</translation>
    </message>
    <message>
        <source>Locate this point on both the source and target canvases</source>
        <translation>同时在源与目标画布上定位该点</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation>禁用</translation>
    </message>
    <message>
        <source>Enable</source>
        <translation>启用</translation>
    </message>
    <message>
        <source>Edit Source Coordinates...</source>
        <translation>编辑源坐标…</translation>
    </message>
    <message>
        <source>Edit Target Coordinates...</source>
        <translation>编辑目标坐标…</translation>
    </message>
    <message>
        <source>Delete the %1 selected points</source>
        <translation>删除选中的 %1 个点</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
</context>
<context>
    <name>QgsGeorefImageToMapWindow</name>
    <message>
        <source>Image Registration · Image 2 Map</source>
        <translation>影像配准 · 影像对地图</translation>
    </message>
    <message>
        <source>Source image canvas: loads the image to correct.
Add GCP: after clicking an image point, a dialog pops up to enter map coordinates, or pick them from the main window map.</source>
        <translation>源影像画布：加载待校正影像。
Add GCP：在影像上点击像点后，弹出对话框填写地图坐标，或从主窗口地图取点。</translation>
    </message>
    <message>
        <source>Source Image (Warp)</source>
        <translation>源影像 (Warp)</translation>
    </message>
    <message>
        <source>Load .points...</source>
        <translation>加载 .points...</translation>
    </message>
    <message>
        <source>Import a saved control point file.</source>
        <translation>导入已保存的控制点文件。</translation>
    </message>
    <message>
        <source>Save .points...</source>
        <translation>保存 .points...</translation>
    </message>
    <message>
        <source>Exports the current control points.</source>
        <translation>导出当前控制点。</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Closes this window.</source>
        <translation>关闭本窗口。</translation>
    </message>
    <message>
        <source>Tools</source>
        <translation>工具</translation>
    </message>
    <message>
        <source>Image to Map: pick points on the source image; enter map coordinates manually or pick them from the main window map (no base map panel).</source>
        <translation>Image 2 Map：在源影像上取点，地图坐标手填或从主窗口地图拾取（无底图面板）。</translation>
    </message>
    <message>
        <source>&lt;b&gt;Image Registration · Image 2 Map&lt;/b&gt;&lt;br&gt;Aligned with the QGIS Georeferencer: only the source image to correct is shown; no base map is embedded in this window.&lt;br&gt;&lt;br&gt;&lt;b&gt;Typical Workflow&lt;/b&gt;&lt;br&gt;1. Load a georeferenced base map / vector in the main window&lt;br&gt;2. Open the source image in this window (file or main project layer)&lt;br&gt;3. Press Add GCP and click an image point on the source image&lt;br&gt;4. In the 'Enter Map Coordinates' dialog: type X/Y, or press 'Pick Point from Map' and click on the main window map&lt;br&gt;5. You can also edit the target X/Y columns directly in the GCP table&lt;br&gt;6. Optionally RPC / polynomial → run the correction&lt;br&gt;&lt;br&gt;No SIFT; no embedded base image panel.</source>
        <translation>&lt;b&gt;影像配准 · 影像对地图&lt;/b&gt;&lt;br&gt;对齐 QGIS Georeferencer：仅显示待校正源影像，不在本窗口嵌入底图。&lt;br&gt;&lt;br&gt;&lt;b&gt;典型流程&lt;/b&gt;&lt;br&gt;1. 主窗口加载已有地理参考的底图/矢量&lt;br&gt;2. 本窗口打开源影像（文件或主工程图层）&lt;br&gt;3. 点 Add GCP，在源影像上点击像点&lt;br&gt;4. 在「输入地图坐标」对话框中：手填 X/Y，或点「从地图取点」在主窗口地图上点选&lt;br&gt;5. 也可在 GCP 表中直接编辑目标 X/Y 列&lt;br&gt;6. 可选 RPC / 多项式 → 运行校正&lt;br&gt;&lt;br&gt;无 SIFT；无内嵌 Base 影像面板。</translation>
    </message>
</context>
<context>
    <name>QgsGeorefShellWindow</name>
    <message>
        <source>GCP Table</source>
        <translation>GCP 控制点表</translation>
    </message>
    <message>
        <source>Right-click: locate / enable-disable / edit / delete. Delete removes the row.</source>
        <translation>右键：定位 / 启用禁用 / 编辑 / 删除。Delete 删行。</translation>
    </message>
    <message>
        <source>Correction Task</source>
        <translation>校正任务</translation>
    </message>
    <message>
        <source>Correction Task List</source>
        <translation>校正任务列表</translation>
    </message>
    <message>
        <source>Output: %1</source>
        <translation>已输出: %1</translation>
    </message>
    <message>
        <source>Correction Parameters</source>
        <translation>校正参数</translation>
    </message>
    <message>
        <source>Correction Parameters Panel</source>
        <translation>校正参数面板</translation>
    </message>
    <message>
        <source>Correction Flowchart</source>
        <translation>校正流程图</translation>
    </message>
    <message>
        <source>Task #%1 finished: %2 — double-click to load into the main project</source>
        <translation>任务 #%1 完成: %2 — 双击可加载到主工程</translation>
    </message>
    <message>
        <source>Task #%1 cancelled</source>
        <translation>任务 #%1 已取消</translation>
    </message>
    <message>
        <source>Task #%1 failed: %2</source>
        <translation>任务 #%1 失败: %2</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Tips and coordinate information.</source>
        <translation>提示与坐标信息。</translation>
    </message>
    <message>
        <source>CRS: —</source>
        <translation>CRS：—</translation>
    </message>
    <message>
        <source>Summary of the relevant coordinate systems.</source>
        <translation>当前相关坐标系摘要。</translation>
    </message>
    <message>
        <source>RMS: —</source>
        <translation>RMS：—</translation>
    </message>
    <message>
        <source>Total RMS of the current fit. Shows — when points are insufficient or not fitted.</source>
        <translation>当前拟合总 RMS。点数不足或未拟合时显示 —。</translation>
    </message>
    <message>
        <source>Ready — open a source image, pick GCPs, set the output and press 'Run'</source>
        <translation>准备就绪 — 打开源影像并选取 GCP，设置输出后点「运行」</translation>
    </message>
    <message>
        <source>Layer / file name of the current canvas. Hover to see the full path.</source>
        <translation>当前画布对应的图层/文件名。悬停可查看完整路径。</translation>
    </message>
    <message>
        <source>Source (Warp): —</source>
        <translation>源 (Warp): —</translation>
    </message>
    <message>
        <source>No source image (to be corrected / Warp) open yet. File → Open source raster...</source>
        <translation>尚未打开源影像（待纠正 / Warp）。请通过 文件 → 打开源栅格… 打开。</translation>
    </message>
    <message>
        <source>Source (Warp): %1</source>
        <translation>源 (Warp): %1</translation>
    </message>
    <message>
        <source>Source image (to be corrected / Warp)
Layer: %1
Path: %2</source>
        <translation>源影像（待纠正 / Warp）
图层: %1
路径: %2</translation>
    </message>
    <message>
        <source>Base: —</source>
        <translation>基准 (Base): —</translation>
    </message>
    <message>
        <source>No base (reference image or map layer) has been set yet.</source>
        <translation>尚未指定基准（参考影像或地图图层）。</translation>
    </message>
    <message>
        <source>Base: %1</source>
        <translation>基准 (Base): %1</translation>
    </message>
    <message>
        <source>Base / reference layer: %1</source>
        <translation>基准图层 / 参考: %1</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>文件(&amp;F)</translation>
    </message>
    <message>
        <source>Open Source Image from File...</source>
        <translation>从文件打开源影像…</translation>
    </message>
    <message>
        <source>Opens the source image to correct (SRC / Warp) from a file. Shown on the source canvas; the path is used to write the warp.</source>
        <translation>从文件打开待校正源影像（SRC / Warp）。显示在源画布，路径用于写出 warp。</translation>
    </message>
    <message>
        <source>Open Source Image from Project Layer...</source>
        <translation>从工程图层打开源影像…</translation>
    </message>
    <message>
        <source>Chooses a raster from the main project layer list as the source image (Warp); no file picker needed.</source>
        <translation>从主工程图层列表选择栅格作为源影像（Warp），无需再选文件。</translation>
    </message>
    <message>
        <source>&amp;Settings</source>
        <translation>设置(&amp;S)</translation>
    </message>
    <message>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <source>About This Window...</source>
        <translation>关于本窗口…</translation>
    </message>
    <message>
        <source>Geometric Correction Help</source>
        <translation>几何校正帮助</translation>
    </message>
    <message>
        <source>Shows this window's workflow and panel explanations.</source>
        <translation>显示本窗口工作流程与各面板说明。</translation>
    </message>
    <message>
        <source>What's This? (Shift+F1)</source>
        <translation>这是什么？(Shift+F1)</translation>
    </message>
    <message>
        <source>Enter 'What's This?' mode, then click a widget for its explanation.</source>
        <translation>进入「这是什么」模式，再点击控件查看说明。</translation>
    </message>
    <message>
        <source>Pan</source>
        <translation>平移</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Fit Source</source>
        <translation>适合源</translation>
    </message>
    <message>
        <source>Fit Reference</source>
        <translation>适合参考</translation>
    </message>
    <message>
        <source>Fit Map</source>
        <translation>适合地图</translation>
    </message>
    <message>
        <source>Fit Both</source>
        <translation>适合两侧</translation>
    </message>
    <message>
        <source>Previous Extent</source>
        <translation>上一范围</translation>
    </message>
    <message>
        <source>Next Extent</source>
        <translation>下一范围</translation>
    </message>
    <message>
        <source>Registration toolbar: navigation, add / move / delete GCPs, import/export control points, run correction.</source>
        <translation>配准工具栏：导航、加点 / 移动 / 删除 GCP，导入导出控制点，运行校正。</translation>
    </message>
    <message>
        <source>Add Control Point</source>
        <translation>添加控制点</translation>
    </message>
    <message>
        <source>Move Control Point</source>
        <translation>移动控制点</translation>
    </message>
    <message>
        <source>Delete Control Point</source>
        <translation>删除控制点</translation>
    </message>
    <message>
        <source>Load Control Points</source>
        <translation>加载控制点</translation>
    </message>
    <message>
        <source>Load control points from a .points / .gcp file.</source>
        <translation>从 .points / .gcp 文件加载控制点。</translation>
    </message>
    <message>
        <source>Export Control Points</source>
        <translation>导出控制点</translation>
    </message>
    <message>
        <source>Exports control points as a .points file.</source>
        <translation>导出控制点为 .points 文件。</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>&lt;b&gt;Image Registration / Geometric Correction&lt;/b&gt;&lt;br&gt;&lt;br&gt;1. Open the source image (File)&lt;br&gt;2. Collect GCPs on the SRC and target canvases&lt;br&gt;3. Set the transform method, target CRS and output path on the right&lt;br&gt;4. Check residuals; once point counts and the method are satisfied, press 'Run' on the toolbar&lt;br&gt;5. Track progress under 'Correction Task'; double-click a finished task to load its result&lt;br&gt;&lt;br&gt;Tip: hover over tool buttons or parameter widgets for detailed explanations.</source>
        <translation>&lt;b&gt;影像配准 / 几何校正&lt;/b&gt;&lt;br&gt;&lt;br&gt;1. 打开源影像（File）&lt;br&gt;2. 在 SRC 与目标画布上采集 GCP&lt;br&gt;3. 在右侧设置变换方法、目标 CRS、输出路径&lt;br&gt;4. 查看残差；点数与方法满足后点工具栏「运行」&lt;br&gt;5. 在「校正任务」中查看进度；完成后双击可加载结果&lt;br&gt;&lt;br&gt;提示：将鼠标悬停在工具按钮或参数控件上可查看详细说明。</translation>
    </message>
    <message>
        <source>Pan (Space): drag to browse the source and reference / map canvases. Mutually exclusive with the point-adding tools.</source>
        <translation>平移 (Space)：在源与参考/地图画布上拖动浏览。与加点等工具互斥。</translation>
    </message>
    <message>
        <source>Zoom in (Ctrl++): drag a rectangle or click. Works on both canvases; the wheel zooms too.</source>
        <translation>放大 (Ctrl++)：框选或点击放大。两侧画布均可用。滚轮也可缩放。</translation>
    </message>
    <message>
        <source>Zoom out (Ctrl+-): drag a rectangle or click. Works on both canvases.</source>
        <translation>缩小 (Ctrl+-)：框选或点击缩小。两侧画布均可用。</translation>
    </message>
    <message>
        <source>Fit source (F): zooms the source image canvas to full extent.</source>
        <translation>适合源 (F)：源影像画布缩放到全图。</translation>
    </message>
    <message>
        <source>Fit reference / map (Shift+F): zooms the target canvas to full extent.</source>
        <translation>适合参考/地图 (Shift+F)：目标画布缩放到全图。</translation>
    </message>
    <message>
        <source>Fit both (Ctrl+Shift+F): zooms the source and target canvases to full extent.</source>
        <translation>适合两侧 (Ctrl+Shift+F)：源与目标画布均缩放到全图。</translation>
    </message>
    <message>
        <source>Previous extent (Alt+←): both canvases go back to the previous view extent.</source>
        <translation>上一范围 (Alt+←)：两侧画布回退到上一次视图范围。</translation>
    </message>
    <message>
        <source>Next extent (Alt+→): both canvases advance to the next view extent.</source>
        <translation>下一范围 (Alt+→)：两侧画布前进到下一次视图范围。</translation>
    </message>
    <message>
        <source>Add control point (A):
1. Click the source point on the source image (SRC)
2. Click the conjugate target point on the reference / map
Right-click cancels an unfinished source point. Points should be evenly spread.</source>
        <translation>添加控制点 (A)：
1. 在源影像 (SRC) 点击源点
2. 在参考/地图上点击同名目标点
右键取消未完成的源点。点宜均匀分布。</translation>
    </message>
    <message>
        <source>Move control point (M): drag an existing GCP marker to fine-tune; residuals recompute automatically.</source>
        <translation>移动控制点 (M)：拖动已有 GCP 标记微调，残差自动重算。</translation>
    </message>
    <message>
        <source>Delete control point (D): click a marker to delete it, or delete the row in the GCP table.</source>
        <translation>删除控制点 (D)：点击标记删除；或在 GCP 表中删除行。</translation>
    </message>
    <message>
        <source>Runs the geometric correction: after validating GCPs / the output path, the task joins the 'Correction Task' list and the warp executes in the background.
Multiple runs create multiple tasks; running ones can be cancelled in the task list.</source>
        <translation>运行几何校正：校验 GCP / 输出路径后，将任务加入「校正任务」列表并后台执行 warp。
可多次运行形成多条任务；运行中可在任务列表取消。</translation>
    </message>
    <message>
        <source>Cancelled the unfinished source point</source>
        <translation>已取消未完成的源点</translation>
    </message>
    <message>
        <source>Add a GCP: click an image point on the source image, then enter map coordinates in the dialog or pick them from the main window map</source>
        <translation>添加 GCP：在源影像上点击像点，然后在对话框中填写地图坐标或从主窗口地图取点</translation>
    </message>
    <message>
        <source>Add a GCP: click the source point on the source canvas, then the same location on the reference image (right-click to cancel)</source>
        <translation>添加 GCP：先在源画布点击源点，再在参考影像上点击同名位置（右键取消）</translation>
    </message>
    <message>
        <source>Pan: drag the canvas to browse. Both canvases are active.</source>
        <translation>平移：拖动画布浏览。两侧画布均可操作。</translation>
    </message>
    <message>
        <source>Zoom in: click or drag a rectangle.</source>
        <translation>放大：点击或框选放大。</translation>
    </message>
    <message>
        <source>Zoom out: click or drag a rectangle.</source>
        <translation>缩小：点击或框选缩小。</translation>
    </message>
    <message>
        <source>Source canvas zoomed to full extent</source>
        <translation>源画布已适合全图</translation>
    </message>
    <message>
        <source>Reference canvas zoomed to full extent</source>
        <translation>参考画布已适合全图</translation>
    </message>
    <message>
        <source>Map canvas zoomed to full extent</source>
        <translation>地图画布已适合全图</translation>
    </message>
    <message>
        <source>Returned to the previous view extent</source>
        <translation>已回退上一视图范围</translation>
    </message>
    <message>
        <source>Advanced to the next view extent</source>
        <translation>已前进下一视图范围</translation>
    </message>
    <message>
        <source>View</source>
        <translation>视图</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>视图(&amp;V)</translation>
    </message>
    <message>
        <source>Zoom In One Level</source>
        <translation>放大一级</translation>
    </message>
    <message>
        <source>Zoom Out One Level</source>
        <translation>缩小一级</translation>
    </message>
    <message>
        <source>Zoom Source to Full Extent</source>
        <translation>适合源全图</translation>
    </message>
    <message>
        <source>Zoom Reference to Full Extent</source>
        <translation>适合参考全图</translation>
    </message>
    <message>
        <source>Zoom Map to Full Extent</source>
        <translation>适合地图全图</translation>
    </message>
    <message>
        <source>Located source point #%1</source>
        <translation>已定位到源点 #%1</translation>
    </message>
    <message>
        <source>Located target point #%1</source>
        <translation>已定位到目标点 #%1</translation>
    </message>
    <message>
        <source>GCP #%1 located on both sides</source>
        <translation>已两侧定位 GCP #%1</translation>
    </message>
    <message>
        <source>RMS: %1 px</source>
        <translation>RMS：%1 px</translation>
    </message>
    <message>
        <source>Fit error: %1</source>
        <translation>拟合错误：%1</translation>
    </message>
    <message>
        <source>Not enough GCPs (need %1, got %2)</source>
        <translation>GCP 数量不足（需要 %1，实际 %2）</translation>
    </message>
    <message>
        <source>Enter the output path</source>
        <translation>请填写输出路径</translation>
    </message>
    <message>
        <source>No source raster path specified</source>
        <translation>未指定源栅格路径</translation>
    </message>
    <message>
        <source>The transformation has not been fitted yet</source>
        <translation>变换尚未完成拟合</translation>
    </message>
    <message>
        <source>Cannot create the correction snapshot</source>
        <translation>无法创建校正快照</translation>
    </message>
    <message>
        <source>%1 → %2</source>
        <translation>%1 → %2</translation>
    </message>
    <message>
        <source>Task Center submission failed</source>
        <translation>Task Center 提交失败</translation>
    </message>
    <message>
        <source>Cannot submit the correction task</source>
        <translation>无法提交校正任务</translation>
    </message>
    <message>
        <source>Added to the task list as #%1 and started...</source>
        <translation>已加入任务列表 #%1 并开始运行…</translation>
    </message>
    <message>
        <source>Task #%1 is no longer running</source>
        <translation>任务 #%1 已不在运行</translation>
    </message>
    <message>
        <source>Cancelling task #%1...</source>
        <translation>正在取消任务 #%1…</translation>
    </message>
    <message>
        <source>Cannot load results: %1</source>
        <translation>无法加载结果: %1</translation>
    </message>
    <message>
        <source>Requested to load into the main project: %1</source>
        <translation>已请求加载到主工程: %1</translation>
    </message>
    <message>
        <source>Source point selected (%1, %2) — click the same location on the reference / map canvas (right-click to cancel)</source>
        <translation>已选源点 (%1, %2) — 请在参考/地图画布上点击同名位置（右键取消）</translation>
    </message>
    <message>
        <source>Source point is invalid (0,0). Pick it again on the source canvas, then click the target position.</source>
        <translation>源点坐标无效 (0,0)。请重新在源画布上取点，再点目标位置。</translation>
    </message>
    <message>
        <source> ⚠ Source and reference are about %1 km apart — turn off Sync zoom, confirm both CRS match, then resample.</source>
        <translation> ⚠ 源影像与参考影像相距约 %1 km——请关闭同步缩放，确认两侧 CRS 一致后重新采样。</translation>
    </message>
    <message>
        <source>Added GCP #%1: source (%2, %3) → target (%4, %5)%6</source>
        <translation>已添加 GCP #%1：源 (%2, %3) → 目标 (%4, %5)%6</translation>
    </message>
    <message>
        <source>Click the source point on the source image canvas first</source>
        <translation>请先在源影像画布上点击源点</translation>
    </message>
    <message>
        <source>Cannot connect to the main map canvas: enter target X/Y directly in the GCP table, or open the main window first.</source>
        <translation>无法连接主地图画布：请在 GCP 表中直接填写目标 X/Y，或先打开主窗口。</translation>
    </message>
    <message>
        <source>Added GCP (source image point + map coordinates)</source>
        <translation>已添加 GCP（源像点 + 地图坐标）</translation>
    </message>
    <message>
        <source>Open source raster</source>
        <translation>打开源栅格</translation>
    </message>
    <message>
        <source>Raster (*.tif *.tiff *.img *.jp2);;All files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.jp2);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Select Source Image from Main Project (Warp)</source>
        <translation>从主工程选择源影像 (Warp)</translation>
    </message>
    <message>
        <source>Cannot open the source image: %1</source>
        <translation>无法打开源影像: %1</translation>
    </message>
    <message>
        <source>Loaded source image (Warp): %1</source>
        <translation>已加载源影像 (Warp): %1</translation>
    </message>
    <message>
        <source>No usable raster layer in the main project.
Load an image in the main window first, or use 'Open from File'.</source>
        <translation>主工程中没有可用的栅格图层。
请先在主窗口加载影像，或改用「从文件打开」。</translation>
    </message>
    <message>
        <source>Select raster layer:</source>
        <translation>选择栅格图层:</translation>
    </message>
    <message>
        <source>Geometric Correction</source>
        <translation>几何校正</translation>
    </message>
    <message>
        <source>A correction task is still running. Close anyway?</source>
        <translation>校正任务仍在运行，仍要关闭？</translation>
    </message>
    <message>
        <source>Unsaved control points</source>
        <translation>未保存的控制点</translation>
    </message>
    <message>
        <source>The GCP list has unsaved changes. Save them?</source>
        <translation>GCP 列表有未保存的更改。是否保存？</translation>
    </message>
    <message>
        <source>Save GCP points</source>
        <translation>保存 GCP 点</translation>
    </message>
    <message>
        <source>GCP Points (*.points *.gcp);;All files (*)</source>
        <translation>GCP 点文件 (*.points *.gcp);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Save GCPs</source>
        <translation>保存 GCP</translation>
    </message>
    <message>
        <source>Saving failed; the window stays open.</source>
        <translation>保存失败，窗口未关闭。</translation>
    </message>
    <message>
        <source>Load GCP points</source>
        <translation>加载 GCP 点</translation>
    </message>
    <message>
        <source>Load GCPs</source>
        <translation>加载 GCP</translation>
    </message>
    <message>
        <source>Failed to load GCP points from %1</source>
        <translation>从 %1 加载 GCP 点失败</translation>
    </message>
    <message>
        <source>Failed to save GCP points to %1</source>
        <translation>保存 GCP 点到 %1 失败</translation>
    </message>
</context>
<context>
    <name>QgsGeoreferencerMainWindow</name>
    <message>
        <source>Image Registration · Image 2 Image</source>
        <translation>影像配准 · 影像对影像</translation>
    </message>
    <message>
        <source>Source (Warp)</source>
        <translation>源 (Warp)</translation>
    </message>
    <message>
        <source>Base</source>
        <translation>基准 (Base)</translation>
    </message>
    <message>
        <source>Sync zoom enabled — confirm both sides share the same CRS, otherwise GCP coordinates may be wrong</source>
        <translation>已开启 Sync zoom — 请确认两侧 CRS 一致，否则 GCP 坐标可能错误</translation>
    </message>
    <message>
        <source>Load reference raster from file...</source>
        <translation>从文件加载参考栅格...</translation>
    </message>
    <message>
        <source>Load reference from project layer...</source>
        <translation>从工程图层加载参考...</translation>
    </message>
    <message>
        <source>Load .points...</source>
        <translation>加载 .points...</translation>
    </message>
    <message>
        <source>Import a saved control point file.</source>
        <translation>导入已保存的控制点文件。</translation>
    </message>
    <message>
        <source>Save .points...</source>
        <translation>保存 .points...</translation>
    </message>
    <message>
        <source>Exports the current control points; unsaved changes are flagged before the window closes.</source>
        <translation>导出当前控制点，关闭窗口前若有未保存更改也会提示。</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Closes this window (does not affect Image to Map).</source>
        <translation>关闭本窗口（不影响 Image 2 Map）。</translation>
    </message>
    <message>
        <source>Tools</source>
        <translation>工具</translation>
    </message>
    <message>
        <source>Image-to-Image tool: navigation, two-image registration, SIFT and run.</source>
        <translation>Image 2 Image 工具：导航、双影像配准、SIFT 与运行。</translation>
    </message>
    <message>
        <source>Sync Zoom</source>
        <translation>同步缩放</translation>
    </message>
    <message>
        <source>SIFT Auto Matching</source>
        <translation>SIFT 自动匹配</translation>
    </message>
    <message>
        <source>Template Matching</source>
        <translation>模板匹配</translation>
    </message>
    <message>
        <source>Source image canvas (SRC / Warp): loads the image to correct.
When adding a GCP, click the source point here first, then the conjugate point on the REF side (no coordinate form pops up).</source>
        <translation>源影像画布 (SRC / Warp)：加载待校正影像。
Add GCP 时先在此点击源点，再在右侧 REF 点击同名点（不弹坐标表单）。</translation>
    </message>
    <message>
        <source>Reference image canvas (REF / Base): loads the registered reference image.
When adding a GCP, click the conjugate position corresponding to the source point here to complete the control point pair.</source>
        <translation>参考影像画布 (REF / Base)：加载已配准参考影像。
Add GCP 时在此点击与源点对应的同名位置，完成一对控制点。</translation>
    </message>
    <message>
        <source>Sync zoom (off by default): use only when SRC and REF share a CRS and similar extents.
Keep Sync zoom off for already-registered image pairs, otherwise picked coordinates scramble and residuals go wild.</source>
        <translation>同步缩放（默认关闭）：仅当 SRC 与 REF 为同一 CRS 且范围相近时使用。
已配准影像对请保持关闭，否则取点坐标会错乱、残差异常。</translation>
    </message>
    <message>
        <source>Opens the reference image from a file into the right REF (Base) side, as the GCP target and alignment base.</source>
        <translation>从文件打开参考影像到右侧 REF（Base），作为 GCP 目标与对齐基准。</translation>
    </message>
    <message>
        <source>Chooses a raster from the main project layer list as the reference image (Base).</source>
        <translation>从主工程图层列表选择栅格作为参考影像（Base）。</translation>
    </message>
    <message>
        <source>Sync zoom (off by default): enable only when both sides share a CRS and similar extents.
With different CRSs, linking scrambles picked coordinates.</source>
        <translation>同步缩放（默认关）：两侧 CRS 一致且范围相近时才建议开启。
不同 CRS 时联动会弄乱取点坐标。</translation>
    </message>
    <message>
        <source>SIFT auto-matching: SRC and the reference image must be open. After feature extraction and inlier filtering, GCPs can be added in batch.
Needs OpenCV; provided by Image to Image only.</source>
        <translation>SIFT 自动匹配：需已打开 SRC 与参考影像。提取特征并筛选内点后，可批量添加 GCP。
需要 OpenCV；仅 Image 2 Image 提供。</translation>
    </message>
    <message>
        <source>Template matching (NCC): predicts the reference search area from the source image's initial geocoordinates, then runs correlation matching.
Suits remote-sensing imagery with approximate coordinates; grid sampling or existing rough GCPs serve as seeds. Needs OpenCV.</source>
        <translation>模板匹配（NCC）：利用源影像初始地理坐标预测参考影像搜索区，再做相关匹配。
适合已有近似坐标的遥感影像；可网格采样或用现有粗 GCP 作种子。需要 OpenCV。</translation>
    </message>
    <message>
        <source>&lt;b&gt;Image Registration · Image 2 Image&lt;/b&gt;&lt;br&gt;Two-image registration: source image (Warp) on the left, reference image (Base) on the right.&lt;br&gt;&lt;br&gt;&lt;b&gt;Typical Workflow&lt;/b&gt;&lt;br&gt;1. Open the source image: from a file or a main project layer&lt;br&gt;2. Open the reference image: from a file or a main project layer&lt;br&gt;3. Add / Move / Delete GCP become available once both sides are open&lt;br&gt;4. Navigation: pan / zoom in / zoom out; fit source / fit reference / fit both&lt;br&gt;5. Press Add GCP: SRC first, then REF (right-click to cancel an unfinished source point)&lt;br&gt;6. Optionally: template matching (needs SRC initial coordinates) / SIFT, Sync zoom → set output → run&lt;br&gt;&lt;br&gt;No RPC (use Image to Map for RPC).</source>
        <translation>&lt;b&gt;影像配准 · 影像对影像&lt;/b&gt;&lt;br&gt;双影像配准：左侧源影像 (Warp)，右侧参考影像 (Base)。&lt;br&gt;&lt;br&gt;&lt;b&gt;典型流程&lt;/b&gt;&lt;br&gt;1. 打开源影像：从文件 或 从主工程图层&lt;br&gt;2. 打开参考影像：从文件 或 从主工程图层&lt;br&gt;3. 两侧都打开后，Add / Move / Delete GCP 才可用&lt;br&gt;4. 导航：平移 / 放大 / 缩小；适合源 / 适合参考 / 适合两侧&lt;br&gt;5. 点选 Add GCP：先 SRC 再 REF（右键取消未完成源点）&lt;br&gt;6. 可选：模板匹配（需 SRC 初始坐标）/ SIFT、Sync zoom → 设置输出 → 运行&lt;br&gt;&lt;br&gt;不含 RPC（RPC 请用 Image 2 Map）。</translation>
    </message>
    <message>
        <source>OpenCV unavailable — SIFT disabled</source>
        <translation>OpenCV 不可用 — SIFT 已禁用</translation>
    </message>
    <message>
        <source>First use File → Load reference raster...</source>
        <translation>请先 File → Load reference raster…</translation>
    </message>
    <message>
        <source>Open the SRC image first</source>
        <translation>请先打开 SRC 影像</translation>
    </message>
    <message>
        <source>SIFT Matching</source>
        <translation>SIFT 匹配</translation>
    </message>
    <message>
        <source>SIFT cancelled</source>
        <translation>SIFT 已取消</translation>
    </message>
    <message>
        <source>SIFT failed: %1</source>
        <translation>SIFT 失败：%1</translation>
    </message>
    <message>
        <source>Unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <source>Found %1 matches, %2 inliers (%3%). Accept all of them?</source>
        <translation>找到 %1 对匹配，内点 %2 个 (%3%)，是否全部采用？</translation>
    </message>
    <message>
        <source>SIFT Matching Results</source>
        <translation>SIFT 匹配结果</translation>
    </message>
    <message>
        <source>SIFT matching...</source>
        <translation>SIFT 匹配中…</translation>
    </message>
    <message>
        <source>OpenCV unavailable — template matching disabled</source>
        <translation>OpenCV 不可用 — 模板匹配已禁用</translation>
    </message>
    <message>
        <source>Seed mode needs at least one existing GCP</source>
        <translation>种子模式需要至少一个已有 GCP</translation>
    </message>
    <message>
        <source>No seed points available</source>
        <translation>没有可用的种子点</translation>
    </message>
    <message>
        <source>Template matching cancelled</source>
        <translation>模板匹配已取消</translation>
    </message>
    <message>
        <source>Template matching failed: %1</source>
        <translation>模板匹配失败：%1</translation>
    </message>
    <message>
        <source>Tried %1 points, accepted %2 match pairs. Write them into the GCP list?</source>
        <translation>尝试 %1 点，接受 %2 对匹配，是否写入 GCP 列表？</translation>
    </message>
    <message>
        <source>Template Matching Results</source>
        <translation>模板匹配结果</translation>
    </message>
    <message>
        <source>Added %1 template-matching GCPs</source>
        <translation>已添加 %1 个模板匹配 GCP</translation>
    </message>
    <message>
        <source>Template matching...</source>
        <translation>模板匹配中…</translation>
    </message>
    <message>
        <source>Load reference raster</source>
        <translation>加载参考栅格</translation>
    </message>
    <message>
        <source>Raster (*.tif *.tiff *.img *.jp2);;All files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.jp2);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Select Reference Image from Main Project (Base)</source>
        <translation>从主工程选择参考影像 (Base)</translation>
    </message>
    <message>
        <source>Reference image (Base) — from a main project layer
Layer: %1
Path: %2</source>
        <translation>参考影像（基准 / Base）— 来自主工程图层
图层: %1
路径: %2</translation>
    </message>
    <message>
        <source>Cannot open the reference image: %1</source>
        <translation>无法打开参考影像: %1</translation>
    </message>
    <message>
        <source>Reference image (Base)
Layer: %1
Path: %2
CRS: %3</source>
        <translation>参考影像（基准 / Base）
图层: %1
路径: %2
CRS: %3</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Loaded reference image (Base): %1</source>
        <translation>已加载参考影像 (Base): %1</translation>
    </message>
    <message>
        <source>Reference image (Base)
Path: %1</source>
        <translation>参考影像（基准 / Base）
路径: %1</translation>
    </message>
</context>
<context>
    <name>QgsGuiVectorLayerTools</name>
    <message>
        <source>Add feature</source>
        <translation>添加要素</translation>
    </message>
    <message>
        <source>Start editing failed</source>
        <translation>开启编辑失败</translation>
    </message>
    <message>
        <source>Provider cannot be opened for editing</source>
        <translation>数据提供者无法以编辑方式打开</translation>
    </message>
    <message>
        <source>Stop Editing</source>
        <translation>停止编辑</translation>
    </message>
    <message>
        <source>Do you want to save the changes to layer %1?</source>
        <translation>是否保存对图层 %1 的更改？</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Problems during roll back</source>
        <translation>回滚时出现问题</translation>
    </message>
    <message>
        <source>The feature cannot be moved because 1 or more resulting geometries would be empty</source>
        <translation>无法移动该要素：至少一个结果几何会变成空几何</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
    <message>
        <source>Commit Errors</source>
        <translation>提交错误</translation>
    </message>
    <message>
        <source>Could not commit changes to layer %1</source>
        <translation>无法将更改提交到图层 %1</translation>
    </message>
    <message>
        <source>Errors: %1
</source>
        <translation>错误：%1
</translation>
    </message>
    <message>
        <source>Show more</source>
        <translation>显示更多</translation>
    </message>
    <message>
        <source>Commit errors</source>
        <translation>提交错误</translation>
    </message>
</context>
<context>
    <name>QgsLayoutDesignerDialog</name>
    <message>
        <source>Layout Designer</source>
        <translation>布局设计器</translation>
    </message>
    <message>
        <source>Item Properties</source>
        <translation>项目属性</translation>
    </message>
    <message>
        <source>Select an item to edit its properties.

Page setup is available via Layout → Page Properties.</source>
        <translation>选择一个项目以编辑其属性。

页面设置可通过 布局 → 页面属性 打开。</translation>
    </message>
    <message>
        <source>%1 items selected — edits apply to all selected items.</source>
        <translation>已选择 %1 个项目——修改将应用到全部选中项。</translation>
    </message>
    <message>
        <source>Common Properties</source>
        <translation>公共属性</translation>
    </message>
    <message>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <source>Opacity</source>
        <translation>不透明度</translation>
    </message>
    <message>
        <source> °</source>
        <translation> °</translation>
    </message>
    <message>
        <source>Rotation</source>
        <translation>旋转</translation>
    </message>
    <message>
        <source>Visible</source>
        <translation>可见</translation>
    </message>
    <message>
        <source>Locked</source>
        <translation>锁定</translation>
    </message>
    <message>
        <source>History</source>
        <translation>历史</translation>
    </message>
    <message>
        <source>&amp;Layout</source>
        <translation>布局(&amp;L)</translation>
    </message>
    <message>
        <source>Page Properties...</source>
        <translation>页面属性...</translation>
    </message>
    <message>
        <source>Auto Arrange (Thematic Composition)...</source>
        <translation>自动排版（专题组版）...</translation>
    </message>
    <message>
        <source>Save as Template...</source>
        <translation>另存为模板...</translation>
    </message>
    <message>
        <source>Load from Template...</source>
        <translation>从模板加载...</translation>
    </message>
    <message>
        <source>Export to PDF...</source>
        <translation>导出为 PDF...</translation>
    </message>
    <message>
        <source>Export to Image...</source>
        <translation>导出为图片...</translation>
    </message>
    <message>
        <source>Export to SVG...</source>
        <translation>导出为 SVG...</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <source>Duplicate Selected Items</source>
        <translation>复制选中项目</translation>
    </message>
    <message>
        <source>Delete Selected Items</source>
        <translation>删除选中项目</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
    <message>
        <source>Deselect All</source>
        <translation>全部取消选择</translation>
    </message>
    <message>
        <source>Ordering</source>
        <translation>层级排序</translation>
    </message>
    <message>
        <source>Raise Items</source>
        <translation>上移项目</translation>
    </message>
    <message>
        <source>Lower Items</source>
        <translation>下移项目</translation>
    </message>
    <message>
        <source>Bring to Front</source>
        <translation>置于顶层</translation>
    </message>
    <message>
        <source>Send to Back</source>
        <translation>置于底层</translation>
    </message>
    <message>
        <source>Lock Items</source>
        <translation>锁定项</translation>
    </message>
    <message>
        <source>Unlock Items</source>
        <translation>解锁项</translation>
    </message>
    <message>
        <source>Unlock All Items</source>
        <translation>解锁全部项目</translation>
    </message>
    <message>
        <source>Align Items</source>
        <translation>对齐项目</translation>
    </message>
    <message>
        <source>Align Left</source>
        <translation>左对齐</translation>
    </message>
    <message>
        <source>Align Horizontal Center</source>
        <translation>水平居中</translation>
    </message>
    <message>
        <source>Align Right</source>
        <translation>右对齐</translation>
    </message>
    <message>
        <source>Align Top</source>
        <translation>顶对齐</translation>
    </message>
    <message>
        <source>Align Vertical Center</source>
        <translation>垂直居中</translation>
    </message>
    <message>
        <source>Align Bottom</source>
        <translation>底对齐</translation>
    </message>
    <message>
        <source>Distribute Items</source>
        <translation>分布项目</translation>
    </message>
    <message>
        <source>Distribute Left Edges</source>
        <translation>按左边界分布</translation>
    </message>
    <message>
        <source>Distribute Centers</source>
        <translation>按中心分布</translation>
    </message>
    <message>
        <source>Distribute Horizontal Spacing</source>
        <translation>水平等间距分布</translation>
    </message>
    <message>
        <source>Distribute Right Edges</source>
        <translation>按右边界分布</translation>
    </message>
    <message>
        <source>Distribute Top Edges</source>
        <translation>按上边界分布</translation>
    </message>
    <message>
        <source>Distribute Vertical Centers</source>
        <translation>按垂直中心分布</translation>
    </message>
    <message>
        <source>Distribute Vertical Spacing</source>
        <translation>垂直等间距分布</translation>
    </message>
    <message>
        <source>Distribute Bottom Edges</source>
        <translation>按下边界分布</translation>
    </message>
    <message>
        <source>&amp;View</source>
        <translation>视图(&amp;V)</translation>
    </message>
    <message>
        <source>Zoom to Page</source>
        <translation>缩放到整页</translation>
    </message>
    <message>
        <source>Zoom In</source>
        <translation>放大</translation>
    </message>
    <message>
        <source>Zoom Out</source>
        <translation>缩小</translation>
    </message>
    <message>
        <source>Zoom to 100%</source>
        <translation>缩放到 100%</translation>
    </message>
    <message>
        <source>&amp;Items</source>
        <translation>项目(&amp;I)</translation>
    </message>
    <message>
        <source>Add Map</source>
        <translation>添加地图</translation>
    </message>
    <message>
        <source>Add Legend</source>
        <translation>添加图例</translation>
    </message>
    <message>
        <source>Add Scale Bar</source>
        <translation>添加比例尺</translation>
    </message>
    <message>
        <source>Add North Arrow</source>
        <translation>添加指北针</translation>
    </message>
    <message>
        <source>Add Grid</source>
        <translation>添加格网</translation>
    </message>
    <message>
        <source>Add Label</source>
        <translation>添加标签</translation>
    </message>
    <message>
        <source>Add Image</source>
        <translation>添加图片</translation>
    </message>
    <message>
        <source>Add Shape</source>
        <translation>添加形状</translation>
    </message>
    <message>
        <source>Add Chart</source>
        <translation>添加图表</translation>
    </message>
    <message>
        <source>&amp;Atlas</source>
        <translation>地图集(&amp;A)</translation>
    </message>
    <message>
        <source>&amp;Report</source>
        <translation>报告(&amp;R)</translation>
    </message>
    <message>
        <source>&amp;Settings</source>
        <translation>设置(&amp;S)</translation>
    </message>
    <message>
        <source>Snap to Grid</source>
        <translation>捕捉到格网</translation>
    </message>
    <message>
        <source>Snap to Guides</source>
        <translation>捕捉到参考线</translation>
    </message>
    <message>
        <source>Snap to Items</source>
        <translation>捕捉到项目</translation>
    </message>
    <message>
        <source>Snap Tolerance...</source>
        <translation>捕捉容差...</translation>
    </message>
    <message>
        <source>Snap Tolerance</source>
        <translation>捕捉容差</translation>
    </message>
    <message>
        <source>Tolerance (pixels):</source>
        <translation>容差（像素）：</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>导航</translation>
    </message>
    <message>
        <source>Select</source>
        <translation>选择</translation>
    </message>
    <message>
        <source>Select / move items</source>
        <translation>选择 / 移动项目</translation>
    </message>
    <message>
        <source>Pan</source>
        <translation>平移</translation>
    </message>
    <message>
        <source>Pan layout view</source>
        <translation>平移布局视图</translation>
    </message>
    <message>
        <source>Zoom</source>
        <translation>缩放</translation>
    </message>
    <message>
        <source>Zoom in / out</source>
        <translation>放大 / 缩小</translation>
    </message>
    <message>
        <source>Items</source>
        <translation>项目</translation>
    </message>
    <message>
        <source>Map</source>
        <translation>地图</translation>
    </message>
    <message>
        <source>Legend</source>
        <translation>图例</translation>
    </message>
    <message>
        <source>Scale</source>
        <translation>比例尺</translation>
    </message>
    <message>
        <source>North</source>
        <translation>指北针</translation>
    </message>
    <message>
        <source>Grid</source>
        <translation>网格</translation>
    </message>
    <message>
        <source>Export</source>
        <translation>导出</translation>
    </message>
    <message>
        <source>PDF...</source>
        <translation>PDF...</translation>
    </message>
    <message>
        <source>Image...</source>
        <translation>图片...</translation>
    </message>
    <message>
        <source>Map added (linked to canvas)</source>
        <translation>已添加地图（链接到画布）</translation>
    </message>
    <message>
        <source>Add a map item first.</source>
        <translation>请先添加地图项目。</translation>
    </message>
    <message>
        <source>Legend added (linked to map)</source>
        <translation>已添加图例（链接到地图）</translation>
    </message>
    <message>
        <source>Scale Bar</source>
        <translation>比例尺</translation>
    </message>
    <message>
        <source>Scale bar added (linked to map)</source>
        <translation>已添加比例尺（链接到地图）</translation>
    </message>
    <message>
        <source>North Arrow</source>
        <translation>指北针</translation>
    </message>
    <message>
        <source>North arrow added (linked to map)</source>
        <translation>已添加指北针（链接到地图）</translation>
    </message>
    <message>
        <source>Grid 1</source>
        <translation>格网 1</translation>
    </message>
    <message>
        <source>Grid added</source>
        <translation>已添加格网</translation>
    </message>
    <message>
        <source>Map Title</source>
        <translation>地图标题</translation>
    </message>
    <message>
        <source>Label added</source>
        <translation>已添加标签</translation>
    </message>
    <message>
        <source>Select Image</source>
        <translation>选择图片</translation>
    </message>
    <message>
        <source>Images (*.png *.jpg *.svg)</source>
        <translation>图片 (*.png *.jpg *.svg)</translation>
    </message>
    <message>
        <source>Image added</source>
        <translation>已添加图片</translation>
    </message>
    <message>
        <source>Shape added</source>
        <translation>已添加形状</translation>
    </message>
    <message>
        <source>Chart added (select a vector layer in its properties)</source>
        <translation>已添加图表（请在属性中选择矢量图层）</translation>
    </message>
    <message>
        <source>Deleted %1 item(s)</source>
        <translation>已删除 %1 个项目</translation>
    </message>
    <message>
        <source>Duplicate</source>
        <translation>创建副本</translation>
    </message>
    <message>
        <source>Select item(s) to duplicate first.</source>
        <translation>请先选择要复制的项目。</translation>
    </message>
    <message>
        <source>Duplicated %1 item(s)</source>
        <translation>已复制 %1 个项目</translation>
    </message>
    <message>
        <source>Raise Item</source>
        <translation>上移项目</translation>
    </message>
    <message>
        <source>Lower Item</source>
        <translation>下移项目</translation>
    </message>
    <message>
        <source>Locked %1 item(s)</source>
        <translation>已锁定 %1 个项目</translation>
    </message>
    <message>
        <source>Unlocked %1 item(s)</source>
        <translation>已解锁 %1 个项目</translation>
    </message>
    <message>
        <source>Page</source>
        <translation>页面</translation>
    </message>
    <message>
        <source>Layout has no pages.</source>
        <translation>布局中没有任何页面。</translation>
    </message>
    <message>
        <source>Thematic composition arranged (%1 new component(s); existing non-auto items untouched)</source>
        <translation>专题组版完成（新增 %1 个组件；已有非自动项目保持不变）</translation>
    </message>
    <message>
        <source>Save as Template</source>
        <translation>另存为模板</translation>
    </message>
    <message>
        <source>QGIS Layout Template (*.qpt)</source>
        <translation>QGIS 布局模板 (*.qpt)</translation>
    </message>
    <message>
        <source>Template saved to %1</source>
        <translation>模板已保存到 %1</translation>
    </message>
    <message>
        <source>Save Template</source>
        <translation>保存模板</translation>
    </message>
    <message>
        <source>Failed to save template.</source>
        <translation>保存模板失败。</translation>
    </message>
    <message>
        <source>Load from Template</source>
        <translation>从模板加载</translation>
    </message>
    <message>
        <source>Load Template</source>
        <translation>加载模板</translation>
    </message>
    <message>
        <source>Cannot read %1</source>
        <translation>无法读取 %1</translation>
    </message>
    <message>
        <source>%1 is not a valid template file.</source>
        <translation>%1 不是有效的模板文件。</translation>
    </message>
    <message>
        <source>Template loaded from %1</source>
        <translation>已从 %1 加载模板</translation>
    </message>
    <message>
        <source>Failed to load template from %1</source>
        <translation>从 %1 加载模板失败</translation>
    </message>
    <message>
        <source>Export to PDF</source>
        <translation>导出为 PDF</translation>
    </message>
    <message>
        <source>PDF (*.pdf)</source>
        <translation>PDF (*.pdf)</translation>
    </message>
    <message>
        <source>Exported to %1</source>
        <translation>已导出到 %1</translation>
    </message>
    <message>
        <source>Export Failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <source>Failed to export to PDF: %1</source>
        <translation>导出 PDF 失败：%1</translation>
    </message>
    <message>
        <source>Export to Image</source>
        <translation>导出为图片</translation>
    </message>
    <message>
        <source>PNG (*.png);;JPEG (*.jpg)</source>
        <translation>PNG (*.png);;JPEG (*.jpg)</translation>
    </message>
    <message>
        <source>Failed to export to image: %1</source>
        <translation>导出图片失败：%1</translation>
    </message>
    <message>
        <source>Export to SVG</source>
        <translation>导出为 SVG</translation>
    </message>
    <message>
        <source>SVG (*.svg)</source>
        <translation>SVG (*.svg)</translation>
    </message>
    <message>
        <source>Failed to export to SVG: %1</source>
        <translation>导出 SVG 失败：%1</translation>
    </message>
</context>
<context>
    <name>QgsLockedFeature</name>
    <message>
        <source>Validation started.</source>
        <translation>校验已开始。</translation>
    </message>
    <message numerus="yes">
        <source>Validation finished (%n error(s) found).</source>
        <comment>number of geometry errors</comment>
        <translation><numerusform>校验完成（发现 %n 个错误）。</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>QgsMapCoordsDialog</name>
    <message>
        <source>Pick Point from Map</source>
        <translation>从地图取点</translation>
    </message>
    <message>
        <source>After clicking, pick a point on the main map and the coordinates fill in automatically.</source>
        <translation>点击后在主地图上点选一点，自动填入坐标。</translation>
    </message>
    <message>
        <source>Pick point from the map canvas</source>
        <translation>从地图画布取点</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens help for entering GCP target coordinates.</source>
        <translation>打开 GCP 目标坐标输入帮助说明。</translation>
    </message>
    <message>
        <source>Target X coordinate (longitude or projected easting; decimal degrees or DMS accepted)</source>
        <translation>目标 X 坐标（经度或投影东坐标，支持十进制度或度分秒 DMS）</translation>
    </message>
    <message>
        <source>Target Y coordinate (latitude or projected northing; decimal degrees or DMS accepted)</source>
        <translation>目标 Y 坐标（纬度或投影北坐标，支持十进制度或度分秒 DMS）</translation>
    </message>
    <message>
        <source>Minimizes the registration window when 'Pick Point from Map' is clicked, making it easy to pick points on the main canvas</source>
        <translation>点击「从地图取点」时自动最小化配准窗口，便于在主画布上选点</translation>
    </message>
    <message>
        <source>Target CRS for the GCP points</source>
        <translation>指定 GCP 点的目标坐标参考系 (CRS)</translation>
    </message>
</context>
<context>
    <name>QgsMapToolAddFeature</name>
    <message>
        <source>Add feature</source>
        <translation>添加要素</translation>
    </message>
    <message>
        <source>add feature</source>
        <translation>添加要素</translation>
    </message>
</context>
<context>
    <name>QgsMapToolAddPart</name>
    <message>
        <source>Add part</source>
        <translation>添加部件</translation>
    </message>
    <message>
        <source>Part added</source>
        <translation>已添加部件</translation>
    </message>
    <message>
        <source>New part's geometry is empty or invalid.</source>
        <translation>新部件的几何为空或无效。</translation>
    </message>
    <message>
        <source>Selected feature is not multi part.</source>
        <translation>所选要素不是多部件要素。</translation>
    </message>
    <message>
        <source>No feature selected. Please select a feature with the selection tool or in the attribute table.</source>
        <translation>未选择要素。请使用选择工具或在属性表中选择一个要素。</translation>
    </message>
    <message>
        <source>Several features are selected. Please select only one feature to which an island should be added.</source>
        <translation>已选择多个要素。添加岛洞时一次只能选择一个要素。</translation>
    </message>
    <message>
        <source>Selected geometry could not be found.</source>
        <translation>找不到所选几何。</translation>
    </message>
    <message>
        <source>Base geometry is not valid.</source>
        <translation>基础几何无效。</translation>
    </message>
    <message>
        <source>Unexpected OperationResult: %1</source>
        <translation>意外的操作结果：%1</translation>
    </message>
    <message>
        <source>Several features are selected. Please select only one feature to which a part should be added.</source>
        <translation>已选择多个要素。添加部件时一次只能选择一个要素。</translation>
    </message>
    <message>
        <source>This layer does not support multipart geometries.</source>
        <translation>该图层不支持多部件几何。</translation>
    </message>
    <message>
        <source>Could not add part. %1</source>
        <translation>添加部件失败。%1</translation>
    </message>
</context>
<context>
    <name>QgsMapToolAddRing</name>
    <message>
        <source>Add ring</source>
        <translation>添加环</translation>
    </message>
    <message>
        <source>Ring added</source>
        <translation>已添加环</translation>
    </message>
    <message>
        <source>a problem with geometry type occurred</source>
        <translation>几何类型出现问题</translation>
    </message>
    <message>
        <source>the inserted ring is not closed</source>
        <translation>插入的环未闭合</translation>
    </message>
    <message>
        <source>the inserted ring is not a valid geometry</source>
        <translation>插入的环不是有效几何</translation>
    </message>
    <message>
        <source>the inserted ring crosses existing rings</source>
        <translation>插入的环与现有环相交</translation>
    </message>
    <message>
        <source>the inserted ring is not contained in a feature</source>
        <translation>插入的环未包含在要素内</translation>
    </message>
    <message>
        <source>an unknown error occurred (%1)</source>
        <translation>发生未知错误（%1）</translation>
    </message>
    <message>
        <source>Could not add ring: %1.</source>
        <translation>添加环失败：%1。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolChamferFillet</name>
    <message>
        <source>Chamfer or fillet</source>
        <translation>倒角或圆角</translation>
    </message>
    <message>
        <source>Generated geometry is not valid: '%1'. </source>
        <translation>生成的几何无效：“%1”。</translation>
    </message>
    <message>
        <source>Chamfer/fillet</source>
        <translation>倒角/圆角</translation>
    </message>
    <message>
        <source>Chamfer/fillet: input geometry is invalid!</source>
        <translation>倒角/圆角：输入几何无效！</translation>
    </message>
    <message>
        <source>Creating chamfer/fillet geometry failed: %1</source>
        <translation>创建倒角/圆角几何失败：%1</translation>
    </message>
</context>
<context>
    <name>QgsMapToolDeletePart</name>
    <message>
        <source>Delete part</source>
        <translation>删除部件</translation>
    </message>
    <message>
        <source>If there are selected features, the delete parts tool only applies to those. Clear the selection and try again.</source>
        <translation>若存在选中要素，删除部件工具仅对其生效。请清除选择后重试。</translation>
    </message>
    <message>
        <source>Part of multipart feature deleted</source>
        <translation>已删除多部件要素的一个部件</translation>
    </message>
    <message>
        <source>Couldn't remove the selected part.</source>
        <translation>无法移除所选部件。</translation>
    </message>
    <message>
        <source>All geometry parts deleted from feature %1. Feature has no geometry now!</source>
        <translation>要素 %1 的所有几何部件均已被删除，该要素已没有几何！</translation>
    </message>
</context>
<context>
    <name>QgsMapToolDeleteRing</name>
    <message>
        <source>Delete ring</source>
        <translation>删除环</translation>
    </message>
    <message>
        <source>Delete ring can only be used in a polygon layer.</source>
        <translation>删除环只能在面图层中使用。</translation>
    </message>
    <message>
        <source>If there are selected features, the delete ring tool only applies to those. Clear the selection and try again.</source>
        <translation>若存在选中要素，删除环工具仅对其生效。请清除选择后重试。</translation>
    </message>
    <message>
        <source>Ring deleted</source>
        <translation>已删除环</translation>
    </message>
</context>
<context>
    <name>QgsMapToolFeatureAction</name>
    <message>
        <source>To run an action, you must choose an active vector layer.</source>
        <translation>运行动作前必须选择一个活动矢量图层。</translation>
    </message>
    <message>
        <source>The active vector layer has no defined actions</source>
        <translation>活动矢量图层没有定义动作</translation>
    </message>
    <message>
        <source>No features found at this position.</source>
        <translation>该位置未找到要素。</translation>
    </message>
    <message>
        <source>All Features</source>
        <translation>全部要素</translation>
    </message>
    <message>
        <source>Security warning</source>
        <translation>安全警告</translation>
    </message>
    <message>
        <source>The action contains an embedded script which has been denied execution.</source>
        <translation>该动作包含内嵌脚本，已被拒绝执行。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolFeatureArray</name>
    <message>
        <source>Copy in an array of features</source>
        <translation>按阵列复制要素</translation>
    </message>
    <message>
        <source>Feature array created</source>
        <translation>已创建要素阵列</translation>
    </message>
    <message>
        <source>Unable to transform coordinates between layer and map CRS</source>
        <translation>无法在图层 CRS 与地图 CRS 之间变换坐标</translation>
    </message>
    <message>
        <source>Unable to copy and translate feature preview</source>
        <translation>无法复制并平移要素预览</translation>
    </message>
</context>
<context>
    <name>QgsMapToolFillRing</name>
    <message>
        <source>Fill ring</source>
        <translation>填充环</translation>
    </message>
    <message>
        <source>Ring added and filled</source>
        <translation>已添加并填充环</translation>
    </message>
    <message>
        <source>a problem with geometry type occurred</source>
        <translation>几何类型出现问题</translation>
    </message>
    <message>
        <source>the inserted Ring is not closed</source>
        <translation>插入的环未闭合</translation>
    </message>
    <message>
        <source>the inserted Ring is not a valid geometry</source>
        <translation>插入的环不是有效几何</translation>
    </message>
    <message>
        <source>the inserted Ring crosses existing rings</source>
        <translation>插入的环与现有环相交</translation>
    </message>
    <message>
        <source>the inserted Ring is not contained in a feature</source>
        <translation>插入的环未包含在要素内</translation>
    </message>
    <message>
        <source>an unknown error occurred</source>
        <translation>发生未知错误</translation>
    </message>
    <message>
        <source>could not add ring: %1.</source>
        <translation>添加环失败：%1。</translation>
    </message>
    <message>
        <source>No ring found to fill.</source>
        <translation>未找到可填充的环。</translation>
    </message>
    <message>
        <source>Ring filled</source>
        <translation>已填充环</translation>
    </message>
</context>
<context>
    <name>QgsMapToolMoveFeature</name>
    <message>
        <source>Move feature</source>
        <translation>移动要素</translation>
    </message>
    <message>
        <source>Move features</source>
        <translation>移动要素</translation>
    </message>
    <message>
        <source>Some of the selected features are outside of the current map view. Would you still like to continue?</source>
        <translation>部分选中要素位于当前地图视图之外，是否仍然继续？</translation>
    </message>
    <message>
        <source>Feature moved</source>
        <translation>要素已移动</translation>
    </message>
    <message>
        <source>Feature copied and moved</source>
        <translation>要素已复制并移动</translation>
    </message>
    <message>
        <source>The feature cannot be moved because the resulting geometry would be empty</source>
        <translation>无法移动该要素：结果几何会变成空几何</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
</context>
<context>
    <name>QgsMapToolOffsetCurve</name>
    <message>
        <source>Map tool offset curve</source>
        <translation>地图工具：偏移曲线</translation>
    </message>
    <message>
        <source>Could not find a nearby feature in any vector layer.</source>
        <translation>在任何矢量图层中都找不到附近的要素。</translation>
    </message>
    <message>
        <source>Generated geometry is not valid.</source>
        <translation>生成的几何无效。</translation>
    </message>
    <message>
        <source>Offset curve</source>
        <translation>偏移曲线</translation>
    </message>
    <message>
        <source>The feature cannot be modified because the resulting geometry would be empty</source>
        <translation>无法修改该要素：结果几何会变成空几何</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
    <message>
        <source>Creating offset geometry failed: %1</source>
        <translation>创建偏移几何失败：%1</translation>
    </message>
</context>
<context>
    <name>QgsMapToolReshape</name>
    <message>
        <source>Reshape features</source>
        <translation>重塑要素</translation>
    </message>
    <message>
        <source>Cannot transform the point to the layers coordinate system</source>
        <translation>无法将该点变换到图层坐标系</translation>
    </message>
    <message>
        <source>Reshape</source>
        <translation>整形</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
    <message>
        <source>The feature cannot be reshaped because the resulting geometry is empty</source>
        <translation>无法重塑该要素：结果几何为空</translation>
    </message>
</context>
<context>
    <name>QgsMapToolReverseLine</name>
    <message>
        <source>Reverse line geometry</source>
        <translation>反转线几何</translation>
    </message>
    <message>
        <source>Reverse line</source>
        <translation>反转线</translation>
    </message>
    <message>
        <source>Line reversed.</source>
        <translation>线已反转。</translation>
    </message>
    <message>
        <source>Couldn't reverse the selected part.</source>
        <translation>无法反转所选部件。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolRotateFeature</name>
    <message>
        <source>Rotate feature</source>
        <translation>旋转要素</translation>
    </message>
    <message>
        <source>Could not find a nearby feature in the current layer.</source>
        <translation>当前图层中找不到附近的要素。</translation>
    </message>
    <message>
        <source>Features Rotated</source>
        <translation>要素已旋转</translation>
    </message>
    <message>
        <source>The feature cannot be rotated because the resulting geometry would be empty</source>
        <translation>无法旋转该要素：结果几何会变成空几何</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
</context>
<context>
    <name>QgsMapToolScaleFeature</name>
    <message>
        <source>Scale feature</source>
        <translation>缩放要素</translation>
    </message>
    <message>
        <source>Could not find a nearby feature in the current layer.</source>
        <translation>当前图层中找不到附近的要素。</translation>
    </message>
    <message>
        <source>Features Scaled</source>
        <translation>要素已缩放</translation>
    </message>
    <message>
        <source>The feature cannot be scaled because the resulting geometry would be empty</source>
        <translation>无法缩放该要素：结果几何会变成空几何</translation>
    </message>
    <message>
        <source>An error was reported during intersection removal</source>
        <translation>移除相交部分时报告了错误</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSelect</name>
    <message>
        <source>Select features</source>
        <translation>选择要素</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSelectUtils::QgsMapToolSelectMenuActions</name>
    <message>
        <source>Select Feature</source>
        <translation>选择要素</translation>
    </message>
    <message>
        <source>Add to Selection</source>
        <translation>加入选择集</translation>
    </message>
    <message>
        <source>Intersect with Selection</source>
        <translation>与选择集求交</translation>
    </message>
    <message>
        <source>Remove from Selection</source>
        <translation>从选择集移除</translation>
    </message>
    <message>
        <source>Searching…</source>
        <translation>搜索中…</translation>
    </message>
    <message>
        <source>Select All (%1)</source>
        <translation>全选（%1）</translation>
    </message>
    <message>
        <source>Add All to Selection (%1)</source>
        <translation>全部加入选择集（%1）</translation>
    </message>
    <message>
        <source>Intersect All with Selection (%1)</source>
        <translation>全部与选择集求交（%1）</translation>
    </message>
    <message>
        <source>Remove All from Selection (%1)</source>
        <translation>全部移出选择集（%1）</translation>
    </message>
    <message>
        <source>Add Feature to Selection</source>
        <translation>将要素加入选择集</translation>
    </message>
    <message>
        <source>Intersect Feature with Selection</source>
        <translation>要素与选择集求交</translation>
    </message>
    <message>
        <source>Remove Feature from Selection</source>
        <translation>将要素移出选择集</translation>
    </message>
    <message>
        <source>Feature %1</source>
        <translation>要素 %1</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSelectionHandler</name>
    <message>
        <source>Selection radius:</source>
        <translation>选择半径：</translation>
    </message>
</context>
<context>
    <name>QgsMapToolShapeCircle2TangentsPoint</name>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Segments are parallels</source>
        <translation>线段相互平行</translation>
    </message>
    <message>
        <source>Radius of the circle: </source>
        <translation>圆的半径：</translation>
    </message>
</context>
<context>
    <name>QgsMapToolShapeCircle3Tangents</name>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>The three segments are parallel</source>
        <translation>三条线段相互平行</translation>
    </message>
</context>
<context>
    <name>QgsMapToolShapeCircularStringRadius</name>
    <message>
        <source>Radius: </source>
        <translation>半径：</translation>
    </message>
</context>
<context>
    <name>QgsMapToolShapeRegularPolygonAbstract</name>
    <message>
        <source>Number of sides: </source>
        <translation>边数：</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSimplify</name>
    <message>
        <source>Geometry simplified</source>
        <translation>几何已简化</translation>
    </message>
    <message>
        <source>Could not find a nearby feature in the current layer.</source>
        <translation>当前图层中找不到附近的要素。</translation>
    </message>
    <message>
        <source>%1 feature(s): %2 to %3 vertices (%4%)</source>
        <translation>%1 个要素：顶点由 %2 减至 %3（%4%）</translation>
    </message>
    <message>
        <source>Simplification failed!</source>
        <translation>简化失败！</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSplitFeatures</name>
    <message>
        <source>Split features</source>
        <translation>分割要素</translation>
    </message>
    <message>
        <source>Coordinate transform error</source>
        <translation>坐标变换错误</translation>
    </message>
    <message>
        <source>Cannot transform the point to the layers coordinate system</source>
        <translation>无法将该点变换到图层坐标系</translation>
    </message>
    <message>
        <source>Features split</source>
        <translation>要素已分割</translation>
    </message>
    <message>
        <source>Topological points from Features split</source>
        <translation>要素分割产生的拓扑点</translation>
    </message>
    <message>
        <source>No features were split</source>
        <translation>没有要素被分割</translation>
    </message>
    <message>
        <source>If there are selected features, the split tool only applies to those. If you would like to split all features under the split line, clear the selection.</source>
        <translation>若存在选中要素，分割工具仅对其生效。要分割分割线下的全部要素，请清除选择。</translation>
    </message>
    <message>
        <source>No feature split done</source>
        <translation>未执行任何要素分割</translation>
    </message>
    <message>
        <source>Cut edges detected. Make sure the line splits features into multiple parts.</source>
        <translation>检测到切割边。请确保该线能把要素切分为多个部分。</translation>
    </message>
    <message>
        <source>The geometry is invalid. Please repair before trying to split it.</source>
        <translation>几何无效，请先修复再尝试分割。</translation>
    </message>
    <message>
        <source>An error occurred during splitting.</source>
        <translation>分割过程中发生错误。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolSplitParts</name>
    <message>
        <source>Split parts</source>
        <translation>分割部件</translation>
    </message>
    <message>
        <source>Coordinate transform error</source>
        <translation>坐标变换错误</translation>
    </message>
    <message>
        <source>Cannot transform the point to the layers coordinate system</source>
        <translation>无法将该点变换到图层坐标系</translation>
    </message>
    <message>
        <source>Parts split</source>
        <translation>部件已分割</translation>
    </message>
    <message>
        <source>No parts were split</source>
        <translation>没有部件被分割</translation>
    </message>
    <message>
        <source>If there are selected parts, the split tool only applies to those. If you would like to split all parts under the split line, clear the selection.</source>
        <translation>若存在选中部件，分割工具仅对其生效。要分割分割线下的全部部件，请清除选择。</translation>
    </message>
    <message>
        <source>No part split done</source>
        <translation>未执行任何部件分割</translation>
    </message>
    <message>
        <source>Cut edges detected. Make sure the line splits parts into multiple parts.</source>
        <translation>检测到切割边。请确保该线能把部件切分为多个部分。</translation>
    </message>
    <message>
        <source>The geometry is invalid. Please repair before trying to split it.</source>
        <translation>几何无效，请先修复再尝试分割。</translation>
    </message>
    <message>
        <source>Split error</source>
        <translation>分割错误</translation>
    </message>
    <message>
        <source>An error occurred during splitting.</source>
        <translation>分割过程中发生错误。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolTrimExtendFeature</name>
    <message>
        <source>Trim/Extend feature</source>
        <translation>修剪/延伸要素</translation>
    </message>
    <message>
        <source>Feature trimmed/extended.</source>
        <translation>要素已修剪/延伸。</translation>
    </message>
    <message>
        <source>Couldn't trim or extend the feature.</source>
        <translation>无法修剪或延伸该要素。</translation>
    </message>
</context>
<context>
    <name>QgsMapToolsDigitizingTechniqueManager</name>
    <message>
        <source>R</source>
        <comment>Keyboard shortcut: toggle stream digitizing</comment>
        <translation>R</translation>
    </message>
    <message>
        <source>NURBS Degree</source>
        <translation>NURBS 次数</translation>
    </message>
</context>
<context>
    <name>QgsMergeAttributesDialog</name>
    <message>
        <source>Take attributes from feature with the most points</source>
        <translation>取顶点最多的要素属性</translation>
    </message>
    <message>
        <source>Take all attributes from the MultiPoint feature with the most parts</source>
        <translation>取部件最多的多点要素的全部属性</translation>
    </message>
    <message>
        <source>Take attributes from feature with the longest length</source>
        <translation>取长度最长的要素属性</translation>
    </message>
    <message>
        <source>Take all attributes from the Line feature with the longest length</source>
        <translation>取长度最长的线要素的全部属性</translation>
    </message>
    <message>
        <source>Take attributes from feature with the largest area</source>
        <translation>取面积最大的要素属性</translation>
    </message>
    <message>
        <source>Take all attributes from the Polygon feature with the largest area</source>
        <translation>取面积最大的面要素的全部属性</translation>
    </message>
    <message>
        <source>Id</source>
        <translation>ID</translation>
    </message>
    <message>
        <source>Merge</source>
        <translation>合并</translation>
    </message>
    <message>
        <source>Feature %1</source>
        <translation>要素 %1</translation>
    </message>
    <message>
        <source>Concatenation</source>
        <translation>拼接</translation>
    </message>
    <message>
        <source>Skip Attribute</source>
        <translation>跳过属性</translation>
    </message>
    <message>
        <source>Manual Value</source>
        <translation>手动赋值</translation>
    </message>
    <message>
        <source>Set to NULL</source>
        <translation>设为 NULL</translation>
    </message>
    <message>
        <source>Skipped</source>
        <translation>已跳过</translation>
    </message>
    <message>
        <source>NULL</source>
        <translation>NULL</translation>
    </message>
</context>
<context>
    <name>QgsOffsetUserWidget</name>
    <message>
        <source>Round</source>
        <translation>圆角</translation>
    </message>
    <message>
        <source>Miter</source>
        <translation>斜接</translation>
    </message>
    <message>
        <source>Bevel</source>
        <translation>斜角</translation>
    </message>
    <message>
        <source>Flat</source>
        <translation>平头</translation>
    </message>
    <message>
        <source>Square</source>
        <translation>方头</translation>
    </message>
</context>
<context>
    <name>QgsSelectByFormDialog</name>
    <message>
        <source>%1 — Select Features</source>
        <translation>%1 — 选择要素</translation>
    </message>
    <message numerus="yes">
        <source>Zoomed to %n matching feature(s)</source>
        <comment>number of matching features</comment>
        <translation><numerusform>已缩放到 %n 个匹配要素</numerusform>
        </translation>
    </message>
    <message>
        <source>No matching features found</source>
        <translation>未找到匹配要素</translation>
    </message>
</context>
<context>
    <name>QgsSimplifyUserInputWidget</name>
    <message>
        <source>Simplify by Distance</source>
        <translation>按距离简化</translation>
    </message>
    <message>
        <source>Simplify by Snapping to Grid</source>
        <translation>按格网吸附简化</translation>
    </message>
    <message>
        <source>Simplify by Area (Visvalingam)</source>
        <translation>按面积简化（Visvalingam）</translation>
    </message>
    <message>
        <source>Smooth</source>
        <translation>平滑</translation>
    </message>
    <message>
        <source>Layer Units</source>
        <translation>图层单位</translation>
    </message>
    <message>
        <source>Pixels</source>
        <translation>像素</translation>
    </message>
    <message>
        <source>Map Units</source>
        <translation>地图单位</translation>
    </message>
</context>
<context>
    <name>QgsSnappingLayerDelegate</name>
    <message>
        <source>Snapping Type</source>
        <translation>捕捉类型</translation>
    </message>
    <message>
        <source>Set Snapping Mode</source>
        <translation>设置捕捉模式</translation>
    </message>
    <message>
        <source>px</source>
        <translation>px</translation>
    </message>
    <message>
        <source>Minimum scale from which snapping is enabled (i.e. most "zoomed out" scale)</source>
        <translation>启用捕捉的最小比例尺（即最“缩小”的比例尺）</translation>
    </message>
    <message>
        <source>Maximum scale up to which snapping is enabled (i.e. most "zoomed in" scale)</source>
        <translation>启用捕捉的最大比例尺（即最“放大”的比例尺）</translation>
    </message>
</context>
<context>
    <name>QgsSnappingLayerTreeModel</name>
    <message>
        <source>Layer</source>
        <translation>图层</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Tolerance</source>
        <translation>容差</translation>
    </message>
    <message>
        <source>Units</source>
        <translation>单位</translation>
    </message>
    <message>
        <source>Avoid Overlap</source>
        <translation>避免重叠</translation>
    </message>
    <message>
        <source>Min Scale</source>
        <translation>最小比例尺</translation>
    </message>
    <message>
        <source>Max Scale</source>
        <translation>最大比例尺</translation>
    </message>
    <message>
        <source>, …</source>
        <translation>、…</translation>
    </message>
    <message>
        <source>, </source>
        <translation>、</translation>
    </message>
    <message>
        <source>pixels</source>
        <translation>像素</translation>
    </message>
    <message>
        <source>not set</source>
        <translation>未设置</translation>
    </message>
</context>
<context>
    <name>QgsSnappingWidget</name>
    <message>
        <source>Filter layers…</source>
        <translation>过滤图层…</translation>
    </message>
    <message>
        <source>Toggle Snapping</source>
        <translation>切换捕捉</translation>
    </message>
    <message>
        <source>Enable Snapping (S)</source>
        <translation>启用捕捉 (S)</translation>
    </message>
    <message>
        <source>S</source>
        <comment>Keyboard shortcut: toggle snapping</comment>
        <translation>S</translation>
    </message>
    <message>
        <source>When avoid overlap is enabled, digitized features will be clipped to not overlapped existing ones.</source>
        <translation>启用避免重叠后，数字化的要素会被裁剪，不与已有要素重叠。</translation>
    </message>
    <message>
        <source>Set Avoid Overlap Mode</source>
        <translation>设置避免重叠模式</translation>
    </message>
    <message>
        <source>Allow Overlap</source>
        <translation>允许重叠</translation>
    </message>
    <message>
        <source>Avoid Overlap on Active Layer</source>
        <translation>活动图层避免重叠</translation>
    </message>
    <message>
        <source>Avoid Overlap on Active Layer.
Beware that this option will be applied on all vertices of the edited geometries, even if outside the current view extent</source>
        <translation>活动图层避免重叠。
注意：该选项作用于编辑几何的所有顶点，即使顶点在当前视图范围之外。</translation>
    </message>
    <message>
        <source>Follow Advanced Configuration</source>
        <translation>遵循高级配置</translation>
    </message>
    <message>
        <source>Snapping Mode</source>
        <translation>捕捉模式</translation>
    </message>
    <message>
        <source>Set Snapping Mode</source>
        <translation>设置捕捉模式</translation>
    </message>
    <message>
        <source>All Layers</source>
        <translation>所有图层</translation>
    </message>
    <message>
        <source>Active Layer</source>
        <translation>活动图层</translation>
    </message>
    <message>
        <source>Advanced Configuration</source>
        <translation>高级配置</translation>
    </message>
    <message>
        <source>Open Snapping Options…</source>
        <translation>打开捕捉选项…</translation>
    </message>
    <message>
        <source>Snapping Type</source>
        <translation>捕捉类型</translation>
    </message>
    <message>
        <source>Snapping Tolerance in Defined Units</source>
        <translation>以设定单位表示的捕捉容差</translation>
    </message>
    <message>
        <source>px</source>
        <translation>px</translation>
    </message>
    <message>
        <source>Snapping Unit Type: Pixels (px) or Project/Map Units (%1)</source>
        <translation>捕捉单位类型：像素 (px) 或工程/地图单位（%1）</translation>
    </message>
    <message>
        <source>Snapping Unit Type: Pixels (px) or Map Units (%1)</source>
        <translation>捕捉单位类型：像素 (px) 或地图单位（%1）</translation>
    </message>
    <message>
        <source>Topological Editing</source>
        <translation>拓扑编辑</translation>
    </message>
    <message>
        <source>Enable Topological Editing</source>
        <translation>启用拓扑编辑</translation>
    </message>
    <message>
        <source>Snapping on Intersection</source>
        <translation>交点捕捉</translation>
    </message>
    <message>
        <source>Enable Snapping on Intersection</source>
        <translation>启用交点捕捉</translation>
    </message>
    <message>
        <source>Enable Tracing</source>
        <translation>启用追踪</translation>
    </message>
    <message>
        <source>Enable Tracing (T)</source>
        <translation>启用追踪 (T)</translation>
    </message>
    <message>
        <source>T</source>
        <comment>Keyboard shortcut: Enable tracing</comment>
        <translation>T</translation>
    </message>
    <message>
        <source>Self-snapping</source>
        <translation>自捕捉</translation>
    </message>
    <message>
        <source>Enable Self-snapping</source>
        <translation>启用自捕捉</translation>
    </message>
    <message>
        <source>If enabled, snapping will also take the current state of the digitized feature into consideration.</source>
        <translation>启用后，捕捉也会考虑正在数字化的要素的当前状态。</translation>
    </message>
    <message>
        <source>Edit advanced configuration</source>
        <translation>编辑高级配置</translation>
    </message>
    <message>
        <source>Minimum scale from which snapping is enabled (i.e. most "zoomed out" scale)</source>
        <translation>启用捕捉的最小比例尺（即最“缩小”的比例尺）</translation>
    </message>
    <message>
        <source>Maximum scale up to which snapping is enabled (i.e. most "zoomed in" scale)</source>
        <translation>启用捕捉的最大比例尺（即最“放大”的比例尺）</translation>
    </message>
    <message>
        <source>Snapping scale mode</source>
        <translation>捕捉比例模式</translation>
    </message>
    <message>
        <source>Set snapping scale mode</source>
        <translation>设置捕捉比例模式</translation>
    </message>
    <message>
        <source>Disabled</source>
        <translation>禁用</translation>
    </message>
    <message>
        <source>Scale dependency disabled</source>
        <translation>未启用比例尺依赖</translation>
    </message>
    <message>
        <source>Global</source>
        <translation>全局</translation>
    </message>
    <message>
        <source>Scale dependency global</source>
        <translation>全局比例尺依赖</translation>
    </message>
    <message>
        <source>Per layer</source>
        <translation>按图层</translation>
    </message>
    <message>
        <source>Scale dependency per layer</source>
        <translation>按图层的比例尺依赖</translation>
    </message>
</context>
<context>
    <name>QgsStreamDigitizingSettingsAction</name>
    <message>
        <source>px</source>
        <translation>px</translation>
    </message>
    <message>
        <source>Streaming Tolerance</source>
        <translation>流式采集容差</translation>
    </message>
</context>
<context>
    <name>QgsVertexEditor</name>
    <message>
        <source>Vertex Editor</source>
        <translation>顶点编辑器</translation>
    </message>
</context>
<context>
    <name>QgsVertexEditorModel</name>
    <message>
        <source>x</source>
        <translation>x</translation>
    </message>
    <message>
        <source>y</source>
        <translation>y</translation>
    </message>
    <message>
        <source>z</source>
        <translation>z</translation>
    </message>
    <message>
        <source>m</source>
        <translation>m</translation>
    </message>
    <message>
        <source>r</source>
        <translation>r</translation>
    </message>
    <message>
        <source>w</source>
        <translation>w</translation>
    </message>
    <message>
        <source>Vertex %1</source>
        <translation>顶点 %1</translation>
    </message>
    <message>
        <source>X Coordinate</source>
        <translation>X 坐标</translation>
    </message>
    <message>
        <source>Y Coordinate</source>
        <translation>Y 坐标</translation>
    </message>
    <message>
        <source>Z Coordinate</source>
        <translation>Z 坐标</translation>
    </message>
    <message>
        <source>M Value</source>
        <translation>M 值</translation>
    </message>
    <message>
        <source>Radius Value</source>
        <translation>半径值</translation>
    </message>
    <message>
        <source>NURBS Weight</source>
        <translation>NURBS 权重</translation>
    </message>
    <message>
        <source>Changed NURBS weight</source>
        <translation>已修改 NURBS 权重</translation>
    </message>
</context>
<context>
    <name>QgsVertexEditorWidget</name>
    <message>
        <source>Vertex Editor</source>
        <translation>顶点编辑器</translation>
    </message>
    <message>
        <source>Right click on an editable feature to show its table of vertices.</source>
        <translation>右键点击可编辑要素以显示其顶点表。</translation>
    </message>
    <message>
        <source>When a feature is bound to this panel, dragging a rectangle to select vertices on the canvas will only select those of the bound feature.</source>
        <translation>当要素绑定到本面板时，在画布上框选顶点只会选中绑定要素的顶点。</translation>
    </message>
    <message>
        <source>Auto-open Table</source>
        <translation>自动打开顶点表</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>选项</translation>
    </message>
</context>
<context>
    <name>QgsVertexTool</name>
    <message>
        <source>Invisible vertices were not selected</source>
        <translation>不可见顶点未被选中</translation>
    </message>
    <message>
        <source>Vertices belonging to features that are not displayed on the map canvas were not selected.</source>
        <translation>未显示在地图画布上的要素的顶点未被选中。</translation>
    </message>
    <message>
        <source>Topological points added by 'Vertex Tool'</source>
        <translation>“顶点工具”添加的拓扑点</translation>
    </message>
    <message>
        <source>Moved vertex</source>
        <translation>已移动顶点</translation>
    </message>
    <message>
        <source>Deleted vertex</source>
        <translation>已删除顶点</translation>
    </message>
    <message>
        <source>Geometry has been cleared. Use the add part tool to set geometry for this feature.</source>
        <translation>几何已被清空。请使用添加部件工具为该要素设置几何。</translation>
    </message>
    <message>
        <source>Could not convert vertex</source>
        <translation>无法转换顶点</translation>
    </message>
    <message>
        <source>Conversion can only be done on exactly one vertex.</source>
        <translation>一次只能转换一个顶点。</translation>
    </message>
    <message>
        <source>Cannot convert vertex before it is added.</source>
        <translation>顶点添加之前无法转换。</translation>
    </message>
    <message>
        <source>Layer of type %1 does not support curved geometries.</source>
        <translation>类型为 %1 的图层不支持曲线几何。</translation>
    </message>
    <message>
        <source>Toggled vertex to/from curve</source>
        <translation>顶点与曲线互转完成</translation>
    </message>
    <message>
        <source>Start/end of vertices of features and arcs can not be converted.</source>
        <translation>要素和弧段的起止顶点无法转换。</translation>
    </message>
    <message numerus="yes">
        <source>Validation finished (%n error(s) found).</source>
        <comment>number of geometry errors</comment>
        <translation><numerusform>校验完成（发现 %n 个错误）。</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>RadiometricCalibrationDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layer for radiometric calibration.</source>
        <translation>选择待执行辐射定标的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Calibration Parameters</source>
        <translation>定标参数</translation>
    </message>
    <message>
        <source>Radiance</source>
        <translation>辐射亮度 (Radiance)</translation>
    </message>
    <message>
        <source>TOA Apparent Reflectance</source>
        <translation>TOA 表观反射率</translation>
    </message>
    <message>
        <source>Brightness Temperature (K)</source>
        <translation>亮温 (K)</translation>
    </message>
    <message>
        <source>• Radiance: L = gain×DN + bias
• TOA reflectance: Landsat (reflMult×DN+add)/sin(sun); S2 (DN+offset)/scale
• Brightness temperature: needs the thermal band K1/K2 constants</source>
        <translation>• 辐射亮度：L = gain×DN + bias
• TOA 反射率：Landsat (reflMult×DN+add)/sin(sun)；S2 (DN+offset)/scale
• 亮温：需热红外波段 K1/K2 常数</translation>
    </message>
    <message>
        <source>Output Physical Quantity</source>
        <translation>输出物理量</translation>
    </message>
    <message>
        <source>Process All Valid Bands</source>
        <translation>处理全部有效波段</translation>
    </message>
    <message>
        <source>When ticked, all valid bands of the input image are calibrated automatically; untick to calibrate a single target band.</source>
        <translation>勾选时自动对输入影像的所有有效波段执行定标；取消勾选可指定单个目标波段。</translation>
    </message>
    <message>
        <source>Chooses the single target band number to calibrate.</source>
        <translation>选择待定标的单一目标波段号。</translation>
    </message>
    <message>
        <source>Target Band</source>
        <translation>目标波段</translation>
    </message>
    <message>
        <source>Auto-detect (*_MTL.txt / MTD_MSI*.xml next to the input raster)</source>
        <translation>自动探测（输入栅格旁 *_MTL.txt / MTD_MSI*.xml）</translation>
    </message>
    <message>
        <source>Path to a Landsat *_MTL.txt or Sentinel-2 MTD_MSI*.xml; auto-detected if left empty.</source>
        <translation>Landsat *_MTL.txt 或 Sentinel-2 MTD_MSI*.xml 路径；留空则自动探测。</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the sensor metadata file</source>
        <translation>浏览并指定传感器元数据文件</translation>
    </message>
    <message>
        <source>Metadata File</source>
        <translation>元数据文件</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>Select Sensor Metadata File</source>
        <translation>选择传感器元数据文件</translation>
    </message>
    <message>
        <source>Landsat MTL (*_MTL.txt);;Sentinel-2 MTD (MTD_MSI*.xml);;All Files (*)</source>
        <translation>Landsat MTL (*_MTL.txt);;Sentinel-2 MTD (MTD_MSI*.xml);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Sensor metadata file not found; falling back to the raster's embedded GDAL scale/offset.</source>
        <translation>未找到传感器元数据文件；将回退到栅格内嵌 GDAL scale/offset。</translation>
    </message>
    <message>
        <source>Detected %1, but parsing failed: %2</source>
        <translation>已探测到 %1，但解析失败：%2</translation>
    </message>
    <message>
        <source>%1 bands</source>
        <translation>%1 个波段</translation>
    </message>
    <message>
        <source>Platform %1</source>
        <translation>平台 %1</translation>
    </message>
    <message>
        <source>Level %1</source>
        <translation>级别 %1</translation>
    </message>
    <message>
        <source>Sun elevation %1°</source>
        <translation>太阳高度 %1°</translation>
    </message>
    <message>
        <source>Using %1: %2.</source>
        <translation>使用 %1：%2。</translation>
    </message>
    <message>
        <source>Select a valid raster layer first.</source>
        <translation>请先选择一个有效的栅格图层。</translation>
    </message>
    <message>
        <source>Radiometric Calibration</source>
        <translation>辐射定标</translation>
    </message>
</context>
<context>
    <name>RasterProcessingDialogBase</name>
    <message>
        <source>Specify the output file path.</source>
        <translation>请指定输出文件路径。</translation>
    </message>
    <message>
        <source>No valid raster layer selected.</source>
        <translation>未选择有效的栅格图层。</translation>
    </message>
    <message>
        <source>Pipeline</source>
        <translation>流程</translation>
    </message>
    <message>
        <source>Brief overview of this tool. Press 'Help' for parameter explanations.</source>
        <translation>本工具功能简介。点「帮助」查看参数说明。</translation>
    </message>
    <message>
        <source>Parameter Description</source>
        <translation>参数说明</translation>
    </message>
    <message>
        <source>Opens the documentation for this feature.</source>
        <translation>打开本功能的说明文档。</translation>
    </message>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Algorithm Parameters</source>
        <translation>算法参数</translation>
    </message>
    <message>
        <source>Advanced Options</source>
        <translation>高级选项</translation>
    </message>
    <message>
        <source>Output Settings</source>
        <translation>输出配置</translation>
    </message>
    <message>
        <source>Result save path and output options. Required before running; the .tif extension is recommended.</source>
        <translation>结果保存路径与输出选项。运行前必须填写；建议使用 .tif 扩展名。</translation>
    </message>
    <message>
        <source>Choose or enter the output GeoTIFF file path (*.tif)...</source>
        <translation>选择或输入输出 GeoTIFF 文件路径 (*.tif)...</translation>
    </message>
    <message>
        <source>Result save path; the .tif extension is recommended.</source>
        <translation>结果保存路径。建议使用 .tif 扩展名。</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the output file location.</source>
        <translation>浏览选择输出文件位置。</translation>
    </message>
    <message>
        <source>Output Path</source>
        <translation>输出路径</translation>
    </message>
    <message>
        <source>Output GeoTIFF path. Required before running.</source>
        <translation>输出 GeoTIFF 路径。运行前必须填写。</translation>
    </message>
    <message>
        <source>Tip: after processing, results can be loaded from the log or project layers.</source>
        <translation>提示：处理完成后可从日志或工程图层中加载结果。</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>View the documentation and usage tips for this feature.</source>
        <translation>查看本功能的说明文档与使用提示。</translation>
    </message>
    <message>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <source>Restores all parameters to their defaults.</source>
        <translation>恢复所有参数到默认初始状态。</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Cancels the task while running; otherwise closes the dialog.</source>
        <translation>任务运行中取消任务，否则关闭对话框。</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Validates the inputs and starts processing. Do not close the dialog while running.</source>
        <translation>校验输入后开始处理。运行中请勿关闭对话框。</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>取消中…</translation>
    </message>
    <message>
        <source>Select Output File</source>
        <translation>选择输出文件</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif *.tiff);;All Files (*)</source>
        <translation>GeoTIFF (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Running...</source>
        <translation>正在运行…</translation>
    </message>
    <message>
        <source>%1 finished. Output: %2</source>
        <translation>%1 完成。输出：%2</translation>
    </message>
    <message>
        <source>%1 results are shown in the dialog; close it manually when done.</source>
        <translation>%1 结果已在对话框中展示，请查看后手动关闭。</translation>
    </message>
    <message>
        <source>Processing failed; see the log panel for details.</source>
        <translation>处理失败，详见日志面板。</translation>
    </message>
</context>
<context>
    <name>ResolutionWidget</name>
    <message>
        <source>Resolution Mode:</source>
        <translation>分辨率模式：</translation>
    </message>
    <message>
        <source>Fixed Target Resolution</source>
        <translation>固定目标分辨率</translation>
    </message>
    <message>
        <source>Scale Factor Multiplier</source>
        <translation>比例系数倍率</translation>
    </message>
    <message>
        <source>Match Reference Layer Grid</source>
        <translation>匹配参考图层网格</translation>
    </message>
    <message>
        <source>X:</source>
        <translation>X：</translation>
    </message>
    <message>
        <source>Y:</source>
        <translation>Y：</translation>
    </message>
    <message>
        <source>Pixel Size (Map Units):</source>
        <translation>像元大小（地图单位）：</translation>
    </message>
    <message>
        <source>Scale Multiplier (e.g. 0.5x, 2.0x):</source>
        <translation>比例倍率（如 0.5x、2.0x）：</translation>
    </message>
    <message>
        <source>Reference Layer:</source>
        <translation>参考图层：</translation>
    </message>
    <message>
        <source>Physical target resolution must be strictly greater than zero.</source>
        <translation>物理目标分辨率必须严格大于零。</translation>
    </message>
    <message>
        <source>Scale factor multiplier must be strictly greater than zero.</source>
        <translation>比例系数倍率必须严格大于零。</translation>
    </message>
    <message>
        <source>A valid reference raster layer must be selected.</source>
        <translation>必须选择有效的参考栅格图层。</translation>
    </message>
</context>
<context>
    <name>RibbonController</name>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
    <message>
        <source>New Project</source>
        <translation>新建工程</translation>
    </message>
    <message>
        <source>Open Project</source>
        <translation>打开工程</translation>
    </message>
    <message>
        <source>Save Project</source>
        <translation>保存工程</translation>
    </message>
    <message>
        <source>Preferences</source>
        <translation>偏好设置</translation>
    </message>
    <message>
        <source>Project</source>
        <translation>工程</translation>
    </message>
    <message>
        <source>New</source>
        <translation>新建</translation>
    </message>
    <message>
        <source>New Empty Project</source>
        <translation>新建空白工程</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>打开</translation>
    </message>
    <message>
        <source>Open Project File</source>
        <translation>打开工程文件</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <source>Data Entry</source>
        <translation>数据入口</translation>
    </message>
    <message>
        <source>Import</source>
        <translation>导入</translation>
    </message>
    <message>
        <source>Import Layer</source>
        <translation>导入图层</translation>
    </message>
    <message>
        <source>Example</source>
        <translation>示例</translation>
    </message>
    <message>
        <source>Load Teaching Sample Data</source>
        <translation>加载教学示例数据</translation>
    </message>
    <message>
        <source>Project - Project file operations (new/open/save, import, preferences)</source>
        <translation>工程 - 工程文件操作（新建/打开/保存、导入、偏好）</translation>
    </message>
    <message>
        <source>History</source>
        <translation>历史</translation>
    </message>
    <message>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <source>Undo (Ctrl+Z)</source>
        <translation>撤销上一步 (Ctrl+Z)</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
    <message>
        <source>Redo (Ctrl+Y)</source>
        <translation>重做 (Ctrl+Y)</translation>
    </message>
    <message>
        <source>Clipboard</source>
        <translation>剪贴板</translation>
    </message>
    <message>
        <source>Cut</source>
        <translation>剪切</translation>
    </message>
    <message>
        <source>Cut Selected Features</source>
        <translation>剪切选中要素</translation>
    </message>
    <message>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <source>Copy Selected Features</source>
        <translation>复制选中要素</translation>
    </message>
    <message>
        <source>Paste</source>
        <translation>粘贴</translation>
    </message>
    <message>
        <source>Paste Features</source>
        <translation>粘贴要素</translation>
    </message>
    <message>
        <source>Select</source>
        <translation>选择</translation>
    </message>
    <message>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
    <message>
        <source>Select All Features of the Current Layer</source>
        <translation>选择当前图层全部要素</translation>
    </message>
    <message>
        <source>Select Features by Rectangle</source>
        <translation>矩形选择要素</translation>
    </message>
    <message>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <source>Delete Selected Features</source>
        <translation>删除选中要素</translation>
    </message>
    <message>
        <source>Attribute Table</source>
        <translation>属性表</translation>
    </message>
    <message>
        <source>Open Attribute Table</source>
        <translation>打开属性表</translation>
    </message>
    <message>
        <source>Edit</source>
        <translation>编辑</translation>
    </message>
    <message>
        <source>Edit - Feature Editing and Digitizing Tools</source>
        <translation>编辑 - 要素编辑与数字化工具</translation>
    </message>
    <message>
        <source>Session</source>
        <translation>会话</translation>
    </message>
    <message>
        <source>Start Editing</source>
        <translation>开始编辑</translation>
    </message>
    <message>
        <source>Toggle the vector layer editing session</source>
        <translation>切换矢量图层编辑会话</translation>
    </message>
    <message>
        <source>Save Edits</source>
        <translation>保存编辑</translation>
    </message>
    <message>
        <source>Save Vector Edits to Data Source</source>
        <translation>保存矢量编辑到数据源</translation>
    </message>
    <message>
        <source>Features</source>
        <translation>要素</translation>
    </message>
    <message>
        <source>Select Features</source>
        <translation>选择要素</translation>
    </message>
    <message>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <source>Add Feature</source>
        <translation>添加要素</translation>
    </message>
    <message>
        <source>Node</source>
        <translation>节点</translation>
    </message>
    <message>
        <source>Node Tool</source>
        <translation>节点工具</translation>
    </message>
    <message>
        <source>Modify</source>
        <translation>修改</translation>
    </message>
    <message>
        <source>Move</source>
        <translation>移动</translation>
    </message>
    <message>
        <source>Move Features</source>
        <translation>移动要素</translation>
    </message>
    <message>
        <source>Rotate</source>
        <translation>旋转</translation>
    </message>
    <message>
        <source>Rotate Features</source>
        <translation>旋转要素</translation>
    </message>
    <message>
        <source>Reshape</source>
        <translation>整形</translation>
    </message>
    <message>
        <source>Segmentation</source>
        <translation>分割</translation>
    </message>
    <message>
        <source>Split Features</source>
        <translation>分割要素</translation>
    </message>
    <message>
        <source>Advanced</source>
        <translation>高级</translation>
    </message>
    <message>
        <source>Offset</source>
        <translation>偏移</translation>
    </message>
    <message>
        <source>Offset Curve</source>
        <translation>偏移曲线</translation>
    </message>
    <message>
        <source>Simplify</source>
        <translation>简化</translation>
    </message>
    <message>
        <source>Simplify Features</source>
        <translation>简化要素</translation>
    </message>
    <message>
        <source>Reverse</source>
        <translation>反向</translation>
    </message>
    <message>
        <source>Reverse Line</source>
        <translation>线反向</translation>
    </message>
    <message>
        <source>Add Ring</source>
        <translation>挖环</translation>
    </message>
    <message>
        <source>Fill Ring</source>
        <translation>填充环</translation>
    </message>
    <message>
        <source>Delete Part</source>
        <translation>删部件</translation>
    </message>
    <message>
        <source>Vector Editing</source>
        <translation>矢量编辑</translation>
    </message>
    <message>
        <source>Vector Editing - Vector Data Processing and Geometry Operations</source>
        <translation>矢量编辑 - 矢量数据处理与几何操作</translation>
    </message>
    <message>
        <source>Navigation</source>
        <translation>导航</translation>
    </message>
    <message>
        <source>Query</source>
        <translation>查询</translation>
    </message>
    <message>
        <source>Band Composition</source>
        <translation>波段合成</translation>
    </message>
    <message>
        <source>Mode</source>
        <translation>模式</translation>
    </message>
    <message>
        <source>RGB true color or single-band grayscale display</source>
        <translation>RGB 真彩色 或 灰度单波段显示</translation>
    </message>
    <message>
        <source>RGB true color</source>
        <translation>RGB 真彩色</translation>
    </message>
    <message>
        <source>Grayscale</source>
        <translation>灰度</translation>
    </message>
    <message>
        <source>Red R</source>
        <translation>红 R</translation>
    </message>
    <message>
        <source>Band used by the red channel</source>
        <translation>红色通道使用的波段</translation>
    </message>
    <message>
        <source>Green G</source>
        <translation>绿 G</translation>
    </message>
    <message>
        <source>Band used by the green channel</source>
        <translation>绿色通道使用的波段</translation>
    </message>
    <message>
        <source>Blue B</source>
        <translation>蓝 B</translation>
    </message>
    <message>
        <source>Band used by the blue channel</source>
        <translation>蓝色通道使用的波段</translation>
    </message>
    <message>
        <source>Band used for grayscale display</source>
        <translation>灰度显示使用的波段</translation>
    </message>
    <message>
        <source>Appearance</source>
        <translation>外观</translation>
    </message>
    <message>
        <source>Map</source>
        <translation>地图</translation>
    </message>
    <message>
        <source>Map - View Navigation, Layer Management and Identify Tools</source>
        <translation>地图 - 视图导航、图层管理与识别工具</translation>
    </message>
    <message>
        <source>Data Catalog</source>
        <translation>数据目录</translation>
    </message>
    <message>
        <source>Data Management</source>
        <translation>数据管理</translation>
    </message>
    <message>
        <source>Open the Data Asset Catalog (Data Manager)</source>
        <translation>打开数据资产目录（Data Manager）</translation>
    </message>
    <message>
        <source>Add Layer</source>
        <translation>添加图层</translation>
    </message>
    <message>
        <source>Raster</source>
        <translation>栅格</translation>
    </message>
    <message>
        <source>Add Raster Layer</source>
        <translation>添加栅格图层</translation>
    </message>
    <message>
        <source>Vector</source>
        <translation>矢量</translation>
    </message>
    <message>
        <source>Add Vector Layer</source>
        <translation>添加矢量图层</translation>
    </message>
    <message>
        <source>STAC</source>
        <translation>STAC</translation>
    </message>
    <message>
        <source>STAC Catalog Search</source>
        <translation>STAC 目录检索</translation>
    </message>
    <message>
        <source>Registration</source>
        <translation>配准</translation>
    </message>
    <message>
        <source>Image Registration</source>
        <translation>影像配准</translation>
    </message>
    <message>
        <source>Image to Image</source>
        <translation>影像对影像</translation>
    </message>
    <message>
        <source>On-Map Registration</source>
        <translation>图上配准</translation>
    </message>
    <message>
        <source>Image to Map</source>
        <translation>影像对地图</translation>
    </message>
    <message>
        <source>Data</source>
        <translation>数据</translation>
    </message>
    <message>
        <source>Data - Data Asset Catalog and Data Management</source>
        <translation>数据 - 数据资产目录与数据管理</translation>
    </message>
    <message>
        <source>Preprocessing</source>
        <translation>预处理</translation>
    </message>
    <message>
        <source>Atmospheric Correction</source>
        <translation>大气校正</translation>
    </message>
    <message>
        <source>Atmospheric / Radiometric Correction</source>
        <translation>大气 / 辐射校正</translation>
    </message>
    <message>
        <source>Image Fusion</source>
        <translation>影像融合</translation>
    </message>
    <message>
        <source>Panchromatic + multispectral fusion</source>
        <translation>全色 + 多光谱融合</translation>
    </message>
    <message>
        <source>Mosaic</source>
        <translation>镶嵌</translation>
    </message>
    <message>
        <source>Multi-Scene Mosaic</source>
        <translation>多景镶嵌</translation>
    </message>
    <message>
        <source>Speckle Filtering</source>
        <translation>斑点滤波</translation>
    </message>
    <message>
        <source>SAR Speckle Filtering</source>
        <translation>SAR 斑点滤波</translation>
    </message>
    <message>
        <source>Preprocessing - Radiometric/Atmospheric Correction, Registration, Fusion, Clipping</source>
        <translation>预处理 - 辐射/大气校正、配准、融合、裁剪</translation>
    </message>
    <message>
        <source>Display Adjustment</source>
        <translation>显示调整</translation>
    </message>
    <message>
        <source>Brightness</source>
        <translation>亮度</translation>
    </message>
    <message>
        <source>Current raster display brightness</source>
        <translation>当前栅格显示亮度</translation>
    </message>
    <message>
        <source>Contrast</source>
        <translation>对比度</translation>
    </message>
    <message>
        <source>Current raster display contrast</source>
        <translation>当前栅格显示对比度</translation>
    </message>
    <message>
        <source>Stretch and Filtering</source>
        <translation>拉伸与滤波</translation>
    </message>
    <message>
        <source>Display Stretch</source>
        <translation>显示拉伸</translation>
    </message>
    <message>
        <source>Changes display contrast only; no file is written</source>
        <translation>仅改显示对比度，不写出文件</translation>
    </message>
    <message>
        <source>Write Contrast</source>
        <translation>对比度写出</translation>
    </message>
    <message>
        <source>Stretch and Export GeoTIFF</source>
        <translation>拉伸并导出 GeoTIFF</translation>
    </message>
    <message>
        <source>Spatial Filtering</source>
        <translation>空间滤波</translation>
    </message>
    <message>
        <source>Smoothing / Sharpening</source>
        <translation>平滑 / 锐化</translation>
    </message>
    <message>
        <source>Enhancement Panel</source>
        <translation>增强面板</translation>
    </message>
    <message>
        <source>Combined Image Enhancement Panel</source>
        <translation>影像增强综合面板</translation>
    </message>
    <message>
        <source>Enhancement</source>
        <translation>增强</translation>
    </message>
    <message>
        <source>Enhancement - Image Enhancement, Stretch, Filtering, Band Math</source>
        <translation>增强 - 影像增强、拉伸、滤波、波段运算</translation>
    </message>
    <message>
        <source>Spectrum</source>
        <translation>光谱</translation>
    </message>
    <message>
        <source>Spectral Indices</source>
        <translation>光谱指数</translation>
    </message>
    <message>
        <source>NDVI / EVI, etc.</source>
        <translation>NDVI / EVI 等</translation>
    </message>
    <message>
        <source>Band Math</source>
        <translation>波段运算</translation>
    </message>
    <message>
        <source>Band Expression</source>
        <translation>波段表达式</translation>
    </message>
    <message>
        <source>Principal Component</source>
        <translation>主成分</translation>
    </message>
    <message>
        <source>PCA</source>
        <translation>PCA</translation>
    </message>
    <message>
        <source>Spatial Analysis</source>
        <translation>空间分析</translation>
    </message>
    <message>
        <source>Change Detection</source>
        <translation>变化检测</translation>
    </message>
    <message>
        <source>Two-Date Change</source>
        <translation>两期变化</translation>
    </message>
    <message>
        <source>Terrain</source>
        <translation>地形</translation>
    </message>
    <message>
        <source>Slope / Aspect</source>
        <translation>坡度 / 坡向</translation>
    </message>
    <message>
        <source>Analysis</source>
        <translation>分析</translation>
    </message>
    <message>
        <source>Analysis - Spectral Indices, Change Detection, Terrain, Classification</source>
        <translation>分析 - 光谱指数、变化检测、地形、分类</translation>
    </message>
    <message>
        <source>Classification</source>
        <translation>分类</translation>
    </message>
    <message>
        <source>Supervised Classification</source>
        <translation>监督分类</translation>
    </message>
    <message>
        <source>Pixel-Level Supervised Classification</source>
        <translation>像元级监督分类</translation>
    </message>
    <message>
        <source>Object Classification</source>
        <translation>对象分类</translation>
    </message>
    <message>
        <source>Object-Based (OBIA)</source>
        <translation>面向对象 OBIA</translation>
    </message>
    <message>
        <source>Classification - Supervised/Unsupervised and Accuracy Assessment</source>
        <translation>分类 - 监督/非监督分类与精度评价</translation>
    </message>
    <message>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>Print Layout</source>
        <translation>打印布局</translation>
    </message>
    <message>
        <source>New Print Layout</source>
        <translation>新建打印布局</translation>
    </message>
    <message>
        <source>Swipe Comparison</source>
        <translation>卷帘对比</translation>
    </message>
    <message>
        <source>Swipe Comparison Layer</source>
        <translation>卷帘对比图层</translation>
    </message>
    <message>
        <source>Cartography</source>
        <translation>制图</translation>
    </message>
    <message>
        <source>Cartography - Layout Design and Map Output</source>
        <translation>制图 - 布局设计与制图输出</translation>
    </message>
    <message>
        <source>Jobs</source>
        <translation>作业</translation>
    </message>
    <message>
        <source>Task Center</source>
        <translation>任务中心</translation>
    </message>
    <message>
        <source>Queue, Progress and Log</source>
        <translation>队列、进度与日志</translation>
    </message>
    <message>
        <source>Processing History</source>
        <translation>处理历史</translation>
    </message>
    <message>
        <source>Toolbox</source>
        <translation>工具箱</translation>
    </message>
    <message>
        <source>Processing Toolbox</source>
        <translation>处理工具箱</translation>
    </message>
    <message>
        <source>Tasks</source>
        <translation>任务</translation>
    </message>
    <message>
        <source>Tasks - Task Center, Processing History and Batch</source>
        <translation>任务 - 任务中心、处理历史与批量</translation>
    </message>
    <message>
        <source>Pipeline Editing</source>
        <translation>流程编辑</translation>
    </message>
    <message>
        <source>New Pipeline</source>
        <translation>新建流程</translation>
    </message>
    <message>
        <source>New empty workflow canvas</source>
        <translation>新建空白工作流画布</translation>
    </message>
    <message>
        <source>Open Pipeline</source>
        <translation>打开流程</translation>
    </message>
    <message>
        <source>Open Workflow JSON Definition</source>
        <translation>打开工作流 JSON 定义</translation>
    </message>
    <message>
        <source>Pipeline Editor</source>
        <translation>流程编辑器</translation>
    </message>
    <message>
        <source>Show/hide the workflow graphical editor</source>
        <translation>显示/隐藏工作流图形编辑器</translation>
    </message>
    <message>
        <source>Pipeline Control</source>
        <translation>流程控制</translation>
    </message>
    <message>
        <source>Run Full Pipeline</source>
        <translation>运行全流程</translation>
    </message>
    <message>
        <source>Runs the whole pipeline in DAG topological order</source>
        <translation>按 DAG 拓扑顺序顺序执行全流程</translation>
    </message>
    <message>
        <source>Stop Run</source>
        <translation>停止运行</translation>
    </message>
    <message>
        <source>Stop the currently running workflow</source>
        <translation>停止当前正在运行的工作流</translation>
    </message>
    <message>
        <source>Pipeline</source>
        <translation>流程</translation>
    </message>
    <message>
        <source>Pipeline - Workflow Editor and Model Building</source>
        <translation>流程 - 工作流编辑器与模型构建</translation>
    </message>
    <message>
        <source>SICNU GEO RS</source>
        <translation>SICNU GEO RS</translation>
    </message>
    <message>
        <source>?</source>
        <translation>?</translation>
    </message>
    <message>
        <source>Help content (opens the help document)</source>
        <translation>帮助内容（打开帮助文档）</translation>
    </message>
    <message>
        <source>Open Help Document</source>
        <translation>打开帮助文档</translation>
    </message>
    <message>
        <source>Help content (opens the help document). Press Shift+F1 and click any widget to see its explanation.</source>
        <translation>帮助内容（打开帮助文档）。按 Shift+F1 后点击任意控件可查看该控件的说明。</translation>
    </message>
    <message>
        <source>Collapse Ribbon (Ctrl+F1)</source>
        <translation>收起功能区 (Ctrl+F1)</translation>
    </message>
    <message>
        <source>Expand Ribbon (Ctrl+F1)</source>
        <translation>展开功能区 (Ctrl+F1)</translation>
    </message>
</context>
<context>
    <name>RoiStatisticsWidget</name>
    <message>
        <source>Select a raster layer and ROI to compute statistics.</source>
        <translation>选择栅格图层与 ROI 以计算统计。</translation>
    </message>
    <message>
        <source>Per-band statistics inside the ROI (min/max/mean/stddev/pixel count).</source>
        <translation>ROI 区域内各波段的统计（最小/最大/均值/标准差/像元数）。</translation>
    </message>
    <message>
        <source>Band</source>
        <translation>波段</translation>
    </message>
    <message>
        <source>Min</source>
        <translation>最小值</translation>
    </message>
    <message>
        <source>Max</source>
        <translation>最大值</translation>
    </message>
    <message>
        <source>Mean</source>
        <translation>均值</translation>
    </message>
    <message>
        <source>StdDev</source>
        <translation>标准差</translation>
    </message>
    <message>
        <source>Pixels</source>
        <translation>像素</translation>
    </message>
    <message>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <source>Recomputes statistics for the current ROI.</source>
        <translation>重新计算当前 ROI 的统计。</translation>
    </message>
    <message>
        <source>Export CSV</source>
        <translation>导出 CSV</translation>
    </message>
    <message>
        <source>Exports the statistics table as a CSV file.</source>
        <translation>把统计表导出为 CSV 文件。</translation>
    </message>
    <message>
        <source>Export Statistics</source>
        <translation>导出统计</translation>
    </message>
    <message>
        <source>CSV (*.csv)</source>
        <translation>CSV (*.csv)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Cannot open file for writing.</source>
        <translation>无法打开文件进行写入。</translation>
    </message>
    <message>
        <source>Export</source>
        <translation>导出</translation>
    </message>
    <message>
        <source>Statistics exported to %1</source>
        <translation>统计已导出到 %1</translation>
    </message>
    <message>
        <source>No raster layer selected.</source>
        <translation>未选择栅格图层。</translation>
    </message>
    <message>
        <source>Computing…</source>
        <translation>计算中…</translation>
    </message>
    <message>
        <source>Cannot open raster.</source>
        <translation>无法打开栅格。</translation>
    </message>
    <message>
        <source>ROI outside raster extent.</source>
        <translation>ROI 超出栅格范围。</translation>
    </message>
    <message>
        <source>Statistics computed for %1 bands, %2 pixels</source>
        <translation>已完成 %1 个波段、%2 个像元的统计</translation>
    </message>
    <message>
        <source>Band %1</source>
        <translation>波段 %1</translation>
    </message>
</context>
<context>
    <name>RsAccuracyDialog</name>
    <message>
        <source>Accuracy Assessment</source>
        <translation>精度评价</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>View accuracy metric explanations (OA, Kappa, confusion matrix, etc.).</source>
        <translation>查看精度指标说明（OA、Kappa、混淆矩阵等）。</translation>
    </message>
</context>
<context>
    <name>RsAccuracyPanel</name>
    <message>
        <source>Accuracy Assessment</source>
        <translation>精度评价</translation>
    </message>
    <message>
        <source>After full-image classification (with validation / holdout accuracy), shows OA, Kappa and the confusion matrix here.</source>
        <translation>完成全图分类（含验证/holdout 精度）后在此显示 OA、Kappa 与混淆矩阵。</translation>
    </message>
    <message>
        <source>Overall accuracy OA, Kappa, confusion matrix (rows = truth, columns = prediction), producer's / user's accuracy and F1.</source>
        <translation>总体精度 OA、Kappa、混淆矩阵（行=真实，列=预测）、制图/用户精度与 F1。</translation>
    </message>
    <message>
        <source>Confusion matrix (rows = truth, columns = prediction)</source>
        <translation>混淆矩阵（行=真实，列=预测）</translation>
    </message>
    <message>
        <source>Confusion matrix: rows = true classes, columns = predicted classes; the diagonal holds correctly classified sample counts.</source>
        <translation>混淆矩阵：行=真实类别，列=预测类别。对角线上为正确分类样本数。</translation>
    </message>
    <message>
        <source>Per-Class Metrics</source>
        <translation>分类别指标</translation>
    </message>
    <message>
        <source>Class</source>
        <translation>类别</translation>
    </message>
    <message>
        <source>Producer's Accuracy</source>
        <translation>制图精度</translation>
    </message>
    <message>
        <source>User's Accuracy</source>
        <translation>用户精度</translation>
    </message>
    <message>
        <source>F1</source>
        <translation>F1</translation>
    </message>
    <message>
        <source>Producer's accuracy ≈ recall; user's accuracy ≈ precision; F1 is their harmonic mean.</source>
        <translation>制图精度≈召回率；用户精度≈精确率；F1 为二者调和平均。</translation>
    </message>
    <message>
        <source>Export CSV...</source>
        <translation>导出 CSV…</translation>
    </message>
    <message>
        <source>Exports the accuracy table as a CSV report.</source>
        <translation>将精度表导出为 CSV 报告。</translation>
    </message>
    <message>
        <source>Overall accuracy: %1%   Kappa: %2</source>
        <translation>总体精度: %1%   Kappa: %2</translation>
    </message>
    <message>
        <source>Export Accuracy Report (CSV)</source>
        <translation>导出精度报告 (CSV)</translation>
    </message>
    <message>
        <source>CSV files (*.csv)</source>
        <translation>CSV 文件 (*.csv)</translation>
    </message>
    <message>
        <source>Export Failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <source>Cannot write file: %1</source>
        <translation>无法写入文件: %1</translation>
    </message>
</context>
<context>
    <name>RsClassQuickList</name>
    <message>
        <source>Class quick list: click to select the current class for collecting ROIs or assigning labels.</source>
        <translation>类别快览：点击选中当前类别，用于采集 ROI 或赋标签。</translation>
    </message>
    <message>
        <source>Select Current Class</source>
        <translation>选择当前类别</translation>
    </message>
</context>
<context>
    <name>RsClassTableWidget</name>
    <message>
        <source>Color</source>
        <translation>色</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>ROI</source>
        <translation>ROI</translation>
    </message>
    <message>
        <source>Pixel</source>
        <translation>像元</translation>
    </message>
    <message>
        <source>Class table: double-click to edit a name; columns show color, name, ROI count and pixel count.</source>
        <translation>类别表：双击编辑名称；列显示颜色、名称、ROI 数与像元数。</translation>
    </message>
    <message>
        <source>Class Definition Table</source>
        <translation>类别定义表</translation>
    </message>
    <message>
        <source>Choose Class Color</source>
        <translation>选择类别颜色</translation>
    </message>
</context>
<context>
    <name>RsClassifierLoadDialog</name>
    <message>
        <source>Load Classifier Model</source>
        <translation>加载分类器模型</translation>
    </message>
    <message>
        <source>Choose the algorithm type and specify the trained OpenCV model file (.yml).</source>
        <translation>选择算法类型并指定已训练的 OpenCV 模型文件 (.yml)。</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <source>Normal Bayes (maximum likelihood)</source>
        <translation>NormalBayes（最大似然）</translation>
    </message>
    <message>
        <source>Normal Bayes classifier: a maximum-likelihood model assuming multivariate normal class distributions</source>
        <translation>正态贝叶斯分类器：假定各类样本服从多元正态分布的最大似然模型</translation>
    </message>
    <message>
        <source>SVM (RBF kernel)</source>
        <translation>SVM（RBF 核）</translation>
    </message>
    <message>
        <source>Support vector machine classifier: uses the radial basis kernel (RBF) for non-linearly separable land covers</source>
        <translation>支持向量机分类器：采用径向基核函数 (RBF) 处理非线性可分地物</translation>
    </message>
    <message>
        <source>Model file path (.yml)</source>
        <translation>模型文件路径 (.yml)</translation>
    </message>
    <message>
        <source>Path of the saved OpenCV trained model parameter file (*.yml)</source>
        <translation>已保存的 OpenCV 训练模型参数文件路径 (*.yml)</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the classifier model file</source>
        <translation>浏览并选择分类器模型文件</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens the help for this dialog.</source>
        <translation>打开本对话框的帮助说明。</translation>
    </message>
    <message>
        <source>OpenCV YAML (*.yml *.yaml *.xml);;All Files (*)</source>
        <translation>OpenCV YAML (*.yml *.yaml *.xml);;所有文件 (*)</translation>
    </message>
</context>
<context>
    <name>RsClassifierSetupBar</name>
    <message>
        <source>Classifier Setup Bar</source>
        <translation>分类设置栏</translation>
    </message>
    <message>
        <source>Workflow: pick an algorithm → set bands / training ratio → collect ROIs → preview or train the classification → accuracy assessment</source>
        <translation>流程：选算法 → 设波段/训练比例 → 采集 ROI → 预览或训练分类 → 精度评价</translation>
    </message>
    <message>
        <source>NormalBayes</source>
        <translation>正态贝叶斯</translation>
    </message>
    <message>
        <source>SVM (RBF)</source>
        <translation>SVM（RBF 核）</translation>
    </message>
    <message>
        <source>K-Means</source>
        <translation>K 均值</translation>
    </message>
    <message>
        <source>Random Forest</source>
        <translation>随机森林</translation>
    </message>
    <message>
        <source>Mahalanobis</source>
        <translation>马氏距离</translation>
    </message>
    <message>
        <source>UNet</source>
        <translation>UNet</translation>
    </message>
    <message>
        <source>Normal Bayes: assumes multivariate normal class spectra; suits well-sampled, separable classes.</source>
        <translation>正态贝叶斯：假设各类光谱呈多维正态，适合样本较充分、类间可分的场景。</translation>
    </message>
    <message>
        <source>SVM (RBF): support vector machine with a radial basis kernel; suits medium samples and non-linear boundaries.</source>
        <translation>SVM (RBF)：支持向量机 + 径向基核，适合中等样本、非线性边界。</translation>
    </message>
    <message>
        <source>K-means: unsupervised clustering; the class count comes from the labeled samples, and labels may need mapping to ROI class ids.</source>
        <translation>K-Means：无监督聚类，类别数取自有样本的类；标签可能与 ROI 类号需对应。</translation>
    </message>
    <message>
        <source>Random forest: planned; not enabled in this build.</source>
        <translation>随机森林：计划中，当前构建未启用。</translation>
    </message>
    <message>
        <source>Mahalanobis distance classification: planned.</source>
        <translation>马氏距离分类：计划中。</translation>
    </message>
    <message>
        <source>UNet deep learning: planned.</source>
        <translation>UNet 深度学习：计划中。</translation>
    </message>
    <message>
        <source>Algorithm:</source>
        <translation>算法:</translation>
    </message>
    <message>
        <source>Bands:</source>
        <translation>波段:</translation>
    </message>
    <message>
        <source>e.g. 1,2,3</source>
        <translation>如 1,2,3</translation>
    </message>
    <message>
        <source>Band numbers taking part in the classification (starting at 1), comma-separated.
e.g. 1,2,3 or 2,3,4,5. Left empty, the first few bands are used by default.</source>
        <translation>参与分类的波段序号（从 1 开始），逗号分隔。
例：1,2,3 或 2,3,4,5。留空时默认取前若干波段。</translation>
    </message>
    <message>
        <source>Training share in stratified sampling (0.1–0.95).
The remaining samples measure accuracy (confusion matrix). Defaults to 0.7.</source>
        <translation>分层抽样中用于训练的比例（0.1–0.95）。
其余样本用于测试精度（混淆矩阵）。默认 0.7。</translation>
    </message>
    <message>
        <source>When enabled, GDAL NoData pixels of the input bands are excluded from classification and output as unclassified (0).Suits image edges or invalid areas.</source>
        <translation>启用后，各输入波段的 GDAL NoData 像元不参与分类，输出为未分类 (0)。适合影像边缘或无效区。</translation>
    </message>
    <message>
        <source>Extra ignored pixel values (any band equal to them counts as background / edge).Common: 0 fill, -9999 background. Can apply together with the source NoData.</source>
        <translation>额外忽略的像元值（任意波段等于该值则视为背景/边缘）。常见：填充 0、背景 -9999。可与源 NoData 同时生效。</translation>
    </message>
    <message>
        <source>Any band: if one band is NoData / ignored, the whole pixel is ignored (default; suits edges).
All bands: the pixel is ignored only when every band is an ignored value.</source>
        <translation>任一波段：只要有一个波段为 NoData/忽略值 → 整像素忽略（默认，适合边缘）。
全部波段：仅当所有波段均为忽略值时才忽略。</translation>
    </message>
    <message>
        <source>Training ratio:</source>
        <translation>训练比例:</translation>
    </message>
    <message>
        <source>Output:</source>
        <translation>输出:</translation>
    </message>
    <message>
        <source>/path/to/classified.tif (prompt if empty)</source>
        <translation>/path/to/classified.tif（留空则提示）</translation>
    </message>
    <message>
        <source>Classification result GeoTIFF path. Left empty, a save dialog pops up on run.</source>
        <translation>分类结果 GeoTIFF 路径。留空时运行会弹出保存对话框。</translation>
    </message>
    <message>
        <source>Cross-Validation</source>
        <translation>交叉验证</translation>
    </message>
    <message>
        <source>Stratified K-fold cross-validation to estimate model stability (writes no full-scene classification map).</source>
        <translation>分层 K 折交叉验证，估计模型稳定性（不写整景分类图）。</translation>
    </message>
    <message>
        <source>Quick Preview</source>
        <translation>快速预览</translation>
    </message>
    <message>
        <source>Classifies only the current map viewport and loads it temporarily, for quick parameter trials.</source>
        <translation>仅对当前地图视口范围分类并临时加载，便于快速试参数。</translation>
    </message>
    <message>
        <source>Train and Classify</source>
        <translation>训练并分类</translation>
    </message>
    <message>
        <source>Trains on the ROI samples and classifies the whole scene, writing the output raster; accuracy assessment follows.</source>
        <translation>用 ROI 样本训练并整景分类，写出输出栅格；完成后可做精度评价。</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens the full Classifier Setup Bar explanation.</source>
        <translation>打开分类设置栏完整说明。</translation>
    </message>
    <message>
        <source>Classification Settings</source>
        <translation>分类设置</translation>
    </message>
    <message>
        <source>Use source NoData</source>
        <translation>使用源 NoData</translation>
    </message>
    <message>
        <source>Ignored values:</source>
        <translation>忽略值:</translation>
    </message>
    <message>
        <source>e.g. 0 or 0,-9999 (comma-separated)</source>
        <translation>如 0 或 0,-9999（逗号分隔）</translation>
    </message>
    <message>
        <source>Matches:</source>
        <translation>匹配:</translation>
    </message>
    <message>
        <source>Any band</source>
        <translation>任一波段</translation>
    </message>
    <message>
        <source>All Bands</source>
        <translation>全部波段</translation>
    </message>
</context>
<context>
    <name>RsClassifyFlowchartWidget</name>
    <message>
        <source>Remote-Sensing Image Classification Pipeline</source>
        <translation>遥感影像分类处理流程</translation>
    </message>
    <message>
        <source>Pipeline progress: 0/8 steps finished (0%)</source>
        <translation>流程进度: 0/8 步已完成 (0%)</translation>
    </message>
    <message>
        <source>Input Source Image</source>
        <translation>输入源影像</translation>
    </message>
    <message>
        <source>Load a multiband remote-sensing raster and inspect its bands and spatial reference</source>
        <translation>加载多波段遥感栅格影像，检查波段与空间参考</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>打开影像</translation>
    </message>
    <message>
        <source>Define Classification Scheme</source>
        <translation>定义分类体系</translation>
    </message>
    <message>
        <source>Create/import land-cover class codes, names and display palette</source>
        <translation>创建/导入地物类别代码、类别名称与显示调色板</translation>
    </message>
    <message>
        <source>Class Management</source>
        <translation>类别管理</translation>
    </message>
    <message>
        <source>Collect Training Samples</source>
        <translation>采集训练样本</translation>
    </message>
    <message>
        <source>Digitize polygon/point ROI training samples on the source image and extract pixels</source>
        <translation>在源影像上数字化多边形/点 ROI 训练样本并提取像元</translation>
    </message>
    <message>
        <source>Sample Collection</source>
        <translation>样本采集</translation>
    </message>
    <message>
        <source>Sample Separability Evaluation</source>
        <translation>样本可分性评价</translation>
    </message>
    <message>
        <source>Compute the JM separability distance matrix and per-class mean spectral curves</source>
        <translation>计算 JM 分离度距离矩阵与类别均值光谱特征曲线</translation>
    </message>
    <message>
        <source>Separability Evaluation</source>
        <translation>可分性评价</translation>
    </message>
    <message>
        <source>Classifier Training and Classification</source>
        <translation>分类器训练与分类</translation>
    </message>
    <message>
        <source>Trains random forest / SVM / normal Bayes / KNN classifiers and predicts block-wise in a streaming fashion</source>
        <translation>训练随机森林/SVM/正态贝叶斯/KNN等分类器并分块流式预测</translation>
    </message>
    <message>
        <source>Run Classification</source>
        <translation>执行分类</translation>
    </message>
    <message>
        <source>Classification Accuracy Assessment</source>
        <translation>分类精度评定</translation>
    </message>
    <message>
        <source>Computes the confusion matrix, overall accuracy (OA) and Kappa on an independent validation set</source>
        <translation>基于独立验证集计算混淆矩阵、总体精度(OA)与Kappa系数</translation>
    </message>
    <message>
        <source>Accuracy Assessment</source>
        <translation>精度评价</translation>
    </message>
    <message>
        <source>Post-Classification</source>
        <translation>分类后处理</translation>
    </message>
    <message>
        <source>Small-patch filtering (Majority/Sieve), clumping and class recoding</source>
        <translation>碎斑过滤(Majority/Sieve)、聚类合并(Clump)与类别重编码</translation>
    </message>
    <message>
        <source>Post-Processing</source>
        <translation>后处理</translation>
    </message>
    <message>
        <source>Result Export and Loading</source>
        <translation>成果导出与加载</translation>
    </message>
    <message>
        <source>Output the final classification theme GeoTIFF, a vector Shapefile, or save the model</source>
        <translation>输出最终分类专题图 GeoTIFF、矢量 Shapefile 或保存模型</translation>
    </message>
    <message>
        <source>Export Results</source>
        <translation>导出成果</translation>
    </message>
    <message>
        <source>Not started</source>
        <translation>未开始</translation>
    </message>
    <message>
        <source>Click to run pipeline step: %1</source>
        <translation>点击执行流程步骤: %1</translation>
    </message>
    <message>
        <source>&lt;b&gt;Step %1: %2&lt;/b&gt;&lt;br&gt;%3</source>
        <translation>&lt;b&gt;第 %1 步：%2&lt;/b&gt;&lt;br&gt;%3</translation>
    </message>
    <message>
        <source>%1 (%2×%3, %4 bands)</source>
        <translation>%1（%2×%3，%4 个波段）</translation>
    </message>
    <message>
        <source>No image loaded</source>
        <translation>未加载影像</translation>
    </message>
    <message>
        <source>%1 land-cover classes defined</source>
        <translation>已定义 %1 个地物类别</translation>
    </message>
    <message>
        <source>Undefined class</source>
        <translation>未定义类别</translation>
    </message>
    <message>
        <source>%1 ROIs, %2 pixels in total</source>
        <translation>%1 个 ROI, 共 %2 像元</translation>
    </message>
    <message>
        <source>0 ROIs, 0 pixels</source>
        <translation>0 个 ROI, 0 像元</translation>
    </message>
    <message>
        <source>Not assessed</source>
        <translation>未评估</translation>
    </message>
    <message>
        <source>%1 (elapsed %2 ms)</source>
        <translation>%1 (耗时 %2 ms)</translation>
    </message>
    <message>
        <source>Unclassified</source>
        <translation>未分类</translation>
    </message>
    <message>
        <source>Accuracy not assessed</source>
        <translation>未评估精度</translation>
    </message>
    <message>
        <source>No post-processing applied</source>
        <translation>未进行后处理</translation>
    </message>
    <message>
        <source>Not exported</source>
        <translation>未导出</translation>
    </message>
    <message>
        <source>Exported to: %1</source>
        <translation>已导出至: %1</translation>
    </message>
    <message>
        <source>✓ Done</source>
        <translation>✓ 已完成</translation>
    </message>
    <message>
        <source>● Current step</source>
        <translation>● 当前步骤</translation>
    </message>
    <message>
        <source>Pipeline progress: %1/%2 steps finished (%3%)</source>
        <translation>流程进度: %1/%2 步已完成 (%3%)</translation>
    </message>
</context>
<context>
    <name>RsClassifyStepperBar</name>
    <message>
        <source>1 Scheme</source>
        <translation>1 体系</translation>
    </message>
    <message>
        <source>2 Samples</source>
        <translation>2 样本</translation>
    </message>
    <message>
        <source>3 Evaluate</source>
        <translation>3 评价</translation>
    </message>
    <message>
        <source>4 Train</source>
        <translation>4 训练</translation>
    </message>
    <message>
        <source>5 Accuracy</source>
        <translation>5 精度</translation>
    </message>
    <message>
        <source>6 Post-Processing</source>
        <translation>6 后处理</translation>
    </message>
    <message>
        <source>7 Output</source>
        <translation>7 输出</translation>
    </message>
    <message>
        <source>Click to switch to step: %1</source>
        <translation>点击切换到步骤：%1</translation>
    </message>
    <message>
        <source>Expert Mode</source>
        <translation>专家模式</translation>
    </message>
    <message>
        <source>When ticked, all steps are unlocked (the wizard guides step by step by default).</source>
        <translation>勾选后解锁全部步骤（默认向导模式逐步引导）。</translation>
    </message>
</context>
<context>
    <name>RsClassifyWorkflowController</name>
    <message>
        <source>at least 2 classes</source>
        <translation>至少 2 个类别</translation>
    </message>
    <message>
        <source>Open Source Image</source>
        <translation>打开源影像</translation>
    </message>
    <message>
        <source>at least 1 class</source>
        <translation>至少 1 个类别</translation>
    </message>
    <message>
        <source>training pixels ≥ 1</source>
        <translation>训练像元 ≥ 1</translation>
    </message>
    <message>
        <source>training pixels ≥ 10</source>
        <translation>训练像元 ≥ 10</translation>
    </message>
    <message>
        <source>Finish Full-Image Classification</source>
        <translation>完成全图分类</translation>
    </message>
    <message>
        <source>Finish Full-Image Classification or Post-Processing</source>
        <translation>完成全图分类或后处理</translation>
    </message>
</context>
<context>
    <name>RsGeorefFlowchartWidget</name>
    <message>
        <source>Remote-Sensing Geometric Correction Pipeline</source>
        <translation>遥感影像几何校正流程</translation>
    </message>
    <message>
        <source>Pipeline progress: 0/7 steps ready (0%)</source>
        <translation>流程进度: 0/7 步已就绪 (0%)</translation>
    </message>
    <message>
        <source>Load Source Image</source>
        <translation>加载源影像</translation>
    </message>
    <message>
        <source>Open the raw remote-sensing raster to be geometrically corrected / registered</source>
        <translation>打开待几何校正/配准的原始遥感栅格影像</translation>
    </message>
    <message>
        <source>Open Image</source>
        <translation>打开影像</translation>
    </message>
    <message>
        <source>Collect Control Points (GCPs)</source>
        <translation>采集控制点 (GCP)</translation>
    </message>
    <message>
        <source>Collect conjugate control point pairs on the source image and reference base map, or use auto matching</source>
        <translation>在源影像与参考底图上采集同名控制点对，或使用自动匹配</translation>
    </message>
    <message>
        <source>Control Point Collection</source>
        <translation>控制点采集</translation>
    </message>
    <message>
        <source>Select Transform Model</source>
        <translation>选择变换模型</translation>
    </message>
    <message>
        <source>Set the geometric correction model (polynomial order 1–3 / linear / Helmert / thin plate spline / RPC)</source>
        <translation>设定几何校正数学模型（多项式1-3阶/线性/Helmert/薄板样条/RPC）</translation>
    </message>
    <message>
        <source>Model Parameters</source>
        <translation>模型参数</translation>
    </message>
    <message>
        <source>Residual and Accuracy Check</source>
        <translation>残差与精度检查</translation>
    </message>
    <message>
        <source>Compute control point pixel residuals (dx, dy) and the total RMS, rejecting gross errors</source>
        <translation>计算控制点像元残差(dx, dy)与总 RMS 均方根误差，剔除粗差点</translation>
    </message>
    <message>
        <source>Residual Check</source>
        <translation>残差检查</translation>
    </message>
    <message>
        <source>Configure Correction Parameters</source>
        <translation>配置校正参数</translation>
    </message>
    <message>
        <source>Specify the target CRS, pixel resolution, resampling method and output path</source>
        <translation>指定目标坐标系(CRS)、像元分辨率、重采样方法与输出路径</translation>
    </message>
    <message>
        <source>Output Settings</source>
        <translation>输出配置</translation>
    </message>
    <message>
        <source>Run Resampling Correction</source>
        <translation>执行重采样校正</translation>
    </message>
    <message>
        <source>Starts the background multi-threaded resampling engine to produce the corrected raster</source>
        <translation>启动后台多线程重采样变换引擎，生成几何校正后的栅格</translation>
    </message>
    <message>
        <source>Start Correction</source>
        <translation>开始校正</translation>
    </message>
    <message>
        <source>Result Loading and Verification</source>
        <translation>成果加载与验证</translation>
    </message>
    <message>
        <source>Load the corrected raster onto the main map canvas for spatial overlay against the base map</source>
        <translation>将纠正后的栅格加载至主地图画布，与基准底图进行空间叠加比对</translation>
    </message>
    <message>
        <source>Load Results</source>
        <translation>加载成果</translation>
    </message>
    <message>
        <source>Not started</source>
        <translation>未开始</translation>
    </message>
    <message>
        <source>%1 (%2×%3, %4 bands)</source>
        <translation>%1（%2×%3，%4 个波段）</translation>
    </message>
    <message>
        <source>No image loaded</source>
        <translation>未加载影像</translation>
    </message>
    <message>
        <source>%1 GCPs (%2 enabled)</source>
        <translation>%1 个 GCP（启用 %2 个）</translation>
    </message>
    <message>
        <source>0 GCPs (0 enabled)</source>
        <translation>0 个 GCP（启用 0 个）</translation>
    </message>
    <message>
        <source>%1 (needs ≥ %2 points)</source>
        <translation>%1 (需 ≥%2 点)</translation>
    </message>
    <message>
        <source> (excellent)</source>
        <translation> (优)</translation>
    </message>
    <message>
        <source> (good)</source>
        <translation> (良好)</translation>
    </message>
    <message>
        <source> (needs tuning)</source>
        <translation> (需优化)</translation>
    </message>
    <message>
        <source>Unsolved</source>
        <translation>未解算</translation>
    </message>
    <message>
        <source>Parameters not configured</source>
        <translation>未配置参数</translation>
    </message>
    <message>
        <source>Correction task running...</source>
        <translation>校正任务运行中…</translation>
    </message>
    <message>
        <source>Ready, waiting to run</source>
        <translation>就绪，等待执行</translation>
    </message>
    <message>
        <source>Loaded into the main map: %1</source>
        <translation>已加载至主地图: %1</translation>
    </message>
    <message>
        <source>Output: %1</source>
        <translation>已输出: %1</translation>
    </message>
    <message>
        <source>No results loaded</source>
        <translation>未加载成果</translation>
    </message>
    <message>
        <source>✓ Ready</source>
        <translation>✓ 已就绪</translation>
    </message>
    <message>
        <source>● Current step</source>
        <translation>● 当前步骤</translation>
    </message>
    <message>
        <source>Not Ready</source>
        <translation>未就绪</translation>
    </message>
    <message>
        <source>Pipeline progress: %1/%2 steps ready (%3%)</source>
        <translation>流程进度: %1/%2 步已就绪 (%3%)</translation>
    </message>
    <message>
        <source>Polynomial order 1 (needs ≥ 3 points)</source>
        <translation>多项式 1 阶 (需 ≥3 点)</translation>
    </message>
    <message>
        <source>Output parameters not configured</source>
        <translation>未配置输出参数</translation>
    </message>
    <message>
        <source>Correction not started</source>
        <translation>未开始校正</translation>
    </message>
</context>
<context>
    <name>RsGeorefModeToggle</name>
    <message>
        <source>Image → Map</source>
        <translation>影像 → 地图</translation>
    </message>
    <message>
        <source>Image → Image</source>
        <translation>影像 → 影像</translation>
    </message>
    <message>
        <source>RPC Physical Model</source>
        <translation>RPC 物理模型</translation>
    </message>
    <message>
        <source>Image-to-map registration: pick points on the image and enter geographic coordinates.</source>
        <translation>影像对地图配准：在影像上选点并输入地理坐标。</translation>
    </message>
    <message>
        <source>Image-to-image registration: pick conjugate points on the image to correct and the reference image.</source>
        <translation>影像对影像配准：在待校正影像与参考影像上选同名点。</translation>
    </message>
    <message>
        <source>RPC physical model: correction using rational polynomial coefficients.</source>
        <translation>RPC 物理模型：使用有理多项式系数进行物理模型校正。</translation>
    </message>
</context>
<context>
    <name>RsGeorefParamsPanel</name>
    <message>
        <source>Correction Parameters Panel</source>
        <translation>校正参数面板</translation>
    </message>
    <message>
        <source>Correction parameters: transform / resampling / residuals / CRS / output</source>
        <translation>校正参数：变换 / 重采样 / 残差 / CRS / 输出</translation>
    </message>
    <message>
        <source>Parameter Description</source>
        <translation>参数说明</translation>
    </message>
    <message>
        <source>Opens the full 'Correction Parameters' explanation (transform method, point counts, resampling, RMS, CRS, output).</source>
        <translation>打开「校正参数」完整说明（变换方法、点数、重采样、RMS、CRS、输出）。</translation>
    </message>
    <message>
        <source>Coordinate Transformation</source>
        <translation>坐标变换</translation>
    </message>
    <message>
        <source>Linear</source>
        <translation>线性</translation>
    </message>
    <message>
        <source>Helmert</source>
        <translation>Helmert</translation>
    </message>
    <message>
        <source>Polynomial Order 1</source>
        <translation>Polynomial Order 1 (一次多项式)</translation>
    </message>
    <message>
        <source>Polynomial Order 2</source>
        <translation>Polynomial Order 2 (二次多项式)</translation>
    </message>
    <message>
        <source>Polynomial Order 3</source>
        <translation>Polynomial Order 3 (三次多项式)</translation>
    </message>
    <message>
        <source>Thin Plate Spline</source>
        <translation>薄板样条</translation>
    </message>
    <message>
        <source>Projective</source>
        <translation>透视</translation>
    </message>
    <message>
        <source>RPC Physical (RFM)</source>
        <translation>RPC 物理模型（RFM）</translation>
    </message>
    <message>
        <source>Method</source>
        <translation>方法</translation>
    </message>
    <message>
        <source>Geometric transformation model. Hover the combo box for per-method descriptions and minimum point counts.</source>
        <translation>几何变换模型类型。悬停下拉框查看各方法说明与最少点数。</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Minimum Points</source>
        <translation>最少点数</translation>
    </message>
    <message>
        <source>Minimum number of enabled GCPs required by the method.</source>
        <translation>方法所需最少启用 GCP 数。</translation>
    </message>
    <message>
        <source>Actually usable points</source>
        <translation>实际可用点数</translation>
    </message>
    <message>
        <source>Number of GCPs enabled and used in the fit.</source>
        <translation>已启用并参与拟合的 GCP 数。</translation>
    </message>
    <message>
        <source>Degrees of Freedom</source>
        <translation>自由度 DOF</translation>
    </message>
    <message>
        <source>Actual points minus the minimum required. Residual analysis needs &gt; 0.</source>
        <translation>实际点数减最少点数。&gt;0 才能用残差评估。</translation>
    </message>
    <message>
        <source>Resampling</source>
        <translation>重采样</translation>
    </message>
    <message>
        <source>Nearest Neighbour</source>
        <translation>最邻近</translation>
    </message>
    <message>
        <source>Bilinear</source>
        <translation>双线性</translation>
    </message>
    <message>
        <source>Cubic</source>
        <translation>三次卷积</translation>
    </message>
    <message>
        <source>Cubic Spline</source>
        <translation>三次样条</translation>
    </message>
    <message>
        <source>Lanczos</source>
        <translation>Lanczos</translation>
    </message>
    <message>
        <source>Algorithm</source>
        <translation>算法</translation>
    </message>
    <message>
        <source>Pixel interpolation method.</source>
        <translation>像元插值方法。</translation>
    </message>
    <message>
        <source>auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <source>Output Pixel Size</source>
        <translation>输出像元大小</translation>
    </message>
    <message>
        <source>Target grid resolution; auto = automatic.</source>
        <translation>目标网格分辨率；auto=自动。</translation>
    </message>
    <message>
        <source>auto · ref</source>
        <translation>自动 · 参考</translation>
    </message>
    <message>
        <source>Output Extent</source>
        <translation>输出范围</translation>
    </message>
    <message>
        <source>Map extent covered by the result.</source>
        <translation>结果覆盖的地图范围。</translation>
    </message>
    <message>
        <source>Background Value</source>
        <translation>背景值</translation>
    </message>
    <message>
        <source>Pixel value used to fill holes.</source>
        <translation>空洞填充像元值。</translation>
    </message>
    <message>
        <source>RMS Error Distribution</source>
        <translation>RMS 误差分布</translation>
    </message>
    <message>
        <source>Root-mean-square of X residuals (pixels).</source>
        <translation>X 方向残差的均方根（像元）。</translation>
    </message>
    <message>
        <source>Root-mean-square of Y residuals (pixels).</source>
        <translation>Y 方向残差的均方根（像元）。</translation>
    </message>
    <message>
        <source>X RMS</source>
        <translation>X 方向 RMS</translation>
    </message>
    <message>
        <source>RMS of X residuals.</source>
        <translation>X 向残差 RMS。</translation>
    </message>
    <message>
        <source>Y RMS</source>
        <translation>Y 方向 RMS</translation>
    </message>
    <message>
        <source>RMS of Y residuals.</source>
        <translation>Y 向残差 RMS。</translation>
    </message>
    <message>
        <source>Total RMS</source>
        <translation>总 RMS</translation>
    </message>
    <message>
        <source>Total residual RMS.</source>
        <translation>总残差 RMS。</translation>
    </message>
    <message>
        <source>Maximum Residual</source>
        <translation>最大残差</translation>
    </message>
    <message>
        <source>The worst single GCP.</source>
        <translation>最差的一个 GCP。</translation>
    </message>
    <message>
        <source>RMS before RPC refinement (shown with ≥ 3 GCPs).</source>
        <translation>RPC 精化前 RMS（有 ≥3 个 GCP 时显示）。</translation>
    </message>
    <message>
        <source>RMS after RPC linear-bias refinement; green means improvement.</source>
        <translation>RPC 线性偏差精化后 RMS；绿字表示精化改善。</translation>
    </message>
    <message>
        <source>Coordinate System</source>
        <translation>坐标系</translation>
    </message>
    <message>
        <source>Source CRS</source>
        <translation>源 CRS</translation>
    </message>
    <message>
        <source>Source image CRS.</source>
        <translation>源影像坐标系。</translation>
    </message>
    <message>
        <source>Target CRS</source>
        <translation>目标 CRS</translation>
    </message>
    <message>
        <source>Result and fit target coordinate system.</source>
        <translation>结果与拟合目标坐标系。</translation>
    </message>
    <message>
        <source>Projection Name</source>
        <translation>投影名</translation>
    </message>
    <message>
        <source>Human-readable name of the target CRS.</source>
        <translation>目标 CRS 的可读名称。</translation>
    </message>
    <message>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>/path/to/output.tif</source>
        <translation>/path/to/output.tif</translation>
    </message>
    <message>
        <source>Browse…</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the output file location.</source>
        <translation>浏览选择输出文件位置。</translation>
    </message>
    <message>
        <source>DEM (RPC mode)</source>
        <translation>DEM（RPC 模式）</translation>
    </message>
    <message>
        <source>This panel controls all write-out parameters of the geometric correction.
Hover the section titles and widgets for details; press 'Parameter Description' for the full documentation.</source>
        <translation>本面板控制几何校正的全部写出参数。
悬停各分区标题与控件可看详细说明；点「参数说明」查看完整文档。</translation>
    </message>
    <message>
        <source>[Transform] Fits a geometric model from source image coordinates to target coordinates using GCPs.
Each method needs a different minimum point count; fitting is unreliable below it and 'Run' is disabled.
Same-scene registration usually needs only Linear or a first-order polynomial; use higher orders / TPS for complex distortions.</source>
        <translation>【坐标变换】用 GCP 拟合「源影像坐标 → 目标坐标」的几何模型。
不同方法所需最少点数不同；实际点数不足时无法可靠拟合，「运行」会禁用。
同景配准一般用 Linear 或 一次多项式即可；复杂畸变再用高阶/TPS。</translation>
    </message>
    <message>
        <source>Transform method (geometric model):
• Linear (≥ 2 points): translation + scale; common for the same scene / nearly collinear cases
• Helmert (≥ 2 points): similarity transform (rotation + uniform scale)
• Polynomial 1 (≥ 3 points): affine; corrects rotation / shear
• Polynomial 2 / 3 (≥ 6/10 points): bending deformation; high orders overfit easily
• TPS thin plate spline: strong local deformation; GCPs should be evenly spread
• Projective: perspective (scanned maps, oblique imagery)
• RPC Physical: sensor RPC, Image→Map only; requires metadata and an optional DEM

Tip: with exactly the minimum points, DOF = 0 and residuals approach 0 — that does not mean good accuracy; collect more points.</source>
        <translation>变换方法（几何模型）：
• Linear（线性，≥2 点）：平移+缩放，同景/近似共线时常用
• Helmert（≥2 点）：相似变换（旋转+统一缩放）
• 一次多项式（≥3 点）：仿射，纠正旋转/剪切
• 二次/三次多项式（≥6/10 点）：弯曲变形，阶数高易过拟合
• TPS 薄板样条：局部变形强，GCP 宜均匀
• Projective：透视（扫描图、倾斜摄影）
• RPC Physical：传感器 RPC，仅 Image→Map，需元数据与可选 DEM

提示：点数刚好等于最少点数时 DOF=0，残差会接近 0，不能说明精度好，应多采点。</translation>
    </message>
    <message>
        <source>Minimum points: the lower bound of 'enabled' GCPs required by the current transform method.
For example, a cubic polynomial usually needs about 10 points. Below the minimum, fitting is unreliable.</source>
        <translation>最少点数：当前变换方法要求的「已启用」GCP 下限。
例如三次多项式通常约 10 点。未达下限时不能可靠拟合。</translation>
    </message>
    <message>
        <source>Usable points: the number of control points ticked 'enabled' in the GCP table.
Only enabled points take part in the fit and RMS computation.</source>
        <translation>实际可用点数：GCP 表中勾选「启用」的控制点个数。
只有启用的点参与拟合与 RMS 计算。</translation>
    </message>
    <message>
        <source>Degrees of freedom DOF = usable points − minimum points.
• DOF &lt; 0: not enough points to fit
• DOF = 0: exactly determined; residuals are 'fitted away' to almost always 0, with no statistical meaning
• DOF &gt; 0: over-determined; assess accuracy via RMS — collecting more evenly distributed points is advisable</source>
        <translation>自由度 DOF = 实际可用点数 − 最少点数。
• DOF &lt; 0：点数不够，无法拟合
• DOF = 0：刚好定解，残差会被「拟合光」，几乎总是 0，无统计意义
• DOF &gt; 0：可过约束，用 RMS 评估精度；宜再多采均匀分布的点</translation>
    </message>
    <message>
        <source>[Resampling] Pixel interpolation used when warping the source image onto the target grid by the transform model.
Affects only the output's smoothness / sharpness; the GCP geometric fit itself is unchanged.</source>
        <translation>【重采样】将源影像按变换模型「扭曲」到目标网格时的像元插值方式。
只影响输出影像的平滑/锐利程度，不改变 GCP 几何拟合本身。</translation>
    </message>
    <message>
        <source>Resampling algorithm (when writing the raster):
• Nearest Neighbour: no neighbourhood mixing; first choice for classification / integer labels
• Bilinear: balanced speed and quality; common for continuous grayscale / multispectral
• Cubic: cubic convolution, smoother with slightly softer edges
• Cubic Spline / Lanczos: higher order, sharper / slower; use with care for quantitative work

Visual optics: Bilinear or Cubic; classification maps: Nearest.</source>
        <translation>重采样算法（写出栅格时）：
• Nearest Neighbour：最近邻，不混合邻域，分类/整型标签首选
• Bilinear：双线性，连续灰度/多光谱常用，速度与质量均衡
• Cubic：三次卷积，更平滑，边缘略糊
• Cubic Spline / Lanczos：更高阶，更锐/更慢，慎用于定量

光学目视：Bilinear 或 Cubic；分类图：Nearest。</translation>
    </message>
    <message>
        <source>Output pixel size (ground units of the target CRS, e.g. metres).
• auto (0): estimated by the engine from the input / reference
• Manual: e.g. 30 means 30 m resolution (under UTM)
For I2I alignment to the reference, the reference resolution or auto is typical.</source>
        <translation>输出像元大小（目标 CRS 的地面单位，如米）。
• auto（0）：由引擎按输入/参考估计
• 手动：如 30 表示 30 m 分辨率（UTM 下）
I2I 对齐参考时常用参考分辨率或 auto。</translation>
    </message>
    <message>
        <source>Output geographic extent (read-only preview).
auto · ref: the extent follows the reference / transform result automatically; usually no change needed.</source>
        <translation>输出地理范围（只读预览）。
auto · ref：按参考/变换结果自动确定范围，一般无需改。</translation>
    </message>
    <message>
        <source>Background / fill value: written to pixels the warp leaves without source data.
Usually 0; if 0 is a valid DN, use e.g. 65535 instead and set it as NoData in the result.</source>
        <translation>背景/填充值：扭曲后无源数据覆盖的像元写入此值。
常用 0；若 0 是有效 DN，可改为如 65535 并在结果中设 NoData。</translation>
    </message>
    <message>
        <source>[RMS Error] Root mean square of 'predicted − observed' positions over enabled GCPs.
Usually in source image pixels (px). With accurate conjugate points and a suitable model, the RMS should be small.
With DOF = 0, residuals are fitted to nearly 0 and do not represent real accuracy — collect more points.</source>
        <translation>【RMS 误差】启用 GCP 上「预测位置 − 观测位置」的均方根。
单位一般为源影像像元 (px)。同名点选得准、模型合适时 RMS 应较小。
DOF=0 时残差会被拟合到接近 0，不能代表真实精度——请多采点。</translation>
    </message>
    <message>
        <source>Residual scatter plot:
• Horizontal axis ≈ ΔX (column-direction residual)
• Vertical axis ≈ ΔY (row-direction residual)
Points should stay near the origin and be roughly isotropic. For outliers: check whether the wrong feature was picked, or disable the point in the GCP table.</source>
        <translation>残差散点图：
• 横轴 ≈ ΔX（列方向残差）
• 纵轴 ≈ ΔY（行方向残差）
点应靠近原点且大致各向均匀。离群点：检查是否取错同名地物，或在 GCP 表禁用该点。</translation>
    </message>
    <message>
        <source>Total RMS: root mean square of the residual magnitudes of all enabled GCPs.
Visual same-scene registration: a few pixels is the norm; hundreds to thousands means checking CRS / Sync zoom / point picking.</source>
        <translation>Total RMS：所有启用 GCP 残差模长的均方根。
目视同景配准：通常希望数像素级；若数百～数千需检查 CRS/Sync zoom/取点。</translation>
    </message>
    <message>
        <source>The maximum residual and its GCP number. Check first whether that point was picked wrongly or suffers edge distortion.</source>
        <translation>最大残差及对应 GCP 编号。优先检查该点是否取错或影像边缘畸变。</translation>
    </message>
    <message>
        <source>[CRS] The target CRS determines the output GeoTIFF projection and how GCP target coordinates are interpreted.
I2I: usually identical to the reference image CRS (aligned automatically when the reference loads).
I2M: the Map canvas follows the target CRS when showing main project layers.</source>
        <translation>【坐标系】目标 CRS 决定输出 GeoTIFF 的投影，并参与 GCP 目标坐标解释。
I2I：通常与参考影像 CRS 一致（加载参考后会自动对齐）。
I2M：Map 画布会尽量跟随目标 CRS 显示主工程图层。</translation>
    </message>
    <message>
        <source>The source image (Warp) CRS. Shows — when undefined.
Picked coordinates follow the layer CRS.</source>
        <translation>源影像 (Warp) 的坐标系。未定义时显示 —。
取点坐标以图层 CRS 为准。</translation>
    </message>
    <message>
        <source>Target CRS: the coordinate system of the correction result and of the fit.
Common choices: WGS 84 / UTM zone xxN, CGCS2000 Gauss projection, etc.
In I2I, it is set to the reference CRS automatically once the reference image loads.</source>
        <translation>目标 CRS：校正结果与拟合所用目标坐标系。
常见：WGS 84 / UTM zone xxN、CGCS2000 高斯投影等。
I2I 加载参考影像后会尽量自动设为参考 CRS。</translation>
    </message>
    <message>
        <source>[Output] Save path of the corrected GeoTIFF.
The toolbar 'Run' enables only after a valid path is entered (and GCP counts / fitting conditions are met).
The task list records this path so the result can be loaded into the main project when finished.</source>
        <translation>【输出】校正后的 GeoTIFF 保存路径。
必须填写有效路径后，工具栏「运行」才会启用（且 GCP 数量与拟合需满足条件）。
任务列表会记录该路径，完成后可加载到主工程。</translation>
    </message>
    <message>
        <source>Full path of the output file; .tif / .tiff recommended.
The directory must be writable; same-named files may be overwritten (depending on the task implementation).</source>
        <translation>输出文件完整路径，建议使用 .tif / .tiff。
目录需可写；同名文件可能被覆盖（视任务实现）。</translation>
    </message>
    <message>
        <source>[DEM] Shown only when the transform method is RPC Physical.
An optional DEM improves RPC projection heights; the Z offset is a metric correction relative to the DEM.</source>
        <translation>【DEM】仅当变换方法为 RPC Physical 时显示。
可选 DEM 改善 RPC 投影高程；Z 偏移为相对 DEM 的米制修正。</translation>
    </message>
    <message>
        <source>/path/to/dem.tif (optional)</source>
        <translation>/path/to/dem.tif（可选）</translation>
    </message>
    <message>
        <source>DEM path (optional), used for RPC height-related projection.</source>
        <translation>数字高程模型路径（可选）。用于 RPC 高度相关投影。</translation>
    </message>
    <message>
        <source>Select DEM File</source>
        <translation>选择 DEM 文件</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif *.tiff);;All files (*)</source>
        <translation>GeoTIFF (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Selects the DEM raster file.</source>
        <translation>选择 DEM 栅格文件。</translation>
    </message>
    <message>
        <source>DEM path</source>
        <translation>DEM 路径</translation>
    </message>
    <message>
        <source> m</source>
        <translation> m</translation>
    </message>
    <message>
        <source>Elevation offset relative to the DEM (m), passed as the RPC_HEIGHT option.</source>
        <translation>相对 DEM 的高程偏移（米），传入 RPC_HEIGHT 选项。</translation>
    </message>
    <message>
        <source>Elevation Offset</source>
        <translation>高程偏移</translation>
    </message>
    <message>
        <source>In RPC mode the target CRS is fixed to EPSG:4326 (WGS84 lat/lon, the RPC output space); reproject afterwards if a projection is needed</source>
        <translation>RPC 模式下目标 CRS 固定为 EPSG:4326（WGS84 经纬度，RPC 输出空间）；如需投影请在校正后另行重投影</translation>
    </message>
    <message>
        <source>Target CRS: the coordinate system used for the correction result and the fit.
Common choices: WGS 84 / UTM zone xxN, CGCS2000 Gauss projection, etc.
In I2I, it is set to the reference CRS automatically once the reference image loads.</source>
        <translation>目标 CRS：校正结果与拟合所用目标坐标系。
常见：WGS 84 / UTM zone xxN、CGCS2000 高斯投影等。
I2I 加载参考影像后会尽量自动设为参考 CRS。</translation>
    </message>
    <message>
        <source> px</source>
        <translation> px</translation>
    </message>
    <message>
        <source>%1 px (row #%2)</source>
        <translation>%1 px (行 #%2)</translation>
    </message>
    <message>
        <source>RMS before refinement: %1 px</source>
        <translation>精化前 RMS: %1 px</translation>
    </message>
    <message>
        <source>RMS after refinement: %1 px</source>
        <translation>精化后 RMS: %1 px</translation>
    </message>
    <message>
        <source>RMS before refinement: —</source>
        <translation>精化前 RMS: —</translation>
    </message>
    <message>
        <source>RMS after refinement: —</source>
        <translation>精化后 RMS: —</translation>
    </message>
    <message>
        <source>Select Output GeoTIFF</source>
        <translation>选择输出 GeoTIFF</translation>
    </message>
</context>
<context>
    <name>RsGeorefTaskList</name>
    <message>
        <source>Tasks: 0</source>
        <translation>任务: 0</translation>
    </message>
    <message>
        <source>Task statistics: total / running / done / failed.</source>
        <translation>任务统计：总数 / 运行中 / 完成 / 失败。</translation>
    </message>
    <message>
        <source>Deselect</source>
        <translation>取消选中</translation>
    </message>
    <message>
        <source>Cancels the selected correction task if it is still running.</source>
        <translation>取消当前选中且仍在运行的校正任务。</translation>
    </message>
    <message>
        <source>Clear Finished</source>
        <translation>清空已完成</translation>
    </message>
    <message>
        <source>Removes finished/failed/cancelled tasks from the list; running tasks are not affected.</source>
        <translation>从列表移除已完成/失败/取消的任务，不影响运行中任务。</translation>
    </message>
    <message>
        <source>#</source>
        <translation>#</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Method</source>
        <translation>方法</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <source>GCP</source>
        <translation>GCP</translation>
    </message>
    <message>
        <source>RMS</source>
        <translation>RMS</translation>
    </message>
    <message>
        <source>Elapsed</source>
        <translation>耗时</translation>
    </message>
    <message>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>Double-click a finished task to load its results into the main project; right-click a running task to cancel it</source>
        <translation>双击成功任务：加载结果到主工程；右键可取消运行中任务</translation>
    </message>
    <message>
        <source>Cancel Task</source>
        <translation>取消任务</translation>
    </message>
    <message>
        <source>Load Results into Main Project</source>
        <translation>加载结果到主工程</translation>
    </message>
    <message>
        <source>Source: %1</source>
        <translation>来源：%1</translation>
    </message>
    <message>
        <source>%1 ms</source>
        <translation>%1 ms</translation>
    </message>
    <message>
        <source>%1 s</source>
        <translation>%1 s</translation>
    </message>
    <message>
        <source>
%1 bytes</source>
        <translation>
%1 字节</translation>
    </message>
    <message>
        <source>Tasks: %1  |  Running %2  ·  Done %3  ·  Failed %4</source>
        <translation>任务: %1  |  运行中 %2  ·  完成 %3  ·  失败 %4</translation>
    </message>
</context>
<context>
    <name>RsJobPanel</name>
    <message>
        <source>Task Center</source>
        <translation>任务中心</translation>
    </message>
    <message>
        <source>Right-click a task for method/parameters/inputs/outputs, to stop it or load its results into the main view. Right-click empty space to refresh and view help.</source>
        <translation>右键任务可查看方法/参数/输入输出，或停止、加载结果到主图。空白处右键也可刷新与查看说明。</translation>
    </message>
    <message>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <source>Running</source>
        <translation>运行中</translation>
    </message>
    <message>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <source>Cancel Queued or Running Tasks</source>
        <translation>取消排队或运行中的任务</translation>
    </message>
    <message>
        <source>Load to Main View</source>
        <translation>加载到主图</translation>
    </message>
    <message>
        <source>Load the selected task's output paths into the main program layers</source>
        <translation>将选中任务的输出路径加载到主程序图层</translation>
    </message>
    <message>
        <source>Clear Finished</source>
        <translation>清空已完成</translation>
    </message>
    <message>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <source>Load</source>
        <translation>加载</translation>
    </message>
    <message>
        <source>Estimated Remaining</source>
        <translation>预计剩余</translation>
    </message>
    <message>
        <source>Estimated from elapsed time and current progress; unavailable at 0% or while paused</source>
        <translation>基于已用时间与当前进度的估算；进度为 0 或暂停时不可用</translation>
    </message>
    <message>
        <source>Tick: load outputs into the main program automatically after the task succeeds</source>
        <translation>勾选：任务成功后自动将输出加载到主程序</translation>
    </message>
    <message>
        <source>No tasks yet</source>
        <translation>暂无任务</translation>
    </message>
    <message>
        <source>All computation and algorithm tasks are finished or not yet submitted. Start new tasks in the Processing Toolbox or a workflow.</source>
        <translation>所有计算与算法任务均已完成或尚未提交。可在处理工具箱或工作流中启动新任务。</translation>
    </message>
    <message>
        <source>Select a task to view method, parameters, inputs/outputs...</source>
        <translation>选择任务查看方法、参数、输入输出…</translation>
    </message>
    <message>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <source>Result</source>
        <translation>结果</translation>
    </message>
    <message>
        <source>Select a task to view its log...</source>
        <translation>选择任务以查看日志…</translation>
    </message>
    <message>
        <source>Log</source>
        <translation>日志</translation>
    </message>
    <message>
        <source>No loadable output path found (or the file does not exist).</source>
        <translation>未找到可加载的输出路径（或文件不存在）。</translation>
    </message>
    <message>
        <source>Task Center: aggregates title, status, progress and output loading for all background tasks.</source>
        <translation>任务中心：汇总所有后台任务的标题、状态、进度与输出加载。</translation>
    </message>
    <message>
        <source>Filters the task list by state: all / running / failed / finished.</source>
        <translation>按状态筛选任务列表：全部 / 运行中 / 失败 / 已完成。</translation>
    </message>
    <message>
        <source>Cancels queued or running tasks (a confirmation pops up).</source>
        <translation>取消排队或运行中的任务（会弹出确认）。</translation>
    </message>
    <message>
        <source>Loads the selected task's output paths into the main program layers (successful tasks only).</source>
        <translation>将选中任务的输出路径加载到主程序图层（仅成功任务）。</translation>
    </message>
    <message>
        <source>Clear all finished/failed/cancelled tasks from the list (a confirmation pops up).</source>
        <translation>从列表清除所有已完成/失败/已取消的任务（会弹出确认）。</translation>
    </message>
    <message>
        <source>Task list. Double-click for details; right-click for details/log, stop, pause/resume, retry, load output and copy info.</source>
        <translation>任务列表。双击查看详情；右键查看详情/日志、停止、暂停/恢复、重试、加载输出、复制信息。</translation>
    </message>
    <message>
        <source>Task details: method ID, parameters, inputs/outputs and results.</source>
        <translation>任务详情：方法 ID、参数、输入输出与结果。</translation>
    </message>
    <message>
        <source>Task run log (read-only).</source>
        <translation>任务运行日志（只读）。</translation>
    </message>
    <message>
        <source>Operation tips.</source>
        <translation>操作提示。</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation>排队</translation>
    </message>
    <message>
        <source>Waiting for Resources</source>
        <translation>等待资源</translation>
    </message>
    <message>
        <source>Cancelling</source>
        <translation>取消中</translation>
    </message>
    <message>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <source>Succeeded</source>
        <translation>成功</translation>
    </message>
    <message>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <source>Task ID: %1
Method: %2
Right-click for details / stop / load</source>
        <translation>任务 ID: %1
方法: %2
右键查看详情 / 停止 / 加载</translation>
    </message>
    <message>
        <source>When ticked, outputs are loaded into the main program automatically on task success</source>
        <translation>勾选后任务成功时自动加载输出到主程序</translation>
    </message>
    <message>
        <source>(task not found)</source>
        <translation>(任务不存在)</translation>
    </message>
    <message>
        <source>—— Task Log · %1 ——</source>
        <translation>—— 任务日志 · %1 ——</translation>
    </message>
    <message>
        <source>(no log yet)</source>
        <translation>(暂无日志)</translation>
    </message>
    <message>
        <source>[Basic information]</source>
        <translation>【基本信息】</translation>
    </message>
    <message>
        <source>Task ID: %1</source>
        <translation>任务 ID：%1</translation>
    </message>
    <message>
        <source>Title: %1</source>
        <translation>标题：%1</translation>
    </message>
    <message>
        <source>Method (algorithmId): %1</source>
        <translation>方法 (algorithmId)：%1</translation>
    </message>
    <message>
        <source>Source: %1</source>
        <translation>来源：%1</translation>
    </message>
    <message>
        <source>Client tag: %1</source>
        <translation>客户端标记：%1</translation>
    </message>
    <message>
        <source>Exclusive execution: %1</source>
        <translation>独占执行：%1</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <source>Internal jobId: %1</source>
        <translation>内部 jobId：%1</translation>
    </message>
    <message>
        <source>Status: %1</source>
        <translation>状态：%1</translation>
    </message>
    <message>
        <source>Progress: %1</source>
        <translation>进度：%1</translation>
    </message>
    <message>
        <source>Loaded to main view on success: %1</source>
        <translation>成功后加载到主图：%1</translation>
    </message>
    <message>
        <source>Start time: %1</source>
        <translation>开始时间：%1</translation>
    </message>
    <message>
        <source>End time: %1</source>
        <translation>结束时间：%1</translation>
    </message>
    <message>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <source>[Method parameters (params)]</source>
        <translation>【方法参数 params】</translation>
    </message>
    <message>
        <source>(no parameters)</source>
        <translation>(无参数)</translation>
    </message>
    <message>
        <source>[Inputs / path-like parameters]</source>
        <translation>【输入 / 路径类参数】</translation>
    </message>
    <message>
        <source>  (no path-like input parameters found)</source>
        <translation>  (未识别到路径类输入键)</translation>
    </message>
    <message>
        <source>[Result / output]</source>
        <translation>【结果 / 输出 result】</translation>
    </message>
    <message>
        <source>(no results yet)</source>
        <translation>(尚无结果)</translation>
    </message>
    <message>
        <source>[Loadable output paths]</source>
        <translation>【可加载输出路径】</translation>
    </message>
    <message>
        <source>  (None)</source>
        <translation>  (无)</translation>
    </message>
    <message>
        <source>[present]</source>
        <translation>[存在]</translation>
    </message>
    <message>
        <source>[missing]</source>
        <translation>[不存在]</translation>
    </message>
    <message>
        <source>Cancel Task</source>
        <translation>取消任务</translation>
    </message>
    <message>
        <source>Cancel task '%1'?
A running task will be aborted; intermediate results already produced are not rolled back.</source>
        <translation>确定取消任务「%1」？
运行中的任务将被中止，已产生的中间结果不会回滚。</translation>
    </message>
    <message>
        <source>Clear %1 finished/failed/cancelled tasks from the list?
(Output files on disk are not deleted.)</source>
        <translation>从列表清除 %1 个已完成/失败/已取消的任务？
（不会删除磁盘上的输出文件。）</translation>
    </message>
    <message>
        <source>The Task Center aggregates all algorithm tasks submitted through the Task Center (JobEngine is the internal execution adapter).

• The list shows title, status and progress; when the 'Load' column is ticked, outputs are loaded into the main view automatically on task success.
• Right-click a task: view details (method/parameters/inputs/outputs), stop, load outputs, copy info.
• Right-click empty list space: refresh, clear finished, this help.
• Cancellation, logs and final states are authoritative in the Task Center; this panel is a projection and holds no independent lifecycle state.</source>
        <translation>任务中心汇总所有经 Task Center 提交的算法任务（JobEngine 为内部执行适配器）。

• 列表显示标题、状态、进度；「加载」列勾选后，任务成功时自动把输出加载到主图。
• 右键任务：查看详情（方法/参数/输入输出）、停止、加载输出、复制信息。
• 列表空白处右键：刷新、清空已完成、本说明。
• 取消、日志与终态均以 Task Center 为准；本面板为投影，不持有独立生命周期状态。</translation>
    </message>
    <message>
        <source>Refresh List</source>
        <translation>刷新列表</translation>
    </message>
    <message>
        <source>Re-fetches all tasks from the Task Center.</source>
        <translation>重新从 Task Center 拉取全部任务。</translation>
    </message>
    <message>
        <source>Clear Finished...</source>
        <translation>清空已完成…</translation>
    </message>
    <message>
        <source>Clears all finished/failed/cancelled tasks (a confirmation pops up).</source>
        <translation>清除所有已完成/失败/已取消的任务（会弹出确认）。</translation>
    </message>
    <message>
        <source>About the Task Center...</source>
        <translation>任务中心说明…</translation>
    </message>
    <message>
        <source>View the Task Center feature description.</source>
        <translation>查看任务中心的功能说明。</translation>
    </message>
    <message>
        <source>Currently: %1 active / %2 finished (Task Center total %3)</source>
        <translation>当前：%1 个活动 / %2 个已结束（Task Center 共 %3）</translation>
    </message>
    <message>
        <source>View Details</source>
        <translation>查看详情</translation>
    </message>
    <message>
        <source>Shows method, parameters, inputs/outputs and results on the details page to the right.</source>
        <translation>在右侧详情页显示方法、参数、输入输出与结果。</translation>
    </message>
    <message>
        <source>View Log</source>
        <translation>查看日志</translation>
    </message>
    <message>
        <source>Shows the run log on the Log page on the right.</source>
        <translation>在右侧日志页显示运行日志。</translation>
    </message>
    <message>
        <source>Stop / Cancel...</source>
        <translation>停止 / 取消…</translation>
    </message>
    <message>
        <source>Abort queued / running / paused tasks (a confirmation pops up).</source>
        <translation>中止排队/运行中/已暂停的任务（会弹出确认）。</translation>
    </message>
    <message>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <source>Pauses running tasks; they can be resumed later.</source>
        <translation>暂停运行中的任务，可稍后恢复。</translation>
    </message>
    <message>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <source>Resumes paused tasks.</source>
        <translation>恢复已暂停的任务。</translation>
    </message>
    <message>
        <source>Retry...</source>
        <translation>重试…</translation>
    </message>
    <message>
        <source>Retry Task</source>
        <translation>重试任务</translation>
    </message>
    <message>
        <source>Resubmit task '%1'?
A new task will be created with the same parameters.</source>
        <translation>重新提交任务「%1」？
将以相同参数创建一个新任务。</translation>
    </message>
    <message>
        <source>Resubmits failed/cancelled tasks with the same parameters (a confirmation pops up).</source>
        <translation>以相同参数重新提交失败/已取消的任务（会弹出确认）。</translation>
    </message>
    <message>
        <source>Load Output to Main View</source>
        <translation>加载输出到主图</translation>
    </message>
    <message>
        <source>No loadable output file found.</source>
        <translation>未找到可加载的输出文件。</translation>
    </message>
    <message>
        <source>Loads task output paths into the main program layers (successful tasks only).</source>
        <translation>将任务输出路径加载到主程序图层（仅成功任务）。</translation>
    </message>
    <message>
        <source>Load to Main View on Success</source>
        <translation>成功后加载到主图</translation>
    </message>
    <message>
        <source>When ticked, outputs are loaded into the main program automatically on task success.</source>
        <translation>勾选后任务成功时自动把输出加载到主程序。</translation>
    </message>
    <message>
        <source>Copy Task ID</source>
        <translation>复制任务 ID</translation>
    </message>
    <message>
        <source>Copies the task ID to the clipboard.</source>
        <translation>把任务 ID 复制到剪贴板。</translation>
    </message>
    <message>
        <source>Copy Method ID</source>
        <translation>复制方法 ID</translation>
    </message>
    <message>
        <source>Copies the method algorithmId to the clipboard.</source>
        <translation>把方法 algorithmId 复制到剪贴板。</translation>
    </message>
    <message>
        <source>Copy Parameters JSON</source>
        <translation>复制参数 JSON</translation>
    </message>
    <message>
        <source>Copies the formatted parameters JSON to the clipboard.</source>
        <translation>把格式化后的参数 JSON 复制到剪贴板。</translation>
    </message>
    <message>
        <source>Copy Result JSON</source>
        <translation>复制结果 JSON</translation>
    </message>
    <message>
        <source>Copies the formatted result JSON to the clipboard.</source>
        <translation>把格式化后的结果 JSON 复制到剪贴板。</translation>
    </message>
    <message>
        <source>Copy Full Details</source>
        <translation>复制详情全文</translation>
    </message>
    <message>
        <source>Copies the full details page to the clipboard.</source>
        <translation>把详情页全文复制到剪贴板。</translation>
    </message>
    <message>
        <source>Remove from List</source>
        <translation>从列表移除</translation>
    </message>
    <message>
        <source>Removes the row from this list only (Task Center records are kept; they reappear after refresh).</source>
        <translation>仅从列表移除该行（不清除 Task Center 记录；刷新后会重新出现）。</translation>
    </message>
</context>
<context>
    <name>RsMergeClassesDialog</name>
    <message>
        <source>Merge Classification Classes</source>
        <translation>合并分类类别</translation>
    </message>
    <message>
        <source>Merge Target Class Attributes</source>
        <translation>合并目标类别属性</translation>
    </message>
    <message>
        <source>ID of the merged class (always the smallest ID of the selected source classes)</source>
        <translation>合并后新类别的 ID（固定取所选源类别的最小 ID）</translation>
    </message>
    <message>
        <source>Target ID</source>
        <translation>目标 ID</translation>
    </message>
    <message>
        <source>Display name of the merged class</source>
        <translation>合并后新类别的显示名称</translation>
    </message>
    <message>
        <source>Target Name</source>
        <translation>目标名称</translation>
    </message>
    <message>
        <source>Click to choose the display color of the merged class on the map and in the class table</source>
        <translation>点击选择合并后新类别在地图与分类表中的显示颜色</translation>
    </message>
    <message>
        <source>Target Color</source>
        <translation>目标颜色</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens the help for this dialog.</source>
        <translation>打开本对话框的帮助说明。</translation>
    </message>
    <message>
        <source>Merge the following classes: %1</source>
        <translation>合并以下类别: %1</translation>
    </message>
    <message>
        <source> (auto)</source>
        <translation> (自动)</translation>
    </message>
    <message>
        <source>Choose Target Color</source>
        <translation>选择目标颜色</translation>
    </message>
</context>
<context>
    <name>RsObiaMainWindow</name>
    <message>
        <source>OBIA — Object-Based Classification</source>
        <translation>OBIA — 面向对象分类</translation>
    </message>
    <message>
        <source>Map canvas: shows the image, segmentation boundaries and classification results. Use the 'Select Objects' tool to inspect/label objects.</source>
        <translation>地图画布：显示影像、分割边界与分类结果。用「选择对象」工具点选对象以查看/赋类。</translation>
    </message>
    <message>
        <source>OBIA</source>
        <translation>OBIA</translation>
    </message>
    <message>
        <source>Load Raster</source>
        <translation>加载栅格</translation>
    </message>
    <message>
        <source>Load the raster image to be segmented / classified.</source>
        <translation>加载待分割/分类的栅格影像。</translation>
    </message>
    <message>
        <source> Segments:</source>
        <translation> 图斑：</translation>
    </message>
    <message>
        <source>Smoothing kernel size (rs:obia_segment.smoothKernel, odd; also used as the OTB spatialRadius). Larger values give coarser boundaries and fewer small patches.</source>
        <translation>平滑核大小（rs:obia_segment.smoothKernel，奇数；同时用作 OTB spatialRadius）。越大对象边界越粗、碎斑越少。</translation>
    </message>
    <message>
        <source>Quantization levels (rs:obia_segment.quantizeBins, built-in segmentation fallback). More levels mean more detail and finer objects.</source>
        <translation>量化级数（rs:obia_segment.quantizeBins，内置分割回退）。级数多则细节多、对象更碎。</translation>
    </message>
    <message>
        <source>OTB MeanShift spectral radius (rs:obia_segment.rangeRadius, metres).</source>
        <translation>OTB MeanShift 光谱半径（rs:obia_segment.rangeRadius，米）。</translation>
    </message>
    <message>
        <source>Minimum object pixel count (rs:obia_segment.minRegionSize). Regions below it are merged, suppressing small patches.</source>
        <translation>最小对象像元数（rs:obia_segment.minRegionSize）。小于此值的区域会被合并，抑制碎斑。</translation>
    </message>
    <message>
        <source>Segment</source>
        <translation>分割</translation>
    </message>
    <message>
        <source>Runs single-level image segmentation (rs:obia_segment; prefers OTB MeanShift, falls back to the built-in segmentation).</source>
        <translation>运行单层影像分割（rs:obia_segment，优先 OTB MeanShift，缺失时内置分割回退）。</translation>
    </message>
    <message>
        <source>Hierarchy</source>
        <translation>分级分割</translation>
    </message>
    <message>
        <source>Two-level hierarchical segmentation: fine MeanShift + coarse Watershed + parent links (rs:obia_hierarchy; needs OTB).</source>
        <translation>两层层次分割：细层 MeanShift + 粗层 Watershed + 父链接（rs:obia_hierarchy，需 OTB）。</translation>
    </message>
    <message>
        <source> View L:</source>
        <translation> 视图层级：</translation>
    </message>
    <message>
        <source>Active display level (0 = finest). Object ids are numbered per level.</source>
        <translation>活动显示层级（0=最细）。对象 id 按层独立编号。</translation>
    </message>
    <message>
        <source> Classifier:</source>
        <translation> 分类器：</translation>
    </message>
    <message>
        <source>Object-level classifier (rs:obia_classify.method): Normal Bayes / SVM / Random Forest / K-means / MLP.</source>
        <translation>对象级分类器（rs:obia_classify.method）：NormalBayes / SVM / RandomForest / KMeans / MLP。</translation>
    </message>
    <message>
        <source> Cls L:</source>
        <translation> 分类层级：</translation>
    </message>
    <message>
        <source>Classification level, 0 (finest) by default. Training labels bind to objects of this level.</source>
        <translation>分类层级，默认 0（最细）。训练标签绑定该层对象。</translation>
    </message>
    <message>
        <source>Classify</source>
        <translation>分类</translation>
    </message>
    <message>
        <source>Classifies objects at the selected level (rs:obia_classify / rs:obia_hierarchy).</source>
        <translation>对所选层级的对象进行分类（rs:obia_classify / rs:obia_hierarchy）。</translation>
    </message>
    <message>
        <source>Params</source>
        <translation>参数</translation>
    </message>
    <message>
        <source>Configure hyperparameters of the selected classifier (rfNumTrees / mlpHiddenLayerSize of rs:obia_classify, etc.).</source>
        <translation>配置所选分类器的超参数（rs:obia_classify 的 rfNumTrees / mlpHiddenLayerSize 等）。</translation>
    </message>
    <message>
        <source>Import ROI</source>
        <translation>导入 ROI</translation>
    </message>
    <message>
        <source>Labels objects from training polygons by majority vote (rs:obia_label); click labels written later override and are reported.</source>
        <translation>从训练多边形按多数票标注对象（rs:obia_label）；与点击标注冲突时后写覆盖并提示。</translation>
    </message>
    <message>
        <source>Consolidate</source>
        <translation>层级归并</translation>
    </message>
    <message>
        <source>Resolves classification conflicts across scale levels (bottom-up majority vote / top-down integration).</source>
        <translation>消解多尺度层次之间的分类矛盾（向上多数票投票 / 向下集成）。</translation>
    </message>
    <message>
        <source>Accuracy Assessment</source>
        <translation>精度评价</translation>
    </message>
    <message>
        <source>View the training-sample accuracy of the last object classification (confusion matrix / OA / Kappa).</source>
        <translation>查看最近一次对象分类的训练样本精度（混淆矩阵 / OA / Kappa）。</translation>
    </message>
    <message>
        <source>Load to Main View</source>
        <translation>加载到主图</translation>
    </message>
    <message>
        <source>Loads the classification result raster into the main window map.</source>
        <translation>将分类结果栅格加载到主窗口地图。</translation>
    </message>
    <message>
        <source>Export</source>
        <translation>导出</translation>
    </message>
    <message>
        <source>Exports the classification raster (with optional gdal:polygonize vectorization).</source>
        <translation>导出分类栅格（及可选矢量化 gdal:polygonize）。</translation>
    </message>
    <message>
        <source>Classes</source>
        <translation>类别</translation>
    </message>
    <message>
        <source>Class table: ID, name and display color; used for the object classification legend and labeling.</source>
        <translation>类别表：ID、名称、显示颜色。用于对象分类图例与标注。</translation>
    </message>
    <message>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Color</source>
        <translation>色</translation>
    </message>
    <message>
        <source>Class definitions. IDs correspond to classification raster pixel values. Right-click to edit name/color or insert/delete classes.</source>
        <translation>类别定义。ID 对应分类栅格像元值。右键可编辑名称/颜色、插入/删除类别。</translation>
    </message>
    <message>
        <source>Assign to Selected Segment</source>
        <translation>赋给选中图斑</translation>
    </message>
    <message>
        <source>Assigns the current class (selected row) to the objects selected on the canvas.</source>
        <translation>把当前类别（选中行）赋给画布上选中的对象。</translation>
    </message>
    <message>
        <source>Object info: shape, spectral and hierarchy statistics of the selected object (read-only).</source>
        <translation>对象信息：选中对象的形状、光谱与层级统计（只读）。</translation>
    </message>
    <message>
        <source>Segments</source>
        <translation>图斑</translation>
    </message>
    <message>
        <source>Object list: all segmented objects at the current level. Right-click to assign a class, view info or copy the ID.</source>
        <translation>对象列表：当前层级所有分割对象。右键可赋类、查看信息、复制 ID。</translation>
    </message>
    <message>
        <source>Pixels</source>
        <translation>像素</translation>
    </message>
    <message>
        <source>Class</source>
        <translation>类别</translation>
    </message>
    <message>
        <source>Object list (ID / pixel count / class). Right-click to assign the current class, view info or copy the ID.</source>
        <translation>对象列表（ID/像元数/类别）。右键赋为当前类别、查看信息或复制 ID。</translation>
    </message>
    <message>
        <source>Uncertainty Candidates (Active Learning)</source>
        <translation>不确定性候选（主动学习）</translation>
    </message>
    <message>
        <source>Seg ID</source>
        <translation>图斑 ID</translation>
    </message>
    <message>
        <source>Entropy (H)</source>
        <translation>信息熵 (H)</translation>
    </message>
    <message>
        <source>Predicted Class</source>
        <translation>预测类别</translation>
    </message>
    <message>
        <source>Feature Tree</source>
        <translation>特征树</translation>
    </message>
    <message>
        <source>Spectral Features</source>
        <translation>光谱特征</translation>
    </message>
    <message>
        <source>Band Mean</source>
        <translation>波段均值</translation>
    </message>
    <message>
        <source>Band StdDev</source>
        <translation>波段标准差</translation>
    </message>
    <message>
        <source>Band Min</source>
        <translation>波段最小值</translation>
    </message>
    <message>
        <source>Band Max</source>
        <translation>波段最大值</translation>
    </message>
    <message>
        <source>Texture Features (GLCM)</source>
        <translation>纹理特征（GLCM）</translation>
    </message>
    <message>
        <source>GLCM Contrast</source>
        <translation>GLCM 对比度</translation>
    </message>
    <message>
        <source>GLCM Correlation</source>
        <translation>GLCM 相关性</translation>
    </message>
    <message>
        <source>GLCM Energy</source>
        <translation>GLCM 能量</translation>
    </message>
    <message>
        <source>GLCM Homogeneity</source>
        <translation>GLCM 同质性</translation>
    </message>
    <message>
        <source>Shape Features</source>
        <translation>形状特征</translation>
    </message>
    <message>
        <source>Area (pixel area)</source>
        <translation>面积（像元数）</translation>
    </message>
    <message>
        <source>Perimeter</source>
        <translation>周长</translation>
    </message>
    <message>
        <source>Shape Index</source>
        <translation>形状指数</translation>
    </message>
    <message>
        <source>Compactness</source>
        <translation>紧凑度</translation>
    </message>
    <message>
        <source>Rectangularity</source>
        <translation>矩形度</translation>
    </message>
    <message>
        <source>Aspect Ratio</source>
        <translation>长宽比</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Cannot open raster: %1</source>
        <translation>无法打开栅格：%1</translation>
    </message>
    <message>
        <source>Loading a new image will clear the current segmentation and classification results. Continue?</source>
        <translation>加载新影像将清除当前分割与分类结果，是否继续？</translation>
    </message>
    <message>
        <source>Loaded: %1 (%2 bands)</source>
        <translation>已加载：%1（%2 个波段）</translation>
    </message>
    <message>
        <source>Open Raster</source>
        <translation>打开栅格</translation>
    </message>
    <message>
        <source>Raster files (*.tif *.tiff *.img *.jp2 *.png);;All files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.jp2 *.png);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Load a raster first.</source>
        <translation>请先加载栅格。</translation>
    </message>
    <message>
        <source>An OBIA task is already running.</source>
        <translation>已有 OBIA 任务在运行。</translation>
    </message>
    <message>
        <source>Segmenting (rs:obia_segment, engine=%1)...</source>
        <translation>分割中（rs:obia_segment，引擎=%1）...</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>OBIA segmentation</source>
        <translation>OBIA 分割</translation>
    </message>
    <message>
        <source>Task rejected by the Task Center (%1)</source>
        <translation>任务被任务中心拒绝（%1）</translation>
    </message>
    <message>
        <source>Invalid classification raster: %1</source>
        <translation>分类栅格无效：%1</translation>
    </message>
    <message>
        <source>No accuracy results yet. Finish object classification first (training accuracy is computed from labeled objects).</source>
        <translation>尚无精度结果。请先完成对象分类（基于已标注对象计算训练精度）。</translation>
    </message>
    <message>
        <source>OBIA Accuracy Assessment (training samples)</source>
        <translation>OBIA 精度评价（训练样本）</translation>
    </message>
    <message>
        <source>No classification results yet. Run Classify first.</source>
        <translation>尚无分类结果。请先运行 Classify。</translation>
    </message>
    <message>
        <source>Requested to load the result into the main view: %1</source>
        <translation>已请求将结果加载到主图: %1</translation>
    </message>
    <message>
        <source>OBIA task cancelled</source>
        <translation>OBIA 任务已取消</translation>
    </message>
    <message>
        <source>OBIA task canceled</source>
        <translation>OBIA 任务已取消</translation>
    </message>
    <message>
        <source>Operator task failed (%1)</source>
        <translation>算子任务失败（%1）</translation>
    </message>
    <message>
        <source>Segmentation output is unreadable: %1</source>
        <translation>分割输出不可读：%1</translation>
    </message>
    <message>
        <source>Could not start object-feature extraction on the hierarchy labels (%1).</source>
        <translation>无法在分级标签上启动对象特征提取（%1）。</translation>
    </message>
    <message>
        <source>no fine labels raster</source>
        <translation>缺少细层级标签栅格</translation>
    </message>
    <message>
        <source>Classification produced no output raster.</source>
        <translation>分类未生成输出栅格。</translation>
    </message>
    <message>
        <source>No uncertainty sidecar (unsupervised method)</source>
        <translation>无不确定性伴随文件（非监督方法）</translation>
    </message>
    <message>
        <source>
OA=%1  Kappa=%2 (training samples)</source>
        <translation>
OA=%1  Kappa=%2 (训练样本)</translation>
    </message>
    <message>
        <source>OBIA Classification</source>
        <translation>OBIA 分类</translation>
    </message>
    <message>
        <source>Level %1 classification finished!
Output: %2%3</source>
        <translation>层级 %1 分类完成！
输出：%2%3</translation>
    </message>
    <message>
        <source>Object classification finished!
Output: %1%2</source>
        <translation>对象分类完成！
输出：%1%2</translation>
    </message>
    <message>
        <source>ROI labeling finished, but no object received a label (check CRS coverage and the class field).</source>
        <translation>ROI 标注完成，但没有对象获得标签（请检查 CRS 覆盖与类别字段）。</translation>
    </message>
    <message>
        <source>ROI majority (rs:obia_label): +%1 new, %2 overwritten (last write wins)</source>
        <translation>ROI 多数票标注（rs:obia_label）：新增 %1，覆盖 %2（后写优先）</translation>
    </message>
    <message>
        <source>Class raster: %1
Polygons: %2</source>
        <translation>分类栅格：%1
矢量面：%2</translation>
    </message>
    <message>
        <source>OTB MeanShift</source>
        <translation>OTB MeanShift</translation>
    </message>
    <message>
        <source>built-in segmenter</source>
        <translation>内置分割器</translation>
    </message>
    <message>
        <source>Segmentation complete (%1): %2 segments</source>
        <translation>分割完成（%1）：%2 个图斑</translation>
    </message>
    <message>
        <source>Segmentation</source>
        <translation>分割</translation>
    </message>
    <message>
        <source>Segmentation complete using %1: %2 segments</source>
        <translation>使用 %1 完成分割：%2 个图斑</translation>
    </message>
    <message>
        <source>Re-select level %1 after the current task finishes to extract features.</source>
        <translation>当前任务结束后请重新选择层级 %1 以提取特征。</translation>
    </message>
    <message>
        <source>Extracting object features (rs:obia_features)…</source>
        <translation>提取对象特征（rs:obia_features）…</translation>
    </message>
    <message>
        <source>OBIA object features</source>
        <translation>OBIA 对象特征</translation>
    </message>
    <message>
        <source>Level %1 features ready (%2 segments)</source>
        <translation>第 %1 层级特征就绪（%2 个图斑）</translation>
    </message>
    <message>
        <source>Hierarchy ready: %1 levels | viewing L%2 (%3 objects)</source>
        <translation>分级就绪：%1 个层级 | 正在查看第 %2 层（%3 个对象）</translation>
    </message>
    <message>
        <source>Hierarchical Segmentation</source>
        <translation>分级分割</translation>
    </message>
    <message>
        <source>Two-level hierarchy built.
Level 0 (fine): %1 objects
Level 1 (coarse): %2 objects
Select an object to inspect parent / childCount / areaRatio.
Segment ids are local per level.</source>
        <translation>已构建两级分级结构。
第 0 层（细）：%1 个对象
第 1 层（粗）：%2 个对象
选择对象可查看父级 / 子对象数 / 面积比。
图斑 ID 按层级独立编号。</translation>
    </message>
    <message>
        <source>OTB required</source>
        <translation>需要 OTB</translation>
    </message>
    <message>
        <source>Hierarchical OBIA requires OTB Segmentation CLI.
Set SICNU_OTB_PATH or install OTB.
No silent teaching fallback for the hierarchy path.</source>
        <translation>分级 OBIA 需要 OTB Segmentation 命令行工具。
请设置 SICNU_OTB_PATH 或安装 OTB。
分级路径不提供静默的教学降级实现。</translation>
    </message>
    <message>
        <source>Building 2-level hierarchy (rs:obia_hierarchy: MeanShift + Watershed)...</source>
        <translation>正在构建两级分级（rs:obia_hierarchy：MeanShift + Watershed）...</translation>
    </message>
    <message>
        <source>OBIA hierarchical segment</source>
        <translation>OBIA 分级分割</translation>
    </message>
    <message>
        <source>Viewing level %1 (%2 objects, ids are level-local)</source>
        <translation>正在查看第 %1 层（%2 个对象，ID 按层级独立）</translation>
    </message>
    <message>
        <source>Classify level set to %1 — labels cleared (ids are level-local)</source>
        <translation>分类层级已设为 %1 —— 标签已清空（ID 按层级独立）</translation>
    </message>
    <message>
        <source>Run segmentation or hierarchy first.</source>
        <translation>请先运行分割或分级分割。</translation>
    </message>
    <message>
        <source>Label at least 2 segments (click assign and/or Import ROI).</source>
        <translation>请至少标注 2 个图斑（点击赋类和/或导入 ROI）。</translation>
    </message>
    <message>
        <source>Save Classified Raster</source>
        <translation>保存分类栅格</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif *.tiff);;All files (*)</source>
        <translation>GeoTIFF (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Invalid classify level.</source>
        <translation>分类层级无效。</translation>
    </message>
    <message>
        <source>No segment label raster in this session (re-run Segment).</source>
        <translation>本会话没有图斑标签栅格（请重新运行分割）。</translation>
    </message>
    <message>
        <source>Classifying objects (%1)...</source>
        <translation>对象分类中（%1）...</translation>
    </message>
    <message>
        <source>OBIA classify: %1</source>
        <translation>OBIA 分类：%1</translation>
    </message>
    <message>
        <source>No segment label raster for level %1 (re-run segmentation).</source>
        <translation>第 %1 层没有图斑标签栅格（请重新分割）。</translation>
    </message>
    <message>
        <source>Level %1 features are still being extracted; re-run Import ROI once finished.</source>
        <translation>Level %1 特征仍在提取，完成后请重新执行 Import ROI。</translation>
    </message>
    <message>
        <source>Open training polygons</source>
        <translation>打开训练面文件</translation>
    </message>
    <message>
        <source>Vector (*.shp *.gpkg *.geojson);;All files (*)</source>
        <translation>矢量文件 (*.shp *.gpkg *.geojson);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Importing new ROIs will replace the %1 currently labeled objects. Continue?</source>
        <translation>导入新 ROI 将替换当前 %1 个已赋标签的对象，是否继续？</translation>
    </message>
    <message>
        <source>Labeling objects from ROI (rs:obia_label)…</source>
        <translation>由 ROI 标注对象（rs:obia_label）…</translation>
    </message>
    <message>
        <source>OBIA ROI labeling</source>
        <translation>OBIA ROI 标注</translation>
    </message>
    <message>
        <source>Run Classify first to write a class raster, then Export can polygonize it.</source>
        <translation>请先运行分类生成分类栅格，导出才能将其矢量化。</translation>
    </message>
    <message>
        <source>Export class polygons</source>
        <translation>导出类别矢量面</translation>
    </message>
    <message>
        <source>Shapefile (*.shp);;All files (*)</source>
        <translation>Shapefile (*.shp);;所有文件 (*)</translation>
    </message>
    <message>
        <source>OBIA export polygons</source>
        <translation>OBIA 导出矢量面</translation>
    </message>
    <message>
        <source>Selected L%1 segment %2 (pixels: %3)</source>
        <translation>已选中第 %1 层图斑 %2（像元数：%3）</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Switch View L to the classify level (%1) before assigning labels, or change Cls L to the active view level.</source>
        <translation>请先把视图层级切换到分类层级（%1）再赋标签，或将分类层级改为当前视图层级。</translation>
    </message>
    <message>
        <source>Segment %1 → Class %2 (overwrote previous label)</source>
        <translation>图斑 %1 → 类别 %2（覆盖了原标签）</translation>
    </message>
    <message>
        <source>Segment %1 → Class %2</source>
        <translation>图斑 %1 → 类别 %2</translation>
    </message>
    <message>
        <source>Click a segment on the map first.</source>
        <translation>请先在地图上点击一个图斑。</translation>
    </message>
    <message>
        <source>Edit Name...</source>
        <translation>编辑名称…</translation>
    </message>
    <message>
        <source>Changes the display name of this class.</source>
        <translation>修改该类别的显示名称。</translation>
    </message>
    <message>
        <source>Change Color...</source>
        <translation>更改颜色…</translation>
    </message>
    <message>
        <source>Opens the color palette to pick a new class color.</source>
        <translation>打开调色板选择新的类别颜色。</translation>
    </message>
    <message>
        <source>Insert Class After This...</source>
        <translation>在此之后插入类别…</translation>
    </message>
    <message>
        <source>Appends a new class (ID automatically set to the current maximum + 1).</source>
        <translation>追加一个新类别（ID 自动取当前最大值 +1）。</translation>
    </message>
    <message>
        <source>Delete Class...</source>
        <translation>删除类别…</translation>
    </message>
    <message>
        <source>Deletes this class (a confirmation pops up). Object labels already assigned are not cleared automatically.</source>
        <translation>删除该类别（会弹出确认）。已赋该类的对象标签不会自动清除。</translation>
    </message>
    <message>
        <source>Copy Class ID</source>
        <translation>复制类别 ID</translation>
    </message>
    <message>
        <source>Copies this class's ID to the clipboard.</source>
        <translation>把该类别的 ID 复制到剪贴板。</translation>
    </message>
    <message>
        <source>Edit Class Name</source>
        <translation>编辑类别名称</translation>
    </message>
    <message>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <source>Class %1 renamed to '%2'</source>
        <translation>类别 %1 已更名为「%2」</translation>
    </message>
    <message>
        <source>Choose Class Color</source>
        <translation>选择类别颜色</translation>
    </message>
    <message>
        <source>Class %1 color updated</source>
        <translation>类别 %1 颜色已更新</translation>
    </message>
    <message>
        <source>New Class Name</source>
        <translation>新类别名称</translation>
    </message>
    <message>
        <source>New class</source>
        <translation>新类别</translation>
    </message>
    <message>
        <source>Inserted class %1 '%2'</source>
        <translation>已插入类别 %1「%2」</translation>
    </message>
    <message>
        <source>Delete Class</source>
        <translation>删除类别</translation>
    </message>
    <message>
        <source>Delete class %1 '%2'?
Object labels already assigned to this class are not cleared automatically and can be reassigned.</source>
        <translation>确定删除类别 %1「%2」？
已赋该类的对象标签不会被自动清除，可重新赋类。</translation>
    </message>
    <message>
        <source>Deleted class %1</source>
        <translation>已删除类别 %1</translation>
    </message>
    <message>
        <source>Assign Current Class</source>
        <translation>赋为当前类别</translation>
    </message>
    <message>
        <source>Assigns the currently selected class (selected row in the Classes table) to this object.</source>
        <translation>把当前选中类别（Classes 表选中行）赋给该对象。</translation>
    </message>
    <message>
        <source>View Object Info</source>
        <translation>查看对象信息</translation>
    </message>
    <message>
        <source>Shows the object's shape/spectral/hierarchy statistics in the info panel below.</source>
        <translation>在下方信息面板显示该对象的形状/光谱/层级统计。</translation>
    </message>
    <message>
        <source>Copy Object ID</source>
        <translation>复制对象 ID</translation>
    </message>
    <message>
        <source>Copies this object's ID to the clipboard.</source>
        <translation>把该对象 ID 复制到剪贴板。</translation>
    </message>
    <message>
        <source>(objects become available after segmentation)</source>
        <translation>（先完成分割后才有对象可操作）</translation>
    </message>
    <message>
        <source>Switch View L to the classification level (%1) before assigning labels, or make Cls L the current view level.</source>
        <translation>切换 View L 到分类层级（%1）后再赋标签，或将 Cls L 设为当前视图层级。</translation>
    </message>
    <message>
        <source>Object %1 → class %2</source>
        <translation>对象 %1 → 类别 %2</translation>
    </message>
    <message>
        <source>-</source>
        <translation>-</translation>
    </message>
    <message>
        <source>Ready — Load a raster to begin</source>
        <translation>就绪——加载栅格开始</translation>
    </message>
    <message>
        <source>Hierarchy L%1/%2 | %3 objects | %4 labeled | classify L%5</source>
        <translation>分级 L%1/%2 | %3 个对象 | 已标注 %4 | 分类层级 L%5</translation>
    </message>
    <message>
        <source>%1 segments | %2 labeled</source>
        <translation>%1 个图斑 | 已标注 %2</translation>
    </message>
    <message>
        <source>MLP Config</source>
        <translation>MLP 配置</translation>
    </message>
    <message>
        <source>Hidden Layer Neuron Count (mlpHiddenLayerSize):</source>
        <translation>隐藏层神经元数（mlpHiddenLayerSize）：</translation>
    </message>
    <message>
        <source>Maximum Iterations (mlpMaxIter):</source>
        <translation>最大迭代次数（mlpMaxIter）：</translation>
    </message>
    <message>
        <source>RandomForest Config</source>
        <translation>随机森林配置</translation>
    </message>
    <message>
        <source>Number of Decision Trees (rfNumTrees):</source>
        <translation>决策树数量（rfNumTrees）：</translation>
    </message>
    <message>
        <source>Max Tree Depth (rfMaxDepth):</source>
        <translation>最大树深（rfMaxDepth）：</translation>
    </message>
    <message>
        <source>Min Sample Count per Node (rfMinSampleCount):</source>
        <translation>节点最小样本数（rfMinSampleCount）：</translation>
    </message>
    <message>
        <source>Classifier Config</source>
        <translation>分类器配置</translation>
    </message>
    <message>
        <source>No configurable hyperparameters for selected classifier backend.</source>
        <translation>所选分类器后端没有可配置的超参数。</translation>
    </message>
    <message>
        <source>Hierarchy Consolidation</source>
        <translation>分级类别归并</translation>
    </message>
    <message>
        <source>Hierarchy consolidation requires a multi-level RsObjectHierarchy structure.</source>
        <translation>分级归并需要多层级 RsObjectHierarchy 结构。</translation>
    </message>
    <message>
        <source>Bottom-Up Majority Vote (parent decided by child majority)</source>
        <translation>自底向上多数投票（由子级多数票决定父级）</translation>
    </message>
    <message>
        <source>Area-Weighted Vote (parent decided by child pixel area)</source>
        <translation>面积加权投票（由子级像元面积加权决定父级）</translation>
    </message>
    <message>
        <source>Top-Down Inheritance (parent class inherited directly)</source>
        <translation>自顶向下继承（父级类别直接向下传递）</translation>
    </message>
    <message>
        <source>Hierarchy Class Consolidator</source>
        <translation>分级类别归并器</translation>
    </message>
    <message>
        <source>Select Consolidation Strategy:</source>
        <translation>选择归并策略：</translation>
    </message>
    <message>
        <source>Area-Weighted</source>
        <translation>面积加权</translation>
    </message>
    <message>
        <source>Top-Down</source>
        <translation>自顶向下</translation>
    </message>
    <message>
        <source>Consolidation finished successfully. Class labels updated across hierarchy levels.</source>
        <translation>归并完成，各级层级的类别标签已更新。</translation>
    </message>
</context>
<context>
    <name>RsPostProcessDialog</name>
    <message>
        <source>Sieve Small-Region Removal</source>
        <translation>Sieve 小斑去除</translation>
    </message>
    <message>
        <source>Majority Filter</source>
        <translation>多数滤波</translation>
    </message>
    <message>
        <source>Clump Connected-Component Labeling</source>
        <translation>Clump 连通域标记</translation>
    </message>
    <message>
        <source>Recode</source>
        <translation>重编码</translation>
    </message>
    <message>
        <source>Polygonize</source>
        <translation>矢量化 Polygonize</translation>
    </message>
    <message>
        <source>Post-Processing</source>
        <translation>后处理</translation>
    </message>
    <message>
        <source>Removes connected patches smaller than the threshold and fills them with the neighbourhood majority class.</source>
        <translation>去除面积小于阈值的连通斑块，并用邻域多数类填充。</translation>
    </message>
    <message>
        <source>Sliding-window majority filter that smooths classification boundaries (kernel size must be odd).</source>
        <translation>滑动窗口众数滤波，平滑分类边界（核大小须为奇数）。</translation>
    </message>
    <message>
        <source>Labels connected regions of like pixels and outputs a patch-id raster.</source>
        <translation>对同类像元做连通域标记，输出斑块编号栅格。</translation>
    </message>
    <message>
        <source>Remaps class ids by the 'old class → new class' table; unlisted classes stay unchanged.</source>
        <translation>按「旧类 → 新类」表重映射类别编号；未列出的类保持不变。</translation>
    </message>
    <message>
        <source>Vectorizes a classification / label raster into polygon features (.gpkg or .shp).</source>
        <translation>将分类/标签栅格矢量化为面要素（.gpkg 或 .shp）。</translation>
    </message>
    <message>
        <source>Input and Output Data</source>
        <translation>输入与输出数据</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Choose the Output File Save Path</source>
        <translation>选择输出文件保存路径</translation>
    </message>
    <message>
        <source>Select Input File Path</source>
        <translation>选择输入文件路径</translation>
    </message>
    <message>
        <source>Classification / label raster</source>
        <translation>分类/标签栅格</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif *.tiff);;All files (*)</source>
        <translation>GeoTIFF (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Input classification or label raster file for post-processing</source>
        <translation>待进行后处理的输入分类或标签栅格文件</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Output .gpkg / .shp</source>
        <translation>输出 .gpkg / .shp</translation>
    </message>
    <message>
        <source>Output GeoTIFF</source>
        <translation>输出 GeoTIFF</translation>
    </message>
    <message>
        <source>GeoPackage (*.gpkg);;ESRI Shapefile (*.shp)</source>
        <translation>GeoPackage (*.gpkg);;ESRI Shapefile (*.shp)</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif)</source>
        <translation>GeoTIFF (*.tif)</translation>
    </message>
    <message>
        <source>Vectorized output file path (*.gpkg or *.shp)</source>
        <translation>矢量化输出文件路径 (*.gpkg 或 *.shp)</translation>
    </message>
    <message>
        <source>Post-processing result raster output path (*.tif)</source>
        <translation>后处理结果栅格输出路径 (*.tif)</translation>
    </message>
    <message>
        <source>Output Vector</source>
        <translation>输出矢量</translation>
    </message>
    <message>
        <source>Output Raster</source>
        <translation>输出栅格</translation>
    </message>
    <message>
        <source>Algorithm Control Parameters</source>
        <translation>算法控制参数</translation>
    </message>
    <message>
        <source>Area threshold (pixels): connected patches below this pixel count are filtered out and filled from the neighbourhood</source>
        <translation>面积阈值（像元数）：小于该像元数的碎小连通斑块将被滤除并由邻域填充</translation>
    </message>
    <message>
        <source>Area Threshold (pixels)</source>
        <translation>面积阈值 (像元)</translation>
    </message>
    <message>
        <source>Pixel connectivity: 4-connected (edge neighbours) or 8-connected (incl. diagonals)</source>
        <translation>像素连通性：4 连通（上下左右）或 8 连通（含对角线）</translation>
    </message>
    <message>
        <source>Connectivity (4/8)</source>
        <translation>连通性 (4/8)</translation>
    </message>
    <message>
        <source>Majority filter window size (odd 3/5/7); larger values smooth more</source>
        <translation>众数滤波窗口边长（奇数 3/5/7），越大平滑强度越高</translation>
    </message>
    <message>
        <source>Kernel Size (odd)</source>
        <translation>核大小 (奇数)</translation>
    </message>
    <message>
        <source>Connected-region rule: 4-connectivity or 8-connectivity</source>
        <translation>连通域判定方式：4 连通或 8 连通</translation>
    </message>
    <message>
        <source>Old Class</source>
        <translation>旧类</translation>
    </message>
    <message>
        <source>New Class</source>
        <translation>新类</translation>
    </message>
    <message>
        <source>Mapping table from old class IDs to new class IDs</source>
        <translation>旧类别 ID 到新类别 ID 的映射对照表</translation>
    </message>
    <message>
        <source>Recoding Table</source>
        <translation>重编码对照表</translation>
    </message>
    <message>
        <source>Load results into the classification window layer management when finished</source>
        <translation>完成后加载结果到分类窗口图层管理</translation>
    </message>
    <message>
        <source>Ticked by default: result rasters / vectors join this classification window's layer tree on the left instead of only being written to files.</source>
        <translation>默认勾选：结果栅格/矢量加入本分类窗口左侧图层树，而非仅写文件。</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens the help for this dialog.</source>
        <translation>打开本对话框的帮助说明。</translation>
    </message>
    <message>
        <source>Please specify the input raster path</source>
        <translation>请指定输入栅格路径</translation>
    </message>
    <message>
        <source>Input file does not exist: %1</source>
        <translation>输入文件不存在：%1</translation>
    </message>
    <message>
        <source>Fill in at least one old class → new class row in the recoding table</source>
        <translation>请在重编码表中至少填写一行旧类→新类</translation>
    </message>
</context>
<context>
    <name>RsPostProcessTask</name>
    <message>
        <source>Post-processing %1</source>
        <translation>后处理 %1</translation>
    </message>
</context>
<context>
    <name>RsResultSummary</name>
    <message>
        <source>View Raw JSON</source>
        <translation>查看原始 JSON</translation>
    </message>
</context>
<context>
    <name>RsRoiSpectrumTool</name>
    <message>
        <source>An ROI needs at least 3 points and a valid raster layer.</source>
        <translation>ROI 需要至少 3 个点且栅格图层有效。</translation>
    </message>
</context>
<context>
    <name>RsSegmentInfoDock</name>
    <message>
        <source>Segment Info</source>
        <translation>图斑信息</translation>
    </message>
    <message>
        <source>Segment</source>
        <translation>分割</translation>
    </message>
    <message>
        <source>Class:</source>
        <translation>类别：</translation>
    </message>
    <message>
        <source>Shape:</source>
        <translation>形状：</translation>
    </message>
    <message>
        <source>Area (pixels)</source>
        <translation>面积（像元数）</translation>
    </message>
    <message>
        <source>Perimeter</source>
        <translation>周长</translation>
    </message>
    <message>
        <source>Shape Index</source>
        <translation>形状指数</translation>
    </message>
    <message>
        <source>Spectral (per band):</source>
        <translation>光谱（逐波段）：</translation>
    </message>
    <message>
        <source>Hierarchy (ids are level-local):</source>
        <translation>分级（ID 按层级独立）：</translation>
    </message>
    <message>
        <source>Level</source>
        <translation>层级</translation>
    </message>
    <message>
        <source>Parent</source>
        <translation>父级</translation>
    </message>
    <message>
        <source>none (orphan / coarsest)</source>
        <translation>无（顶层/最粗层级）</translation>
    </message>
    <message>
        <source>Parent id</source>
        <translation>父级 ID</translation>
    </message>
    <message>
        <source>Child count</source>
        <translation>子对象数</translation>
    </message>
    <message>
        <source>Area ratio to parent</source>
        <translation>与父级面积比</translation>
    </message>
</context>
<context>
    <name>RsSiftDialog</name>
    <message>
        <source>SIFT Auto-Matching Parameters</source>
        <translation>SIFT 自动匹配参数</translation>
    </message>
    <message>
        <source>Feature contrast threshold; larger values give fewer but steadier points.</source>
        <translation>特征对比度阈值，越大点越少越稳。</translation>
    </message>
    <message>
        <source>Maximum number of matched pairs.</source>
        <translation>最大匹配对数。</translation>
    </message>
    <message>
        <source>Minimum RANSAC inlier ratio.</source>
        <translation>RANSAC 内点比例下限。</translation>
    </message>
    <message>
        <source>RANSAC pixel tolerance.</source>
        <translation>RANSAC 像素容差。</translation>
    </message>
    <message>
        <source>Maximum edge length before matching (speed-up).</source>
        <translation>匹配前最大边长（加速）。</translation>
    </message>
    <message>
        <source>SIFT Auto Matching</source>
        <translation>SIFT 自动匹配</translation>
    </message>
    <message>
        <source>SIFT Feature Extraction and Matching Parameters</source>
        <translation>SIFT 特征提取与匹配参数</translation>
    </message>
    <message>
        <source>Contrast Threshold</source>
        <translation>对比度阈值</translation>
    </message>
    <message>
        <source>Maximum Matches</source>
        <translation>最多匹配数</translation>
    </message>
    <message>
        <source>Minimum Inlier Ratio</source>
        <translation>最小内点比</translation>
    </message>
    <message>
        <source>RANSAC Tolerance</source>
        <translation>RANSAC 容差</translation>
    </message>
    <message>
        <source>Maximum Edge Length</source>
        <translation>最大边长</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
</context>
<context>
    <name>RsSiftTask</name>
    <message>
        <source>SIFT matching %1</source>
        <translation>SIFT 匹配 %1</translation>
    </message>
</context>
<context>
    <name>RsSpectralCurveWidget</name>
    <message>
        <source>Select a class to view spectral curves</source>
        <translation>选择类别以查看光谱曲线</translation>
    </message>
</context>
<context>
    <name>RsTemplateMatchDialog</name>
    <message>
        <source>Template Matching (around the initial coordinates)</source>
        <translation>模板匹配（基于初始坐标）</translation>
    </message>
    <message>
        <source>Applies when the source image already has approximate geocoordinates: the GeoTransform predicts the search area on the reference image,Then template correlation matching runs — steadier and more controllable than SIFT.</source>
        <translation>适用于源影像已有近似地理坐标的情况：使用 GeoTransform 预测参考影像搜索区，再做模板相关匹配，比 SIFT 更稳健、更可控。</translation>
    </message>
    <message>
        <source>Template Correlation Matching Parameters</source>
        <translation>模板相关匹配参数</translation>
    </message>
    <message>
        <source>Regular grid (search areas predicted from the SRC initial georeference)</source>
        <translation>规则网格（用 SRC 初始地理参考预测搜索区）</translation>
    </message>
    <message>
        <source>Existing GCPs as seeds (refinement)</source>
        <translation>现有 GCP 作为种子（精化）</translation>
    </message>
    <message>
        <source>Seed generation mode: a regular grid spread evenly over the scene, or local fine-tuning refinement around existing GCP coordinates</source>
        <translation>种子生成模式：规则网格在整景均匀分布，或基于现有 GCP 坐标做局部微调精化</translation>
    </message>
    <message>
        <source>Seed Mode</source>
        <translation>种子模式</translation>
    </message>
    <message>
        <source>Template side length cut from the source image (pixels; odd values recommended)</source>
        <translation>从源影像裁切的模板边长（像素，建议奇数）</translation>
    </message>
    <message>
        <source>Template Size</source>
        <translation>模板大小</translation>
    </message>
    <message>
        <source>Search half-width around the predicted position on the reference image (pixels)</source>
        <translation>在参考影像上，以初始坐标预测位置为中心的搜索半宽（像素）</translation>
    </message>
    <message>
        <source>Search Radius (px)</source>
        <translation>搜索半径 (px)</translation>
    </message>
    <message>
        <source>Minimum normalized cross-correlation coefficient (TM_CCOEFF_NORMED)</source>
        <translation>归一化互相关系数下限（TM_CCOEFF_NORMED）</translation>
    </message>
    <message>
        <source>Minimum Correlation Score</source>
        <translation>最小相关分</translation>
    </message>
    <message>
        <source>Regular grid rows (number of seed points sampled vertically)</source>
        <translation>规则网格行数（沿垂直方向采样的种子点数量）</translation>
    </message>
    <message>
        <source>Grid Rows</source>
        <translation>网格行数</translation>
    </message>
    <message>
        <source>Regular grid columns (number of seed points sampled horizontally)</source>
        <translation>规则网格列数（沿水平方向采样的种子点数量）</translation>
    </message>
    <message>
        <source>Grid Columns</source>
        <translation>网格列数</translation>
    </message>
    <message>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens the help for this dialog.</source>
        <translation>打开本对话框的帮助说明。</translation>
    </message>
</context>
<context>
    <name>RsToolbarFlowHost</name>
    <message>
        <source>Drag to arrange toolbars (place them on the same row or a second row)</source>
        <translation>拖动以排列工具栏（可放到同一行或第二行）</translation>
    </message>
    <message>
        <source>Drag to resize the toolbar and reveal more icons</source>
        <translation>拖动以调整工具栏长度，显示更多图标</translation>
    </message>
</context>
<context>
    <name>RsWarpTask</name>
    <message>
        <source>Warping %1</source>
        <translation>重采样校正 %1</translation>
    </message>
</context>
<context>
    <name>SchemaFormBuilder</name>
    <message>
        <source>Unit: %1</source>
        <translation>单位：%1</translation>
    </message>
    <message>
        <source>Recommended: %1</source>
        <translation>推荐值：%1</translation>
    </message>
    <message>
        <source>Trade-offs: %1</source>
        <translation>权衡：%1</translation>
    </message>
    <message>
        <source>Select Data Assets...</source>
        <translation>选择数据资产…</translation>
    </message>
    <message>
        <source>Select Model...</source>
        <translation>选择模型…</translation>
    </message>
    <message>
        <source>{ ... } JSON object</source>
        <translation>{ \u2026 } JSON 对象</translation>
    </message>
    <message>
        <source>…</source>
        <translation>…</translation>
    </message>
    <message>
        <source>Choose Color</source>
        <translation>选择颜色</translation>
    </message>
    <message>
        <source>Output Path...</source>
        <translation>输出路径…</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Select Output File</source>
        <translation>选择输出文件</translation>
    </message>
    <message>
        <source>GeoTIFF (*.tif *.tiff);;All Files (*)</source>
        <translation>GeoTIFF (*.tif *.tiff);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Separate multiple values with commas / semicolons / line breaks</source>
        <translation>多个值用逗号/分号/换行分隔</translation>
    </message>
    <message>
        <source>Array parameter: separate multiple values with commas, semicolons or line breaks</source>
        <translation>数组参数：多个值用逗号、分号或换行分隔</translation>
    </message>
    <message>
        <source>Add an Item</source>
        <translation>添加一项</translation>
    </message>
    <message>
        <source>Item %1</source>
        <translation>项 %1</translation>
    </message>
    <message>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <source>Remove this array item</source>
        <translation>移除该数组项</translation>
    </message>
    <message>
        <source>At most %1 items</source>
        <translation>最多 %1 项</translation>
    </message>
    <message>
        <source>⚠ Data has %1 items; loading only the first %2 (editing limit exceeded)</source>
        <translation>⚠ 数据包含 %1 项，仅加载前 %2 项（超出编辑上限）</translation>
    </message>
    <message>
        <source>Inputs</source>
        <translation>输入</translation>
    </message>
    <message>
        <source>Outputs</source>
        <translation>输出</translation>
    </message>
    <message>
        <source>Parameters</source>
        <translation>参数</translation>
    </message>
    <message>
        <source>Advanced</source>
        <translation>高级</translation>
    </message>
    <message>
        <source>⚠ Dynamic option source %1 is unavailable; free-form input allowed</source>
        <translation>⚠ 动态选项源“%1”暂不可用，可自由输入</translation>
    </message>
    <message>
        <source>Required parameter %1 must not be empty</source>
        <translation>必填参数“%1”不能为空</translation>
    </message>
    <message>
        <source>Required parameter %1 has not been chosen</source>
        <translation>必填参数“%1”未选择</translation>
    </message>
    <message>
        <source>%1 is not a valid color (#RRGGBB)</source>
        <translation>“%1”不是有效颜色（#RRGGBB）</translation>
    </message>
    <message>
        <source>%1 is not valid JSON: %2</source>
        <translation>“%1”不是有效 JSON：%2</translation>
    </message>
    <message>
        <source>%1 requires at least %2 values</source>
        <translation>“%1”至少需要 %2 个值</translation>
    </message>
    <message>
        <source>%1 requires at least %2 items</source>
        <translation>“%1”至少需要 %2 项</translation>
    </message>
    <message>
        <source>%1 allows at most %2 items</source>
        <translation>“%1”最多允许 %2 项</translation>
    </message>
    <message>
        <source>%1 is below the suggested minimum %2 (scientific-plausibility warning)</source>
        <translation>“%1”低于建议下限 %2（科学合理性警告）</translation>
    </message>
    <message>
        <source>%1 is above the suggested maximum %2 (scientific-plausibility warning)</source>
        <translation>“%1”高于建议上限 %2（科学合理性警告）</translation>
    </message>
    <message>
        <source>⚠ %1 invalid parameters: %2</source>
        <translation>⚠ %1 处参数无效：%2</translation>
    </message>
    <message>
        <source>△ %1 hints: %2</source>
        <translation>△ %1 条提示：%2</translation>
    </message>
    <message>
        <source>✓ Parameters valid</source>
        <translation>✓ 参数有效</translation>
    </message>
    <message>
        <source>⚠ Path does not exist (%1)</source>
        <translation>⚠ 路径不存在（%1）</translation>
    </message>
</context>
<context>
    <name>SecondaryMapViewWidget</name>
    <message>
        <source>Second View</source>
        <translation>第二视图</translation>
    </message>
    <message>
        <source>Active</source>
        <translation>活动</translation>
    </message>
    <message>
        <source>Route open / show operations to this view</source>
        <translation>将打开/显示操作路由到此视图</translation>
    </message>
    <message>
        <source>Sync Main View</source>
        <translation>同步主视图</translation>
    </message>
    <message>
        <source>Clone the main view's display layers into this view (rendered independently)</source>
        <translation>将主视图中的显示图层克隆到此视图（独立渲染）</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Close the second view and release its display lease</source>
        <translation>关闭第二视图并释放其显示租约</translation>
    </message>
    <message>
        <source>Second View (active)</source>
        <translation>第二视图（活动）</translation>
    </message>
</context>
<context>
    <name>Sicnu::PythonScriptEditor</name>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Run Script (Ctrl+Enter)</source>
        <translation>运行脚本 (Ctrl+Enter)</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>打开</translation>
    </message>
    <message>
        <source>Open Python Script</source>
        <translation>打开 Python 脚本</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <source>Save Python Script</source>
        <translation>保存 Python 脚本</translation>
    </message>
    <message>
        <source>Clear</source>
        <translation>清空</translation>
    </message>
    <message>
        <source>Clear Output Panel</source>
        <translation>清空输出面板</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Type Python script code here...</source>
        <translation>在此输入 Python 脚本代码...</translation>
    </message>
    <message>
        <source>Script execution output will appear here...</source>
        <translation>脚本执行输出将在此显示...</translation>
    </message>
    <message>
        <source>Script is empty</source>
        <translation>脚本为空</translation>
    </message>
    <message>
        <source>Initializing Python...</source>
        <translation>正在初始化 Python...</translation>
    </message>
    <message>
        <source>Failed to initialize Python interpreter.
</source>
        <translation>初始化 Python 解释器失败。
</translation>
    </message>
    <message>
        <source>Python initialization failed</source>
        <translation>Python 初始化失败</translation>
    </message>
    <message>
        <source>Running...</source>
        <translation>正在运行…</translation>
    </message>
    <message>
        <source>Python scripts (*.py);;All files (*.*)</source>
        <translation>Python 脚本 (*.py);;所有文件 (*.*)</translation>
    </message>
    <message>
        <source>Open Script</source>
        <translation>打开脚本</translation>
    </message>
    <message>
        <source>Cannot open file:
%1</source>
        <translation>无法打开文件：
%1</translation>
    </message>
    <message>
        <source>Opened %1</source>
        <translation>已打开 %1</translation>
    </message>
    <message>
        <source>Save Script</source>
        <translation>保存脚本</translation>
    </message>
    <message>
        <source>Cannot write file:
%1</source>
        <translation>无法写入文件：
%1</translation>
    </message>
    <message>
        <source>Saved %1</source>
        <translation>已保存 %1</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Script execution failed</source>
        <translation>脚本执行失败</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Script finished</source>
        <translation>脚本执行结束</translation>
    </message>
</context>
<context>
    <name>SicnuAlgorithmDialog</name>
    <message>
        <source>Advanced Parameters</source>
        <translation>高级参数</translation>
    </message>
    <message>
        <source>Load the result layers into the project when finished</source>
        <translation>完成后将结果图层加载到工程中</translation>
    </message>
    <message>
        <source>When enabled, result rasters or vector layers produced by an algorithm are added to the layer tree on the left and shown on the map automatically.</source>
        <translation>启用后，算法执行完毕生成的结果栅格或矢量图层将自动加入左侧图层树并显示在地图视图中。</translation>
    </message>
    <message>
        <source>Preview via Command Line</source>
        <translation>调用命令行预览</translation>
    </message>
    <message>
        <source>The external command line generated live from the parameters above. Copy it to a terminal to run manually (paths and temporary outputs may differ slightly from the actual run).</source>
        <translation>根据上方参数实时生成的外部命令行。可复制到终端手动执行（路径与临时输出可能与实际运行略有差异）。</translation>
    </message>
    <message>
        <source>(command line generated from parameters...)</source>
        <translation>（根据参数生成调用命令…）</translation>
    </message>
    <message>
        <source># This algorithm is built in; no external CLI command</source>
        <translation># 此算法为内置实现，无外部 CLI 命令</translation>
    </message>
    <message>
        <source>Cancel requested — please wait for the task to finish.</source>
        <translation>已请求取消——请等待任务结束。</translation>
    </message>
    <message>
        <source>Invalid Parameters</source>
        <translation>参数无效</translation>
    </message>
    <message>
        <source>Algorithm started at: %1</source>
        <translation>算法开始时间：%1</translation>
    </message>
    <message>
        <source>&lt;b&gt;Algorithm '%1' starting&amp;hellip;&lt;/b&gt;</source>
        <translation>&lt;b&gt;算法“%1”正在启动…&lt;/b&gt;</translation>
    </message>
    <message>
        <source>Input parameters:</source>
        <translation>输入参数：</translation>
    </message>
    <message>
        <source>Failed to create algorithm instance.</source>
        <translation>创建算法实例失败。</translation>
    </message>
    <message>
        <source>Algorithm prepare() failed.</source>
        <translation>算法 prepare() 失败。</translation>
    </message>
    <message>
        <source>Submitted as task %1</source>
        <translation>已提交为任务 %1</translation>
    </message>
    <message>
        <source>Algorithm execution failed. See log for details.</source>
        <translation>算法执行失败，详情见日志。</translation>
    </message>
    <message>
        <source>Execution completed in %1 seconds</source>
        <translation>执行完成，用时 %1 秒</translation>
    </message>
    <message>
        <source>Execution failed after %1 seconds</source>
        <translation>执行失败，用时 %1 秒</translation>
    </message>
    <message>
        <source>Algorithm failed.</source>
        <translation>算法失败。</translation>
    </message>
</context>
<context>
    <name>SicnuAppInterface</name>
    <message>
        <source>Plugins</source>
        <translation>插件</translation>
    </message>
    <message>
        <source>Plugin Toolbar</source>
        <translation>插件工具栏</translation>
    </message>
</context>
<context>
    <name>SpatialFilterDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the raster layer for spatial filtering.</source>
        <translation>选择待执行空间滤波的栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Filter Parameters</source>
        <translation>滤波参数</translation>
    </message>
    <message>
        <source>Mean Filter</source>
        <translation>均值滤波 (Mean)</translation>
    </message>
    <message>
        <source>Gaussian Filter</source>
        <translation>高斯滤波 (Gaussian)</translation>
    </message>
    <message>
        <source>Median Filter</source>
        <translation>中值滤波 (Median)</translation>
    </message>
    <message>
        <source>Sobel Edge Detection</source>
        <translation>Sobel 边缘检测</translation>
    </message>
    <message>
        <source>Laplacian Edge Enhancement</source>
        <translation>Laplacian 边缘增强</translation>
    </message>
    <message>
        <source>• Mean / Gaussian / median: smoothing and denoising
• Sobel / Laplacian: edge detection and sharpening
The median filter suppresses salt-and-pepper noise while preserving edges remarkably well.</source>
        <translation>• 均值 / 高斯 / 中值：平滑去噪
• Sobel / Laplacian：边缘检测与锐化增强
中值滤波对椒盐噪声具有极佳保边抑制效果。</translation>
    </message>
    <message>
        <source>Filter Type</source>
        <translation>滤波器类型</translation>
    </message>
    <message>
        <source>3×3</source>
        <translation>3×3</translation>
    </message>
    <message>
        <source>5×5</source>
        <translation>5×5</translation>
    </message>
    <message>
        <source>7×7</source>
        <translation>7×7</translation>
    </message>
    <message>
        <source>Convolution filter window size; larger windows smooth more or respond over a wider range.</source>
        <translation>卷积滤波窗口大小。窗口越大平滑强度或响应范围越大。</translation>
    </message>
    <message>
        <source>Window Size</source>
        <translation>窗口大小</translation>
    </message>
    <message>
        <source>Gaussian Std Dev Sigma</source>
        <translation>高斯标准差 Sigma</translation>
    </message>
    <message>
        <source>Spatial std dev (Sigma) of the Gaussian kernel; defaults to 1.0.</source>
        <translation>高斯滤波核的空间标准差 Sigma，默认为 1.0。</translation>
    </message>
    <message>
        <source>Spatial Filtering</source>
        <translation>空间滤波</translation>
    </message>
</context>
<context>
    <name>SpeckleFilterDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Select the SAR raster layer for speckle filtering.</source>
        <translation>选择待执行斑点滤波的 SAR 栅格图层。</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Filter Parameters</source>
        <translation>滤波参数</translation>
    </message>
    <message>
        <source>Lee Filter</source>
        <translation>Lee 滤波</translation>
    </message>
    <message>
        <source>Frost Filter</source>
        <translation>Frost 滤波</translation>
    </message>
    <message>
        <source>Kuan Filter</source>
        <translation>Kuan 滤波</translation>
    </message>
    <message>
        <source>Gamma-MAP Filter</source>
        <translation>Gamma-MAP 滤波</translation>
    </message>
    <message>
        <source>SAR speckle-reduction algorithm: Lee, Frost, Kuan or Gamma-MAP.</source>
        <translation>SAR 斑点噪声抑制算法：Lee、Frost、Kuan 或 Gamma-MAP。</translation>
    </message>
    <message>
        <source>Filter</source>
        <translation>滤波器</translation>
    </message>
    <message>
        <source>3×3</source>
        <translation>3×3</translation>
    </message>
    <message>
        <source>5×5</source>
        <translation>5×5</translation>
    </message>
    <message>
        <source>7×7</source>
        <translation>7×7</translation>
    </message>
    <message>
        <source>Filter window: 3×3 keeps detail and edges; 7×7 smooths and denoises more strongly.</source>
        <translation>滤波窗口。3×3 保持细节边缘，7×7 平滑去噪更强。</translation>
    </message>
    <message>
        <source>Filter Window</source>
        <translation>滤波窗口</translation>
    </message>
    <message>
        <source>Noise Variance</source>
        <translation>噪声方差</translation>
    </message>
    <message>
        <source>Estimated relative noise variance for the Lee / Kuan / Gamma-MAP models.</source>
        <translation>Lee / Kuan / Gamma-MAP 模型的预估相对噪声方差。</translation>
    </message>
    <message>
        <source>Damping Factor</source>
        <translation>阻尼因子</translation>
    </message>
    <message>
        <source>Exponential damping factor of the Frost filter: larger values give smoother output.</source>
        <translation>Frost 滤波器的指数阻尼衰减系数：值越大越平滑。</translation>
    </message>
    <message>
        <source>SAR Speckle Filtering</source>
        <translation>SAR 斑点滤波</translation>
    </message>
</context>
<context>
    <name>SpectralIndexDialog</name>
    <message>
        <source>Input Data</source>
        <translation>输入数据</translation>
    </message>
    <message>
        <source>Input Raster</source>
        <translation>输入栅格</translation>
    </message>
    <message>
        <source>Select the raster layer for the spectral index.</source>
        <translation>选择待计算光谱指数的栅格图层。</translation>
    </message>
    <message>
        <source>Data Assets</source>
        <translation>数据资产</translation>
    </message>
    <message>
        <source>Chooses a registered raster data asset as input. The asset version is validated at run time; execution is refused if the version has changed.</source>
        <translation>选择已注册的栅格数据资产作为输入。运行时会校验资产版本；若版本已变更将拒绝执行。</translation>
    </message>
    <message>
        <source>Index and Band Mapping</source>
        <translation>指数与波段映射</translation>
    </message>
    <message>
        <source>NDVI — Normalized Difference Vegetation Index</source>
        <translation>NDVI — 归一化植被指数</translation>
    </message>
    <message>
        <source>EVI — Enhanced Vegetation Index</source>
        <translation>EVI — 增强型植被指数</translation>
    </message>
    <message>
        <source>SAVI — Soil-Adjusted Vegetation Index</source>
        <translation>SAVI — 土壤调节植被指数</translation>
    </message>
    <message>
        <source>NDWI — Normalized Difference Water Index</source>
        <translation>NDWI — 归一化水体指数</translation>
    </message>
    <message>
        <source>NDBI — Normalized Difference Built-up Index</source>
        <translation>NDBI — 归一化建筑指数</translation>
    </message>
    <message>
        <source>MNDWI — Modified Normalized Difference Water Index</source>
        <translation>MNDWI — 改进归一化水体指数</translation>
    </message>
    <message>
        <source>Spectral index type:
• NDVI: vegetation (NIR, Red)
• EVI: enhanced vegetation (NIR, Red, Blue)
• SAVI: soil-adjusted vegetation (NIR, Red)
• NDWI: water (Green, NIR)
• NDBI: built-up (SWIR, NIR)
• MNDWI: modified water (Green, SWIR)</source>
        <translation>光谱指数类型：
• NDVI：植被 (NIR, Red)
• EVI：增强植被 (NIR, Red, Blue)
• SAVI：土壤调节植被 (NIR, Red)
• NDWI：水体 (Green, NIR)
• NDBI：建成区 (SWIR, NIR)
• MNDWI：改进水体 (Green, SWIR)</translation>
    </message>
    <message>
        <source>Index Type</source>
        <translation>指数类型</translation>
    </message>
    <message>
        <source>NIR (Near Infrared)</source>
        <translation>近红外 NIR</translation>
    </message>
    <message>
        <source>Near-infrared band; usually Band 5 on Landsat 8/9 and Band 8 on Sentinel-2.</source>
        <translation>近红外波段。Landsat 8/9 常为 Band 5，Sentinel-2 常为 Band 8。</translation>
    </message>
    <message>
        <source>Red</source>
        <translation>红光 Red</translation>
    </message>
    <message>
        <source>Red band; used by NDVI/EVI/SAVI.</source>
        <translation>红光波段。用于 NDVI/EVI/SAVI。</translation>
    </message>
    <message>
        <source>Green</source>
        <translation>绿光 Green</translation>
    </message>
    <message>
        <source>Green band; used by NDWI/MNDWI.</source>
        <translation>绿光波段。用于 NDWI/MNDWI。</translation>
    </message>
    <message>
        <source>Blue</source>
        <translation>蓝光 Blue</translation>
    </message>
    <message>
        <source>Blue band; used for the atmospheric background correction in EVI.</source>
        <translation>蓝光波段。用于 EVI 计算大气背景修正。</translation>
    </message>
    <message>
        <source>SWIR (Shortwave Infrared)</source>
        <translation>短波红外 SWIR</translation>
    </message>
    <message>
        <source>Shortwave infrared band; used by NDBI/MNDWI.</source>
        <translation>短波红外波段。用于 NDBI/MNDWI。</translation>
    </message>
    <message>
        <source>Imported products are matched by semantic band role automatically; plain rasters are pre-filled in the common band order — verify before running.</source>
        <translation>已导入的产品按语义波段角色自动匹配；普通栅格按常见波段顺序预填，请核对后运行。</translation>
    </message>
    <message>
        <source>Select a valid input data asset.</source>
        <translation>请选择一个有效的输入数据资产。</translation>
    </message>
    <message>
        <source>The selected asset no longer exists.</source>
        <translation>所选资产已不存在。</translation>
    </message>
    <message>
        <source>The selected asset has no valid data path.</source>
        <translation>所选资产无有效数据路径。</translation>
    </message>
    <message>
        <source>Spectral Indices</source>
        <translation>光谱指数</translation>
    </message>
</context>
<context>
    <name>SpectralLibraryDialog</name>
    <message>
        <source>Spectral Library Matching</source>
        <translation>光谱库匹配</translation>
    </message>
    <message>
        <source>Spectrum to Match and Library</source>
        <translation>待匹配光谱与谱库</translation>
    </message>
    <message>
        <source>Uses pixel spectra collected by the Spectral Profile panel; they can be matched against library entries or saved back to the library.</source>
        <translation>使用光谱剖面面板采集的像元光谱；可与光谱库条目进行匹配或保存回库。</translation>
    </message>
    <message>
        <source>(no spectrum collected)</source>
        <translation>（未采集光谱）</translation>
    </message>
    <message>
        <source>Current Profile</source>
        <translation>当前剖面</translation>
    </message>
    <message>
        <source>Select a spectral library JSON file (*.json)...</source>
        <translation>选择光谱库 JSON 文件 (*.json)...</translation>
    </message>
    <message>
        <source>Spectral library file (SpectralLibrary JSON): named spectra plus optional wavelength rasters.You can also save the current spectrum into the library below.</source>
        <translation>光谱库文件（SpectralLibrary JSON）：命名光谱 + 可选波长栅格。也可在下方把当前谱保存进库。</translation>
    </message>
    <message>
        <source>Browse...</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <source>Browse and choose the spectral library JSON file</source>
        <translation>浏览并选择光谱库 JSON 文件</translation>
    </message>
    <message>
        <source>Spectral Library</source>
        <translation>光谱库</translation>
    </message>
    <message>
        <source>Matching and Search Results</source>
        <translation>匹配与检索结果</translation>
    </message>
    <message>
        <source>Run Matching</source>
        <translation>运行匹配</translation>
    </message>
    <message>
        <source>Run the SAM / SID spectral matching algorithms and rank library entries by similarity</source>
        <translation>运行 SAM / SID 光谱匹配算法，对光谱库中条目按相似度排序</translation>
    </message>
    <message>
        <source>Save Current Spectrum to Library</source>
        <translation>保存当前谱到库</translation>
    </message>
    <message>
        <source>Append or save the currently collected pixel spectrum into the loaded spectral library</source>
        <translation>将当前采集的像元光谱曲线追加或保存到已加载的光谱库中</translation>
    </message>
    <message>
        <source>Match list: shows the SAM angle (smaller is more similar) and the SID divergence</source>
        <translation>匹配结果列表：显示 SAM 夹角（越小越相似）与 SID 散度</translation>
    </message>
    <message>
        <source>Rank</source>
        <translation>排名</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Material / Class</source>
        <translation>物质/类别</translation>
    </message>
    <message>
        <source>SAM (°)</source>
        <translation>SAM（°）</translation>
    </message>
    <message>
        <source>SID</source>
        <translation>SID</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Opens help for spectral library matching.</source>
        <translation>打开光谱库匹配帮助说明。</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Closes the dialog.</source>
        <translation>关闭对话框。</translation>
    </message>
    <message>
        <source>%1 bands%2</source>
        <translation>%1 个波段%2</translation>
    </message>
    <message>
        <source>（%1）</source>
        <translation>（%1）</translation>
    </message>
    <message>
        <source>, </source>
        <translation>、</translation>
    </message>
    <message>
        <source>Select Spectral Library</source>
        <translation>选择光谱库</translation>
    </message>
    <message>
        <source>Spectral Library JSON (*.json);;All Files (*)</source>
        <translation>Spectral Library JSON (*.json);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Collect a spectral profile on the map first.</source>
        <translation>请先在图上采集光谱剖面。</translation>
    </message>
    <message>
        <source>Select a spectral library file.</source>
        <translation>请选择光谱库文件。</translation>
    </message>
    <message>
        <source>Load failed: %1</source>
        <translation>加载失败：%1</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>Matching finished: %1 comparable entries (ascending SAM). The library holds %2 entries in total.Unmatched entries usually have a different band count and no wavelength raster.Entries with wavelength rasters were resampled automatically before matching.</source>
        <translation>匹配完成：%1 个可比条目（SAM 升序）。谱库共 %2 个条目。未匹配条目通常因波段数不一致且缺少波长栅格。带波长栅格的条目已自动重采样后匹配。</translation>
    </message>
    <message>
        <source>Untitled</source>
        <translation>未命名</translation>
    </message>
    <message>
        <source>Spectral Profile Panel</source>
        <translation>光谱剖面面板</translation>
    </message>
    <message>
        <source>Save failed: %1</source>
        <translation>保存失败：%1</translation>
    </message>
    <message>
        <source>Saved entry %1 to %2</source>
        <translation>已保存条目“%1”到 %2</translation>
    </message>
</context>
<context>
    <name>SpectralProfileWidget</name>
    <message>
        <source>No valid pixel data at this location</source>
        <translation>该位置没有有效像元数据</translation>
    </message>
    <message>
        <source>Click on a raster layer to view spectral profile</source>
        <translation>点击栅格图层查看光谱剖面</translation>
    </message>
    <message>
        <source>Spectral Profile — %1</source>
        <translation>光谱剖面 — %1</translation>
    </message>
    <message>
        <source>Point: (%1, %2)</source>
        <translation>点位：(%1, %2)</translation>
    </message>
    <message>
        <source>Wavelength (nm)</source>
        <translation>波长（nm）</translation>
    </message>
    <message>
        <source>Band</source>
        <translation>波段</translation>
    </message>
    <message>
        <source>Continuum Removed</source>
        <translation>连续统去除</translation>
    </message>
    <message>
        <source>Pixel Value</source>
        <translation>像元值</translation>
    </message>
</context>
<context>
    <name>StacBrowserDialog</name>
    <message>
        <source>STAC Data Browser</source>
        <translation>STAC 数据浏览</translation>
    </message>
    <message>
        <source>Workflow: fill in the catalog and spatio-temporal filters → search → select results → load assets into the project.</source>
        <translation>流程：填写目录与时空条件 → 检索 → 选中结果 → 加载资产到工程。</translation>
    </message>
    <message>
        <source>Search Criteria</source>
        <translation>检索条件</translation>
    </message>
    <message>
        <source>min_lon,min_lat,max_lon,max_lat</source>
        <translation>min_lon,min_lat,max_lon,max_lat</translation>
    </message>
    <message>
        <source>More Results</source>
        <translation>更多结果</translation>
    </message>
    <message>
        <source>Load the next page of search results.</source>
        <translation>加载下一页检索结果。</translation>
    </message>
    <message>
        <source>STAC API root URL.</source>
        <translation>STAC API 根 URL。</translation>
    </message>
    <message>
        <source>Collection ID, e.g. sentinel-2-l2a.</source>
        <translation>集合 ID，如 sentinel-2-l2a。</translation>
    </message>
    <message>
        <source>Time filter (ISO).</source>
        <translation>时间过滤（ISO）。</translation>
    </message>
    <message>
        <source>Spatial extent.</source>
        <translation>空间范围。</translation>
    </message>
    <message>
        <source>Endpoint</source>
        <translation>端点 Endpoint</translation>
    </message>
    <message>
        <source>Collection</source>
        <translation>所属集合</translation>
    </message>
    <message>
        <source>Time (Datetime)</source>
        <translation>时间 Datetime</translation>
    </message>
    <message>
        <source>Bounding Box</source>
        <translation>范围 BBox</translation>
    </message>
    <message>
        <source>Search</source>
        <translation>检索</translation>
    </message>
    <message>
        <source>Search STAC items with filters.</source>
        <translation>按条件检索 STAC 要素。</translation>
    </message>
    <message>
        <source>Search Results</source>
        <translation>检索结果</translation>
    </message>
    <message>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <source>Collections</source>
        <translation>集合</translation>
    </message>
    <message>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <source>Assets</source>
        <translation>资产</translation>
    </message>
    <message>
        <source>Search results. Select a row to load the assets.</source>
        <translation>检索结果。选中一行后加载资产。</translation>
    </message>
    <message>
        <source>Load Selected Assets</source>
        <translation>加载选中资产</translation>
    </message>
    <message>
        <source>Loads into the current project (network required).</source>
        <translation>加载到当前工程（需网络）。</translation>
    </message>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>STAC endpoint is required.</source>
        <translation>请填写 STAC 服务地址。</translation>
    </message>
    <message>
        <source>Searching...</source>
        <translation>检索中...</translation>
    </message>
    <message>
        <source>Search Failed</source>
        <translation>检索失败</translation>
    </message>
    <message>
        <source>Select a STAC item first.</source>
        <translation>请先选择一个 STAC 条目。</translation>
    </message>
    <message>
        <source>No COG asset found in selected item.</source>
        <translation>所选条目中没有 COG 资产。</translation>
    </message>
    <message>
        <source>Failed to load COG from STAC asset.</source>
        <translation>从 STAC 资产加载 COG 失败。</translation>
    </message>
</context>
<context>
    <name>TaskPanelHost</name>
    <message>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Load Results to Map</source>
        <translation>加载结果到地图</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Run Current Tool</source>
        <translation>运行当前工具</translation>
    </message>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>Fix the parameters highlighted in red first</source>
        <translation>请先修正表单中标红的参数</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
</context>
<context>
    <name>TemporalAnalysisDialog</name>
    <message>
        <source>Time series statistics (mean / min-max / std dev / count)</source>
        <translation>时序统计（均值/最值/标准差/计数）</translation>
    </message>
    <message>
        <source>Temporal compositing (best pixel / mean / median)</source>
        <translation>时序合成（最佳像元/均值/中值）</translation>
    </message>
    <message>
        <source>Index time series (per-date NDVI/EVI/... stack)</source>
        <translation>指数时序（逐日 NDVI/EVI/…栈）</translation>
    </message>
    <message>
        <source>Linear trend (slope / intercept / R²)</source>
        <translation>线性趋势（斜率/截距/R²）</translation>
    </message>
    <message>
        <source>Time series anomalies (z-score / difference)</source>
        <translation>时序异常（z-score / 差值）</translation>
    </message>
    <message>
        <source>Point/ROI time series extraction (CSV)</source>
        <translation>点/ROI 时间序列提取（CSV）</translation>
    </message>
    <message>
        <source>Epoch Scenes</source>
        <translation>时相场景</translation>
    </message>
    <message>
        <source>Multitemporal raster list. Times are parsed from product metadata / file names and can be edited; sorted by time before running.</source>
        <translation>多时相栅格列表。时间自动从产品元数据/文件名解析，可手动修改；运行前按时间排序。</translation>
    </message>
    <message>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Time (ISO)</source>
        <translation>时间 (ISO)</translation>
    </message>
    <message>
        <source>Platform</source>
        <translation>平台</translation>
    </message>
    <message>
        <source>Modality</source>
        <translation>模态</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Epoch list: the time column is editable (YYYY-MM-DD or a full timestamp); the status column shows grid consistency and QA bands.</source>
        <translation>时相列表：时间列可编辑（YYYY-MM-DD 或完整时间戳）；状态列显示网格一致性与 QA 波段。</translation>
    </message>
    <message>
        <source>Add Epochs...</source>
        <translation>添加时相…</translation>
    </message>
    <message>
        <source>Adds one or more epoch raster files (acquisition times parsed automatically).</source>
        <translation>添加一个或多个时相栅格文件（自动解析获取时间）。</translation>
    </message>
    <message>
        <source>Remove Selected</source>
        <translation>移除选中</translation>
    </message>
    <message>
        <source>Removes the selected rasters from the epoch list.</source>
        <translation>从时相列表移除选中的栅格。</translation>
    </message>
    <message>
        <source>Precheck</source>
        <translation>预检 (Preflight)</translation>
    </message>
    <message>
        <source>Runs time / grid / band-role / radiometric consistency checks without any computation.</source>
        <translation>运行时间/网格/波段角色/辐射一致性检查，不做任何计算。</translation>
    </message>
    <message>
        <source>Date filter:</source>
        <translation>日期过滤：</translation>
    </message>
    <message>
        <source>e.g. 2025-04 (filters the display by the time column; does not affect computation)</source>
        <translation>例如 2025-04（按时间列过滤显示，不影响计算）</translation>
    </message>
    <message>
        <source>Filters the list display only; all non-removed epochs take part in the computation.</source>
        <translation>仅过滤列表显示；参与计算的是全部未移除的时相。</translation>
    </message>
    <message>
        <source>Not prechecked yet: press 'Precheck' to verify time / grid / radiometric consistency.</source>
        <translation>尚未预检：点击“预检”检查时间/网格/辐射一致性。</translation>
    </message>
    <message>
        <source>Analysis and Parameters</source>
        <translation>分析与参数</translation>
    </message>
    <message>
        <source>Analysis:</source>
        <translation>分析：</translation>
    </message>
    <message>
        <source>Time series analysis algorithms (executed via the processing registry; reusable in the toolbox / Agent).</source>
        <translation>时间序列分析算法（经处理注册表执行，可在工具箱/Agent 中复用）。</translation>
    </message>
    <message>
        <source>Band roles:</source>
        <translation>波段角色：</translation>
    </message>
    <message>
        <source>Band 1</source>
        <translation>波段 1</translation>
    </message>
    <message>
        <source>Analysis bands are resolved by semantic role (metadata first; falls back to the conventional order with a warning when absent).</source>
        <translation>分析波段按语义角色解析（元数据优先，缺省按常规顺序回退并警告）。</translation>
    </message>
    <message>
        <source>Output bands: count / valid_count / mean / min / max / stddev. When median is ticked, tile size shrinks automatically to fit the memory budget (exact values, not approximate).</source>
        <translation>输出波段：count / valid_count / mean / min / max / stddev。勾选中值时按内存预算自动缩小分块（精确值，非近似）。</translation>
    </message>
    <message>
        <source>Includes median</source>
        <translation>包含中值 (median)</translation>
    </message>
    <message>
        <source>Exact per-pixel median; tile size shrinks automatically for long series to respect the memory budget.</source>
        <translation>逐像元精确中值；长时序自动减小 tile 尺寸以满足内存预算。</translation>
    </message>
    <message>
        <source>Method:</source>
        <translation>方法：</translation>
    </message>
    <message>
        <source>Best Pixel</source>
        <translation>最佳像元</translation>
    </message>
    <message>
        <source>Mean</source>
        <translation>均值</translation>
    </message>
    <message>
        <source>Median</source>
        <translation>中值</translation>
    </message>
    <message>
        <source>Best pixel: highest quality score among valid observations (ties broken by closeness to the target date, then by the earlier epoch);The output includes valid-observation count and quality score bands.</source>
        <translation>最佳像元：有效观测中质量分最高（并列取最接近目标日期，再取更早时相）；输出含有效观测数与质量分波段。</translation>
    </message>
    <message>
        <source>Period:</source>
        <translation>周期：</translation>
    </message>
    <message>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <source>Monthly</source>
        <translation>逐月</translation>
    </message>
    <message>
        <source>Seasonally</source>
        <translation>逐季</translation>
    </message>
    <message>
        <source>Season</source>
        <translation>季节</translation>
    </message>
    <message>
        <source>Yearly</source>
        <translation>逐年</translation>
    </message>
    <message>
        <source>When grouped by period, one file is written per period (suffix = start date).</source>
        <translation>按周期分组时每个周期输出一个文件（后缀为起始日期）。</translation>
    </message>
    <message>
        <source>Index:</source>
        <translation>指数：</translation>
    </message>
    <message>
        <source>Same kernel as the single-scene spectral index; output is a stack with one band per date (acquisition-time metadata preserved).</source>
        <translation>与单景光谱指数相同的计算内核；输出为逐日期一个波段的栈（保留获取时间元数据）。</translation>
    </message>
    <message>
        <source>Outputs: slope (per day) / intercept / R² / n / RMSE. The regression uses real acquisition-time intervals; slope × 365.25 = annual change rate.</source>
        <translation>输出：slope（每天）/ intercept / R² / n / RMSE。回归使用真实获取时间间隔，斜率×365.25 = 年变化率。</translation>
    </message>
    <message>
        <source>z-score</source>
        <translation>z-score</translation>
    </message>
    <message>
        <source>Difference from baseline mean</source>
        <translation>与基线均值之差</translation>
    </message>
    <message>
        <source>The baseline defaults to all epochs except the target; narrow it with the baseline_start/end parameters.</source>
        <translation>基线默认为除目标时相外的全部时相；可用参数 baseline_start/end 缩小。</translation>
    </message>
    <message>
        <source>Point (x, y):</source>
        <translation>点 (x, y)：</translation>
    </message>
    <message>
        <source>Map coordinates, e.g. 460000.5, 3390020.25 (alternative to point/polygon)</source>
        <translation>地图坐标，例如 460000.5, 3390020.25（与点/多边形二选一）</translation>
    </message>
    <message>
        <source>The point coordinates must share the epoch collection's CRS.</source>
        <translation>点坐标须与时相集合同一坐标系。</translation>
    </message>
    <message>
        <source>Polygon ROIs:</source>
        <translation>多边形 ROI：</translation>
    </message>
    <message>
        <source>Vertex list x1,y1;x2,y2;x3,y3;... (closed ring; the bounding box is scanned only)</source>
        <translation>顶点串 x1,y1;x2,y2;x3,y3;…（闭合环，仅扫描包围盒）</translation>
    </message>
    <message>
        <source>ROI statistics: mean/median/min/max/stddev/valid_count, exported as CSV per date.</source>
        <translation>ROI 统计：mean/median/min/max/stddev/valid_count，按日期输出 CSV。</translation>
    </message>
    <message>
        <source>Add Epoch Rasters</source>
        <translation>添加时相栅格</translation>
    </message>
    <message>
        <source>Rasters (*.tif *.tiff *.img *.asc);;All Files (*)</source>
        <translation>栅格 (*.tif *.tiff *.img *.asc);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Editable: YYYY-MM-DD or ISO timestamp; empty means unknown (the precheck will reject it).</source>
        <translation>可编辑：YYYY-MM-DD 或 ISO 时间戳；留空表示未知（预检将拒绝）。</translation>
    </message>
    <message>
        <source>Cannot Open</source>
        <translation>无法打开</translation>
    </message>
    <message>
        <source>Grid unknown</source>
        <translation>网格未知</translation>
    </message>
    <message>
        <source>Grids consistent</source>
        <translation>网格一致</translation>
    </message>
    <message>
        <source>Grids inconsistent</source>
        <translation>网格不一致</translation>
    </message>
    <message>
        <source>Missing time</source>
        <translation>缺时间</translation>
    </message>
    <message>
        <source>QA✓</source>
        <translation>QA✓</translation>
    </message>
    <message>
        <source>Add epoch scenes first.</source>
        <translation>请先添加时相场景。</translation>
    </message>
    <message>
        <source>Epoch: %1</source>
        <translation>时相: %1</translation>
    </message>
    <message>
        <source>Range: %1 → %2</source>
        <translation>区间: %1 → %2</translation>
    </message>
    <message>
        <source>Grid: consistent</source>
        <translation>网格: 一致</translation>
    </message>
    <message>
        <source>Grid: inconsistent</source>
        <translation>网格: 不一致</translation>
    </message>
    <message>
        <source>Radiometric state: %1</source>
        <translation>辐射态: %1</translation>
    </message>
    <message>
        <source>Unknown (warning)</source>
        <translation>未知（警告）</translation>
    </message>
    <message>
        <source>Precheck Passed</source>
        <translation>预检通过</translation>
    </message>
    <message>
        <source>Precheck failed: %1 blocking issues</source>
        <translation>预检失败： %1 个阻断问题</translation>
    </message>
    <message>
        <source>Time Series Precheck</source>
        <translation>时间序列预检</translation>
    </message>
    <message>
        <source>Time series analysis needs at least 2 epochs.</source>
        <translation>时间序列分析至少需要 2 个时相。</translation>
    </message>
    <message>
        <source>Row %1 is missing an acquisition time (not parsed from product metadata / file name; enter it manually).</source>
        <translation>第 %1 行缺少获取时间（产品元数据/文件名未解析出，请手动填写）。</translation>
    </message>
    <message>
        <source>Specify the output file path.</source>
        <translation>请指定输出文件路径。</translation>
    </message>
    <message>
        <source>Series extraction requires exactly one of: a point or a polygon.</source>
        <translation>序列提取需要且只能填写 点 或 多边形 之一。</translation>
    </message>
    <message>
        <source>Loaded</source>
        <translation>已加载</translation>
    </message>
    <message>
        <source>Time Series Analysis Collection %1</source>
        <translation>时序分析集合 %1</translation>
    </message>
    <message>
        <source>Point coordinates must be in x,y format.</source>
        <translation>点坐标格式应为 x,y。</translation>
    </message>
    <message>
        <source>Polygon vertices must be x,y pairs separated by semicolons.</source>
        <translation>多边形顶点格式应为 x,y（分号分隔）。</translation>
    </message>
    <message>
        <source>Time Series Analysis</source>
        <translation>时序分析</translation>
    </message>
</context>
<context>
    <name>TerrainDialog</name>
    <message>
        <source>Terrain Analysis</source>
        <translation>地形分析</translation>
    </message>
    <message>
        <source>Input Data and Analysis Type</source>
        <translation>输入数据与分析类型</translation>
    </message>
    <message>
        <source>Select the DEM elevation raster and the target analysis product. Running in a metric projected CRS is recommended for accurate slope and shading.</source>
        <translation>选择 DEM 高程栅格与目标分析产品。建议在米制投影坐标系下运行以保证坡度与阴影计算精度。</translation>
    </message>
    <message>
        <source>DEM elevation raster used in the computation.</source>
        <translation>参与计算的 DEM 高程栅格图层。</translation>
    </message>
    <message>
        <source>DEM layer</source>
        <translation>DEM 图层</translation>
    </message>
    <message>
        <source>Slope (degrees)</source>
        <translation>坡度 (Slope, 度)</translation>
    </message>
    <message>
        <source>Aspect (degrees)</source>
        <translation>坡向 (Aspect, 度)</translation>
    </message>
    <message>
        <source>Hillshade</source>
        <translation>山体阴影 (Hillshade)</translation>
    </message>
    <message>
        <source>Surface Roughness</source>
        <translation>地表粗糙度 (Roughness)</translation>
    </message>
    <message>
        <source>Terrain Ruggedness Index (TRI)</source>
        <translation>地形起伏度 (TRI)</translation>
    </message>
    <message>
        <source>Topographic Position Index (TPI)</source>
        <translation>地形位置指数 (TPI)</translation>
    </message>
    <message>
        <source>Slope / aspect computation; hillshade needs solar azimuth and elevation; roughness / TRI / TPI are geomorphometric indices.</source>
        <translation>坡度/坡向计算；山体阴影需指定太阳方位角与高度角；粗糙度/TRI/TPI 为地貌特征指数。</translation>
    </message>
    <message>
        <source>Analysis Type</source>
        <translation>分析类型</translation>
    </message>
    <message>
        <source>Terrain Computation Parameters</source>
        <translation>地形计算参数</translation>
    </message>
    <message>
        <source>Pixel size is usually estimated automatically from the raster spatial resolution; sun illumination parameters only apply to hillshade analysis.</source>
        <translation>像元尺寸通常根据栅格空间分辨率自动估算；太阳光照参数仅在山体阴影分析时生效。</translation>
    </message>
    <message>
        <source>Horizontal and vertical grid pixel size (map coordinate units).</source>
        <translation>水平与垂直网格像元大小（地图坐标单位）。</translation>
    </message>
    <message>
        <source>Pixel Size</source>
        <translation>像元大小</translation>
    </message>
    <message>
        <source>Solar azimuth: clockwise angle from true north (0°–360°).</source>
        <translation>太阳方位角：自正北顺时针旋转角度 (0°~360°)。</translation>
    </message>
    <message>
        <source>Solar Azimuth</source>
        <translation>太阳方位角</translation>
    </message>
    <message>
        <source>Solar elevation: angle of the sun above the horizon (0°–90°).</source>
        <translation>太阳高度角：太阳光线与地平面的夹角 (0°~90°)。</translation>
    </message>
    <message>
        <source>Solar Elevation</source>
        <translation>太阳高度角</translation>
    </message>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Select a valid DEM layer.</source>
        <translation>请选择有效的 DEM 图层。</translation>
    </message>
    <message>
        <source>Select a DEM layer.</source>
        <translation>请选择 DEM 图层。</translation>
    </message>
    <message>
        <source>Processing...</source>
        <translation>处理中…</translation>
    </message>
</context>
<context>
    <name>UndoWidget</name>
    <message>
        <source>Undo/Redo</source>
        <translation>撤销/重做</translation>
    </message>
    <message>
        <source>Undo</source>
        <translation>撤销</translation>
    </message>
    <message>
        <source>Redo</source>
        <translation>重做</translation>
    </message>
</context>
<context>
    <name>WorkflowSessionController</name>
    <message>
        <source>Workflow definition not found: %1</source>
        <translation>未找到工作流定义：%1</translation>
    </message>
    <message>
        <source>Workflow has no steps: %1</source>
        <translation>工作流无步骤：%1</translation>
    </message>
    <message>
        <source>Operator not registered: %1</source>
        <translation>算子未注册：%1</translation>
    </message>
    <message>
        <source>Workspace '%1' is open. Complete the interactive steps in the dedicated window;The task panel shows parameters of operator steps (e.g. rs:obia_segment / rs:obia_classify).</source>
        <translation>工作空间「%1」已打开。请在专用窗口中完成交互步骤；任务面板可查看算子型步骤（如 rs:obia_segment / rs:obia_classify）的参数。</translation>
    </message>
    <message>
        <source>Opened tool: %1</source>
        <translation>已打开工具：%1</translation>
    </message>
    <message>
        <source>The current step is not a runnable operator</source>
        <translation>当前步骤不是可运行算子</translation>
    </message>
    <message>
        <source>Running...</source>
        <translation>正在运行…</translation>
    </message>
    <message>
        <source>Workflow DAG submission failed</source>
        <translation>工作流 DAG 提交失败</translation>
    </message>
    <message>
        <source>Workflow TaskPipeline submitted to the Task Center for execution...</source>
        <translation>工作流 TaskPipeline 已提交至 TaskCenter 运行…</translation>
    </message>
    <message>
        <source>Workflow TaskPipeline (up to %1) submitted for execution...</source>
        <translation>工作流 TaskPipeline（至 %1）已提交运行…</translation>
    </message>
    <message>
        <source>Workflow run stopped</source>
        <translation>工作流运行已停止</translation>
    </message>
    <message>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <source>Run Failed</source>
        <translation>运行失败</translation>
    </message>
    <message>
        <source>Workflow ended (some steps failed)</source>
        <translation>工作流结束（有步骤失败）</translation>
    </message>
    <message>
        <source>Workflow finished end to end!</source>
        <translation>工作流全流程运行完成！</translation>
    </message>
    <message>
        <source>Run Succeeded</source>
        <translation>运行成功</translation>
    </message>
    <message>
        <source>Run succeeded: %1</source>
        <translation>运行成功：%1</translation>
    </message>
</context>
<context>
    <name>sicnu::app::CommandPalette</name>
    <message>
        <source>Command Palette</source>
        <translation>命令面板</translation>
    </message>
    <message>
        <source>Type a command name / keyword...</source>
        <translation>输入命令名称 / 关键词…</translation>
    </message>
    <message>
        <source>Command Search</source>
        <translation>命令搜索</translation>
    </message>
    <message>
        <source>Command Results</source>
        <translation>命令结果</translation>
    </message>
    <message>
        <source>↑↓ select · Enter run · Esc close</source>
        <translation>↑↓ 选择 · Enter 执行 · Esc 关闭</translation>
    </message>
</context>
<context>
    <name>sicnu::app::DatasetExperimentPanel</name>
    <message>
        <source>Draft</source>
        <translation>草稿</translation>
    </message>
    <message>
        <source>Submitted</source>
        <translation>已提交</translation>
    </message>
    <message>
        <source>Deprecated</source>
        <translation>已弃用</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>Not assessed</source>
        <translation>未评估</translation>
    </message>
    <message>
        <source>Draft Grade</source>
        <translation>草稿级</translation>
    </message>
    <message>
        <source>Valid</source>
        <translation>有效</translation>
    </message>
    <message>
        <source>Authenticated</source>
        <translation>已认证</translation>
    </message>
    <message>
        <source>Created</source>
        <translation>已创建</translation>
    </message>
    <message>
        <source>Running</source>
        <translation>运行中</translation>
    </message>
    <message>
        <source>Interrupted</source>
        <translation>已中断</translation>
    </message>
    <message>
        <source>Cancelling</source>
        <translation>取消中</translation>
    </message>
    <message>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Open Dataset Library...</source>
        <translation>打开数据集库…</translation>
    </message>
    <message>
        <source>Not open</source>
        <translation>未打开</translation>
    </message>
    <message>
        <source>First-page sample preview (up to %1 rows; the library holds the complete data)</source>
        <translation>样本首页预览（最多 %1 条，完整数据以库为准）</translation>
    </message>
    <message>
        <source>Dataset</source>
        <translation>数据集</translation>
    </message>
    <message>
        <source>Open Experiment Library...</source>
        <translation>打开实验库…</translation>
    </message>
    <message>
        <source>Run ID</source>
        <translation>运行 ID</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Algorithm</source>
        <translation>算法</translation>
    </message>
    <message>
        <source>Dataset Version</source>
        <translation>数据集版本</translation>
    </message>
    <message>
        <source>Compare Selected Runs</source>
        <translation>对比选中运行</translation>
    </message>
    <message>
        <source>Experiment</source>
        <translation>实验</translation>
    </message>
    <message>
        <source>Open Dataset Library</source>
        <translation>打开数据集库</translation>
    </message>
    <message>
        <source>SQLite Databases (*.db *.sqlite);;All Files (*)</source>
        <translation>SQLite 数据库 (*.db *.sqlite);;所有文件 (*)</translation>
    </message>
    <message>
        <source>File not found: %1</source>
        <translation>文件不存在：%1</translation>
    </message>
    <message>
        <source>Open failed: %1</source>
        <translation>打开失败：%1</translation>
    </message>
    <message>
        <source>Open Experiment Library</source>
        <translation>打开实验库</translation>
    </message>
    <message>
        <source>Read failed: %1</source>
        <translation>读取失败：%1</translation>
    </message>
    <message>
        <source>Unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <source>%1 — %2 datasets in total</source>
        <translation>%1 — 共 %2 个数据集</translation>
    </message>
    <message>
        <source>This dataset has no versions yet.</source>
        <translation>该数据集暂无版本。</translation>
    </message>
    <message>
        <source>Version %1 (%2, quality: %3)</source>
        <translation>版本 %1（%2，质量：%3）</translation>
    </message>
    <message>
        <source>Parent version: %1</source>
        <translation>父版本：%1</translation>
    </message>
    <message>
        <source>Submitted at: %1</source>
        <translation>提交时间：%1</translation>
    </message>
    <message>
        <source>Fingerprint: %1</source>
        <translation>指纹：%1</translation>
    </message>
    <message>
        <source>Not computed</source>
        <translation>未计算</translation>
    </message>
    <message>
        <source>Description: %1</source>
        <translation>说明：%1</translation>
    </message>
    <message>
        <source>Samples: %1</source>
        <translation>样本数：%1</translation>
    </message>
    <message>
        <source>Labeling scheme: %1</source>
        <translation>标注方案：%1</translation>
    </message>
    <message>
        <source>Not declared in the manifest</source>
        <translation>清单未声明</translation>
    </message>
    <message>
        <source>Split list: %1</source>
        <translation>切分清单：%1</translation>
    </message>
    <message>
        <source>Split list: none (not split or not declared)</source>
        <translation>切分清单：无（未划分或未声明）</translation>
    </message>
    <message>
        <source>Leak audit / quality report / reproduction bundle: generated by CLI / Agent pipelines; this panel does not recompute them.</source>
        <translation>泄露审计 / 质量报告 / 复现包：由 CLI/Agent 流程生成，此面板不重复计算。</translation>
    </message>
    <message>
        <source>(no samples)</source>
        <translation>（无样本）</translation>
    </message>
    <message>
        <source>Failed to read samples (the library is read-only or the version does not exist).</source>
        <translation>样本读取失败（库为只读或版本不存在）。</translation>
    </message>
    <message>
        <source>%1 experiments in total</source>
        <translation>共 %1 个实验</translation>
    </message>
    <message>
        <source>Run metrics (showing the first %1 of %2 runs).</source>
        <translation>运行指标（仅显示前 %1 / %2 个运行）。</translation>
    </message>
    <message>
        <source>Run metrics (%1 runs in total).</source>
        <translation>运行指标（共 %1 个运行）。</translation>
    </message>
    <message>
        <source>This run has no recorded metrics.</source>
        <translation>该运行没有指标记录。</translation>
    </message>
    <message>
        <source>Both runs need recorded metrics to compare (metrics from one side only are shown otherwise).</source>
        <translation>两个运行都有指标记录才能对比（缺一侧则显示单侧指标）。</translation>
    </message>
</context>
<context>
    <name>sicnu::app::HelpCenterDialog</name>
    <message>
        <source>Help Center</source>
        <translation>帮助中心</translation>
    </message>
</context>
<context>
    <name>sicnu::app::InspectorHost</name>
    <message>
        <source>No object selected</source>
        <translation>未选中对象</translation>
    </message>
</context>
<context>
    <name>sicnu::app::LayerGeneralSection</name>
    <message>
        <source>No layer selected.</source>
        <translation>未选中图层。</translation>
    </message>
    <message>
        <source>Raster</source>
        <translation>栅格</translation>
    </message>
    <message>
        <source>Vector</source>
        <translation>矢量</translation>
    </message>
    <message>
        <source>Vector Tiles</source>
        <translation>矢量瓦片</translation>
    </message>
    <message>
        <source>Grid</source>
        <translation>网格</translation>
    </message>
    <message>
        <source>Point Cloud</source>
        <translation>点云</translation>
    </message>
    <message>
        <source>Layer</source>
        <translation>图层</translation>
    </message>
    <message>
        <source>Valid</source>
        <translation>有效</translation>
    </message>
    <message>
        <source>Invalid / source missing</source>
        <translation>无效 / 数据源缺失</translation>
    </message>
    <message>
        <source>Undefined</source>
        <translation>未定义</translation>
    </message>
    <message>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <source>&lt;tr&gt;&lt;td&gt;Bands&lt;/td&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Pixel size&lt;/td&gt;&lt;td&gt;%2 × %3&lt;/td&gt;&lt;/tr&gt;</source>
        <translation>&lt;tr&gt;&lt;td&gt;波段数&lt;/td&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;像元尺寸&lt;/td&gt;&lt;td&gt;%2 × %3&lt;/td&gt;&lt;/tr&gt;</translation>
    </message>
    <message>
        <source>&lt;tr&gt;&lt;td&gt;Features&lt;/td&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Geometry type&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;</source>
        <translation>&lt;tr&gt;&lt;td&gt;要素数&lt;/td&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;几何类型&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;Type&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Status&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%4&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Extent&lt;/td&gt;&lt;td&gt;%5&lt;/td&gt;&lt;/tr&gt;%6&lt;/table&gt;</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;类型&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;状态&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%4&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;范围&lt;/td&gt;&lt;td&gt;%5&lt;/td&gt;&lt;/tr&gt;%6&lt;/table&gt;</translation>
    </message>
    <message>
        <source>General</source>
        <translation>常规</translation>
    </message>
</context>
<context>
    <name>sicnu::app::LayerMetadataSection</name>
    <message>
        <source>No layer selected.</source>
        <translation>未选中图层。</translation>
    </message>
    <message>
        <source>Metadata</source>
        <translation>元数据</translation>
    </message>
</context>
<context>
    <name>sicnu::app::MapWorkbench</name>
    <message>
        <source>Map Workspace</source>
        <translation>地图工作区</translation>
    </message>
</context>
<context>
    <name>sicnu::app::ModelWorkbenchPanel</name>
    <message>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <source>Weights missing</source>
        <translation>权重缺失</translation>
    </message>
    <message>
        <source>Manifest invalid</source>
        <translation>清单无效</translation>
    </message>
    <message>
        <source>Checksum mismatch</source>
        <translation>校验和不匹配</translation>
    </message>
    <message>
        <source>Unsupported at Runtime</source>
        <translation>运行时不支持</translation>
    </message>
    <message>
        <source>Hardware Incompatible</source>
        <translation>硬件不兼容</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>No weights file</source>
        <translation>无权重文件</translation>
    </message>
    <message>
        <source>File Not Found</source>
        <translation>文件不存在</translation>
    </message>
    <message>
        <source>%1 MiB</source>
        <translation>%1 MiB</translation>
    </message>
    <message>
        <source>Filter by name / task / tag</source>
        <translation>按名称 / 任务 / 标签过滤</translation>
    </message>
    <message>
        <source>Rescan Catalog</source>
        <translation>重新扫描目录</translation>
    </message>
    <message>
        <source>CPU</source>
        <translation>CPU</translation>
    </message>
    <message>
        <source>CUDA</source>
        <translation>CUDA</translation>
    </message>
    <message>
        <source>Device Filter</source>
        <translation>设备筛选</translation>
    </message>
    <message>
        <source>Filters the catalog view only; the device for test inference is resolved by the runtime.</source>
        <translation>仅过滤目录视图；测试推理的设备由运行时自动解析。</translation>
    </message>
    <message>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <source>Tasks</source>
        <translation>任务</translation>
    </message>
    <message>
        <source>Framework</source>
        <translation>框架</translation>
    </message>
    <message>
        <source>Device</source>
        <translation>设备</translation>
    </message>
    <message>
        <source>Weights</source>
        <translation>权重</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Test inference...</source>
        <translation>测试推理…</translation>
    </message>
    <message>
        <source>Manifest Details</source>
        <translation>清单（manifest）细节</translation>
    </message>
    <message>
        <source>Catalog loading diagnostics (empty when healthy)</source>
        <translation>目录装载诊断（无问题时为空）</translation>
    </message>
    <message>
        <source>Model catalog rescanned (%1 models).</source>
        <translation>模型目录已重新扫描（%1 个模型）。</translation>
    </message>
    <message>
        <source>GPU/CPU</source>
        <translation>GPU/CPU</translation>
    </message>
    <message>
        <source>(this model has no registration record)</source>
        <translation>（该模型无注册记录）</translation>
    </message>
    <message>
        <source>Model not ready (%1); submission cancelled: %2</source>
        <translation>模型未就绪（%1），已取消提交：%2</translation>
    </message>
    <message>
        <source>Select the test input raster (%1)</source>
        <translation>选择测试输入栅格（%1）</translation>
    </message>
    <message>
        <source>Raster Files (*.tif *.tiff *.img *.dat);;All Files (*)</source>
        <translation>栅格文件 (*.tif *.tiff *.img *.dat);;所有文件 (*)</translation>
    </message>
    <message>
        <source>Test inference submitted (task %1); see the processing history for progress and failure reasons.</source>
        <translation>测试推理已提交（任务 %1），进度与失败原因见处理历史。</translation>
    </message>
    <message>
        <source>The submission was rejected by the Task Center (resource or parameter problem) — see processing history / log.</source>
        <translation>提交被任务中心拒绝（资源或参数问题）——详见处理历史/日志。</translation>
    </message>
</context>
<context>
    <name>sicnu::app::ProcessingHistoryModel</name>
    <message>
        <source>%1 artifacts (%2 ...)</source>
        <translation>%1 项产物（%2 …）</translation>
    </message>
    <message>
        <source>Tasks</source>
        <translation>任务</translation>
    </message>
    <message>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <source>Start Time</source>
        <translation>开始时间</translation>
    </message>
    <message>
        <source>Elapsed</source>
        <translation>耗时</translation>
    </message>
    <message>
        <source>Artifacts</source>
        <translation>产物</translation>
    </message>
</context>
<context>
    <name>sicnu::app::ProcessingHistoryPanel</name>
    <message>
        <source>Incremental search: name / source / algorithm / task ID</source>
        <translation>增量搜索：名称 / 来源 / 算法 / 任务 ID</translation>
    </message>
    <message>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <source>Running</source>
        <translation>运行中</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation>排队</translation>
    </message>
    <message>
        <source>Waiting for Resources</source>
        <translation>等待资源</translation>
    </message>
    <message>
        <source>Finished</source>
        <translation>已完成</translation>
    </message>
    <message>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <source>Interrupted</source>
        <translation>已中断</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Re-run</source>
        <translation>重跑</translation>
    </message>
    <message>
        <source>Open Artifacts</source>
        <translation>打开产物</translation>
    </message>
    <message>
        <source>Comparison Artifacts</source>
        <translation>对比产物</translation>
    </message>
    <message>
        <source>View Provenance</source>
        <translation>查看溯源</translation>
    </message>
    <message>
        <source>Resume Run</source>
        <translation>恢复运行</translation>
    </message>
    <message>
        <source>gui</source>
        <translation>gui</translation>
    </message>
    <message>
        <source>Workflow %1</source>
        <translation>工作流 %1</translation>
    </message>
    <message>
        <source>%1 rows in total (projected in this session; cap %2, truncated %3).</source>
        <translation>共 %1 条（本会话投影；上限 %2 条，已截断 %3 条）。</translation>
    </message>
    <message>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
</context>
<context>
    <name>sicnu::app::ProvenanceSection</name>
    <message>
        <source>The data catalog is unavailable; provenance cannot be shown.</source>
        <translation>数据目录不可用，无法显示溯源。</translation>
    </message>
    <message>
        <source>The current selection is not registered in the data catalog and has no platform provenance.</source>
        <translation>当前选中项未注册到数据目录，没有平台溯源信息。</translation>
    </message>
    <message>
        <source>Multiple selection — showing provenance for the first %1 items only.</source>
        <translation>多选 — 仅显示前 %1 项的溯源。</translation>
    </message>
    <message>
        <source>Asset %1 is no longer in the data catalog.</source>
        <translation>资产 %1 已不在数据目录中。</translation>
    </message>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Asset ID</source>
        <translation>资产 ID</translation>
    </message>
    <message>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>Version</source>
        <translation>版本</translation>
    </message>
    <message>
        <source>r%1</source>
        <translation>r%1</translation>
    </message>
    <message>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Asset Identifier</source>
        <translation>资产标识</translation>
    </message>
    <message>
        <source>Asset status is '%1' — results may be unreadable or inconsistent with the catalog.</source>
        <translation>资产状态为「%1」——结果可能不可读或与目录不一致。</translation>
    </message>
    <message>
        <source>Operators / Models</source>
        <translation>算子 / 模型</translation>
    </message>
    <message>
        <source>Operator Version</source>
        <translation>算子版本</translation>
    </message>
    <message>
        <source>Workflow</source>
        <translation>工作流</translation>
    </message>
    <message>
        <source>Run</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Steps</source>
        <translation>步骤</translation>
    </message>
    <message>
        <source>Task References</source>
        <translation>任务引用</translation>
    </message>
    <message>
        <source>Finish Time</source>
        <translation>完成时间</translation>
    </message>
    <message>
        <source>Parameter Snapshot</source>
        <translation>参数快照</translation>
    </message>
    <message>
        <source>Execution Fingerprint</source>
        <translation>执行指纹</translation>
    </message>
    <message>
        <source>Cache</source>
        <translation>缓存</translation>
    </message>
    <message>
        <source>Hit (not recomputed)</source>
        <translation>命中（未重新计算）</translation>
    </message>
    <message>
        <source>Miss (newly computed)</source>
        <translation>未命中（新计算）</translation>
    </message>
    <message>
        <source>Input asset %1 was removed from the catalog; the chain is incomplete.</source>
        <translation>输入资产 %1 已从目录中删除，链条不完整。</translation>
    </message>
    <message>
        <source>Source Assets</source>
        <translation>源资产</translation>
    </message>
    <message>
        <source>Input path %1 did not resolve to a registered asset (unregistered or spelled differently).</source>
        <translation>输入路径 %1 未能解析为注册资产（未注册或拼写差异）。</translation>
    </message>
    <message>
        <source>Time Series Collection</source>
        <translation>时序集合</translation>
    </message>
    <message>
        <source>Provenance</source>
        <translation>溯源</translation>
    </message>
    <message>
        <source>No derivation record (registered directly)</source>
        <translation>无派生记录（直接注册）</translation>
    </message>
    <message>
        <source>Production Process</source>
        <translation>生产过程</translation>
    </message>
    <message>
        <source> ← %1</source>
        <translation> ← %1</translation>
    </message>
    <message>
        <source> ← ... (chain truncated)</source>
        <translation> ← …（链已截断）</translation>
    </message>
    <message>
        <source>Derivation Chain</source>
        <translation>派生链</translation>
    </message>
    <message>
        <source>From Source to Here</source>
        <translation>自源至此</translation>
    </message>
    <message>
        <source>... %1 derived artifacts in total</source>
        <translation>…共 %1 项派生产物</translation>
    </message>
    <message>
        <source>Derived Artifacts</source>
        <translation>派生产物</translation>
    </message>
    <message>
        <source>Used by</source>
        <translation>被用于</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>Catalog Availability</source>
        <translation>目录可用性</translation>
    </message>
    <message>
        <source>The governance catalog marks this asset as stale.</source>
        <translation>治理目录标记该资产为过期（stale）。</translation>
    </message>
    <message>
        <source>The governance catalog has not validated this asset yet (unverified).</source>
        <translation>治理目录尚未校验该资产（unverified）。</translation>
    </message>
    <message>
        <source>Content Fingerprint</source>
        <translation>内容指纹</translation>
    </message>
    <message>
        <source>Not computed</source>
        <translation>未计算</translation>
    </message>
    <message>
        <source>Integrity Check</source>
        <translation>完整性校验</translation>
    </message>
    <message>
        <source>Never validated</source>
        <translation>从未校验</translation>
    </message>
    <message>
        <source>This asset has never passed an integrity check.</source>
        <translation>该资产从未做过完整性校验。</translation>
    </message>
    <message>
        <source>Payload / Sensor</source>
        <translation>载荷 / 传感器</translation>
    </message>
    <message>
        <source>Validation and Governance</source>
        <translation>校验与治理</translation>
    </message>
    <message>
        <source>Quality Hint</source>
        <translation>质量提示</translation>
    </message>
</context>
<context>
    <name>sicnu::app::SarInfoSection</name>
    <message>
        <source>No SAR raster selected.</source>
        <translation>未选中 SAR 栅格。</translation>
    </message>
    <message>
        <source>&lt;tr&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;</source>
        <translation>&lt;tr&gt;&lt;td&gt;%1&lt;/td&gt;&lt;/tr&gt;</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;Identified as a SAR product (heuristics from source / name; explicit detection can override).&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;Bands&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;%4&lt;/table&gt;&lt;br&gt;%5</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;已识别为 SAR 产品（基于数据源/名称启发式，可被显式判定覆盖）。&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;波段&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;%4&lt;/table&gt;&lt;br&gt;%5</translation>
    </message>
    <message>
        <source>Undefined</source>
        <translation>未定义</translation>
    </message>
    <message>
        <source>No standard SAR fields (polarization / orbit / incidence angle) found in the provider metadata.</source>
        <translation>提供方元数据中未发现极化/轨道/入射角等标准 SAR 字段。</translation>
    </message>
    <message>
        <source>SAR</source>
        <translation>SAR</translation>
    </message>
</context>
<context>
    <name>sicnu::app::TemporalSceneModel</name>
    <message>
        <source>Unknown date</source>
        <translation>未知日期</translation>
    </message>
    <message>
        <source>Not reported</source>
        <translation>未报告</translation>
    </message>
    <message>
        <source>QA Mask (%1)</source>
        <translation>QA 掩膜 (%1)</translation>
    </message>
    <message>
        <source>Quality Band (%1)</source>
        <translation>质量波段 (%1)</translation>
    </message>
    <message>
        <source>Date</source>
        <translation>日期</translation>
    </message>
    <message>
        <source>Platform</source>
        <translation>平台</translation>
    </message>
    <message>
        <source>Modality</source>
        <translation>模态</translation>
    </message>
    <message>
        <source>Cloud Cover</source>
        <translation>云量</translation>
    </message>
    <message>
        <source>QA</source>
        <translation>QA</translation>
    </message>
    <message>
        <source>Path</source>
        <translation>路径</translation>
    </message>
</context>
<context>
    <name>sicnu::app::TemporalTimelineBar</name>
    <message>
        <source>Collection Has No Scenes</source>
        <translation>集合无场景</translation>
    </message>
</context>
<context>
    <name>sicnu::app::TemporalWorkbenchPanel</name>
    <message>
        <source>Collection Summary</source>
        <translation>集合摘要</translation>
    </message>
    <message>
        <source>Time Series Collection</source>
        <translation>时序集合</translation>
    </message>
    <message>
        <source>Unlimited</source>
        <translation>不限</translation>
    </message>
    <message>
        <source>Clear Filter</source>
        <translation>清除筛选</translation>
    </message>
    <message>
        <source>Start</source>
        <translation>起始</translation>
    </message>
    <message>
        <source>Deadline</source>
        <translation>截止</translation>
    </message>
    <message>
        <source>Previous Page</source>
        <translation>上一页</translation>
    </message>
    <message>
        <source>Next Page</source>
        <translation>下一页</translation>
    </message>
    <message>
        <source>Preview Current Epoch</source>
        <translation>预览当前时相</translation>
    </message>
    <message>
        <source>Compare Two Epochs</source>
        <translation>对比两时相</translation>
    </message>
    <message>
        <source>Page %1 / %2</source>
        <translation>第 %1 / %2 页</translation>
    </message>
    <message>
        <source>This scene is hidden by the current date filter.</source>
        <translation>该场景被当前日期筛选隐藏。</translation>
    </message>
    <message>
        <source>The data catalog is unavailable.</source>
        <translation>数据目录不可用。</translation>
    </message>
    <message>
        <source>No time series collections in the project yet — create one via the Agent temporal tools or a STAC import.</source>
        <translation>工程中尚无时序集合——可通过 Agent 时序工具或 STAC 导入创建。</translation>
    </message>
    <message>
        <source>Page 1 / %1</source>
        <translation>第 1 / %1 页</translation>
    </message>
    <message>
        <source>Page 1 / 1</source>
        <translation>第 1 / 1 页</translation>
    </message>
    <message>
        <source>Failed to parse the description document: %1</source>
        <translation>描述文档解析失败：%1</translation>
    </message>
    <message>
        <source>%1 to %2</source>
        <translation>%1 至 %2</translation>
    </message>
    <message>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <source>%1 — %2 scenes, %3</source>
        <translation>%1 — %2 个场景，%3</translation>
    </message>
    <message>
        <source>%1 scenes have no acquisition time (the precheck will reject them)</source>
        <translation>%1 个场景缺少时间（预检会拒绝它们）</translation>
    </message>
    <message>
        <source>Mean cloud cover %1% (%2 scenes reported)</source>
        <translation>平均云量 %1%（%2 个场景已报告）</translation>
    </message>
    <message>
        <source>No cloud cover report</source>
        <translation>无云量报告</translation>
    </message>
    <message>
        <source>QA：%1</source>
        <translation>QA：%1</translation>
    </message>
</context>
<context>
    <name>sicnu::app::VectorStructureSection</name>
    <message>
        <source>No vector layer selected.</source>
        <translation>未选中矢量图层。</translation>
    </message>
    <message>
        <source>&lt;tr&gt;&lt;td&gt;%1&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;</source>
        <translation>&lt;tr&gt;&lt;td&gt;%1&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;</translation>
    </message>
    <message>
        <source>&lt;tr&gt;&lt;td colspan='2'&gt;... and %1 more fields&lt;/td&gt;&lt;/tr&gt;</source>
        <translation>&lt;tr&gt;&lt;td colspan='2'&gt;…及其余 %1 个字段&lt;/td&gt;&lt;/tr&gt;</translation>
    </message>
    <message>
        <source>Editing (unsaved changes)</source>
        <translation>编辑中（有未保存修改）</translation>
    </message>
    <message>
        <source>Editing</source>
        <translation>编辑中</translation>
    </message>
    <message>
        <source>Read-only</source>
        <translation>只读</translation>
    </message>
    <message>
        <source>Editable (not started)</source>
        <translation>可编辑（未开始）</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;Features&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Selected features&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Geometry type&lt;/td&gt;&lt;td&gt;%4&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%5&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Edit state&lt;/td&gt;&lt;td&gt;%6&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;Fields&lt;/td&gt;&lt;td&gt;%7&lt;/td&gt;&lt;/tr&gt;%8%9&lt;/table&gt;</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;table cellspacing='2'&gt;&lt;tr&gt;&lt;td&gt;要素&lt;/td&gt;&lt;td&gt;%2&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;选中要素&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;几何类型&lt;/td&gt;&lt;td&gt;%4&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;CRS&lt;/td&gt;&lt;td&gt;%5&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;编辑状态&lt;/td&gt;&lt;td&gt;%6&lt;/td&gt;&lt;/tr&gt;&lt;tr&gt;&lt;td&gt;字段&lt;/td&gt;&lt;td&gt;%7&lt;/td&gt;&lt;/tr&gt;%8%9&lt;/table&gt;</translation>
    </message>
    <message>
        <source>Undefined</source>
        <translation>未定义</translation>
    </message>
    <message>
        <source>Field</source>
        <translation>字段</translation>
    </message>
</context>
<context>
    <name>sicnu::app::WorkspaceBrowserPanel</name>
    <message>
        <source>Search name / locator…</source>
        <translation>搜索名称 / 定位符…</translation>
    </message>
    <message>
        <source>Assets</source>
        <translation>资产</translation>
    </message>
    <message>
        <source>Results</source>
        <translation>结果</translation>
    </message>
    <message>
        <source>Runs</source>
        <translation>运行</translation>
    </message>
    <message>
        <source>Datasets</source>
        <translation>数据集</translation>
    </message>
    <message>
        <source>All kinds</source>
        <translation>全部类型</translation>
    </message>
    <message>
        <source>All states</source>
        <translation>全部状态</translation>
    </message>
    <message>
        <source>All sensors</source>
        <translation>全部传感器</translation>
    </message>
    <message>
        <source>Select an entity for governed details…</source>
        <translation>选择实体查看治理详情…</translation>
    </message>
    <message>
        <source>Health check</source>
        <translation>健康检查</translation>
    </message>
    <message>
        <source>Scan Folder...</source>
        <translation>扫描文件夹…</translation>
    </message>
    <message>
        <source>Import Remote URL...</source>
        <translation>导入远程 URL…</translation>
    </message>
    <message>
        <source>%1 entities</source>
        <translation>%1 个实体</translation>
    </message>
    <message>
        <source>Errors: %1  Warnings: %2  Infos: %3</source>
        <translation>错误：%1  警告：%2  提示：%3</translation>
    </message>
    <message>
        <source>… (%1 more findings omitted)</source>
        <translation>…（其余 %1 项发现已省略）</translation>
    </message>
    <message>
        <source>Asset: %1</source>
        <translation>资产：%1</translation>
    </message>
    <message>
        <source>Identity: %1 (revision %2)</source>
        <translation>标识：%1（修订 %2）</translation>
    </message>
    <message>
        <source>Locator: %1</source>
        <translation>定位符：%1</translation>
    </message>
    <message>
        <source>Fingerprint: %1</source>
        <translation>指纹：%1</translation>
    </message>
    <message>
        <source>not computed</source>
        <translation>未计算</translation>
    </message>
    <message>
        <source>Availability: %1 (verified %2)</source>
        <translation>可用性：%1（校验于 %2）</translation>
    </message>
    <message>
        <source>never</source>
        <translation>从未</translation>
    </message>
    <message>
        <source>Upstream (≤5): %1</source>
        <translation>上游（≤5）：%1</translation>
    </message>
    <message>
        <source>Downstream (≤5): %1</source>
        <translation>下游（≤5）：%1</translation>
    </message>
    <message>
        <source>Choose the Folder to Import</source>
        <translation>选择要导入的文件夹</translation>
    </message>
    <message>
        <source>An import task is already running.</source>
        <translation>已有导入任务在运行中。</translation>
    </message>
    <message>
        <source>Scanning %1 in the background...</source>
        <translation>正在后台扫描 %1 …</translation>
    </message>
    <message>
        <source>Import Remote Data</source>
        <translation>导入远程数据</translation>
    </message>
    <message>
        <source>One remote COG/URL per line (e.g. a STAC asset href):</source>
        <translation>每行一个远程 COG/URL（例如 STAC 资产 href）：</translation>
    </message>
    <message>
        <source>Remote import finished: %1 registered, %2 duplicates, %3 failed</source>
        <translation>远程导入完成：注册 %1，重复 %2，失败 %3</translation>
    </message>
</context>
<context>
    <name>sicnu::app::WorkspaceGovernanceModel</name>
    <message>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <source>Kind</source>
        <translation>类型</translation>
    </message>
    <message>
        <source>State</source>
        <translation>状态</translation>
    </message>
    <message>
        <source>Locator / Run</source>
        <translation>定位符 / 运行</translation>
    </message>
    <message>
        <source>Sensor</source>
        <translation>传感器</translation>
    </message>
    <message>
        <source>Updated</source>
        <translation>更新时间</translation>
    </message>
</context>
<context>
    <name>sicnu::workflow::gui::PipelineCanvasWidget</name>
    <message>
        <source>Run up to node '%1'</source>
        <translation>执行至此节点 '%1'</translation>
    </message>
    <message>
        <source>View Live Execution Log</source>
        <translation>查看实时执行日志</translation>
    </message>
    <message>
        <source>Delete Node</source>
        <translation>删除节点</translation>
    </message>
    <message>
        <source>Delete Link</source>
        <translation>删除连线</translation>
    </message>
    <message>
        <source>Fit in Window</source>
        <translation>适应窗口</translation>
    </message>
    <message>
        <source>100% View</source>
        <translation>100% 视图</translation>
    </message>
</context>
<context>
    <name>sicnu::workflow::gui::PipelineEditorDock</name>
    <message>
        <source>Task Workflow Editor</source>
        <translation>任务流程编辑器</translation>
    </message>
    <message>
        <source>New</source>
        <translation>新建</translation>
    </message>
    <message>
        <source>New Workflow</source>
        <translation>新建工作流</translation>
    </message>
    <message>
        <source>Open</source>
        <translation>打开</translation>
    </message>
    <message>
        <source>Open Workflow (.json) — or via the command palette workflow.open</source>
        <translation>打开工作流 (.json) — 或通过命令面板 workflow.open</translation>
    </message>
    <message>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <source>Save Workflow (.json)</source>
        <translation>保存工作流 (.json)</translation>
    </message>
    <message>
        <source>Run Full Pipeline</source>
        <translation>运行全流程</translation>
    </message>
    <message>
        <source>Run Full Pipeline (scheduled in topological order)</source>
        <translation>运行全流程 (按拓扑顺序调度执行)</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <source>Stop the running pipeline task</source>
        <translation>停止正在运行的流程任务</translation>
    </message>
    <message>
        <source>Fit in Window</source>
        <translation>适应窗口</translation>
    </message>
    <message>
        <source>Fit the window to show all nodes</source>
        <translation>适应窗口显示所有节点</translation>
    </message>
    <message>
        <source>100% View</source>
        <translation>100% 视图</translation>
    </message>
    <message>
        <source>Reset to 100% Scale</source>
        <translation>恢复 100% 比例</translation>
    </message>
    <message>
        <source>Delete Selected</source>
        <translation>删除选中</translation>
    </message>
    <message>
        <source>Delete selected nodes or links (Delete)</source>
        <translation>删除选中节点或连线 (Delete)</translation>
    </message>
    <message>
        <source>Preset Templates</source>
        <translation>预设模板</translation>
    </message>
    <message>
        <source>Expand the preset template library</source>
        <translation>展开预设模板库</translation>
    </message>
    <message>
        <source>Confirm New Pipeline</source>
        <translation>新建流程确认</translation>
    </message>
    <message>
        <source>Creating a new pipeline will clear the current canvas. Continue?</source>
        <translation>新建流程将清空当前画布。是否继续？</translation>
    </message>
    <message>
        <source>Open Pipeline Definition JSON</source>
        <translation>打开流程定义 JSON</translation>
    </message>
    <message>
        <source>JSON Files (*.json)</source>
        <translation>JSON 文件 (*.json)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Cannot open file: %1</source>
        <translation>无法打开文件: %1</translation>
    </message>
    <message>
        <source>Parse Error</source>
        <translation>解析错误</translation>
    </message>
    <message>
        <source>Format Error</source>
        <translation>格式错误</translation>
    </message>
    <message>
        <source>Save Pipeline Definition JSON</source>
        <translation>保存流程定义 JSON</translation>
    </message>
    <message>
        <source>Cannot write file: %1</source>
        <translation>无法写入文件: %1</translation>
    </message>
    <message>
        <source>Succeeded</source>
        <translation>成功</translation>
    </message>
    <message>
        <source>Pipeline definition saved to: %1</source>
        <translation>流程定义已保存至: %1</translation>
    </message>
    <message>
        <source>Confirm Template Loading</source>
        <translation>加载模板确认</translation>
    </message>
    <message>
        <source>Loading a preset template will replace the pipeline on the canvas. Continue?</source>
        <translation>加载预设模板将替换当前画布中的流程。是否继续？</translation>
    </message>
</context>
<context>
    <name>sicnu::workflow::gui::PipelineScene</name>
    <message>
        <source>The workflow canvas is empty
Pick a preset template on the right or build a pipeline with the toolbar</source>
        <translation>工作流画布为空
从右侧选择预设模板或使用工具栏构建流程</translation>
    </message>
</context>
<context>
    <name>sicnu::workflow::gui::PresetCatalogWidget</name>
    <message>
        <source>Preset Pipeline Templates</source>
        <translation>预设流程模板</translation>
    </message>
    <message>
        <source>Search pipeline templates...</source>
        <translation>搜索流程模板...</translation>
    </message>
    <message>
        <source>Select a pipeline template above to see its description</source>
        <translation>请选择上方流程模板查看说明</translation>
    </message>
    <message>
        <source>Load Template onto Canvas</source>
        <translation>加载模板到画布</translation>
    </message>
    <message>
        <source>Landsat Vegetation Indices and Change Detection</source>
        <translation>Landsat 植被指数与变化检测</translation>
    </message>
    <message>
        <source>Remote-Sensing Change Detection</source>
        <translation>遥感变化检测</translation>
    </message>
    <message>
        <source>Covers Landsat import, NDVI computation and a two-date change detection pipeline.</source>
        <translation>包含 Landsat 数据导入、植被指数 (NDVI) 计算以及前后时相变化检测流。</translation>
    </message>
    <message>
        <source>DEM Elevation and Slope Analysis</source>
        <translation>DEM 高程与坡度分析</translation>
    </message>
    <message>
        <source>Terrain Analysis</source>
        <translation>地形分析</translation>
    </message>
    <message>
        <source>Covers DEM import, slope computation and hillshade terrain rendering.</source>
        <translation>包含高程 DEM 导入、坡度 (Slope) 计算以及山体阴影 (Hillshade) 地形渲染。</translation>
    </message>
    <message>
        <source>OBIA Object-Based Segmentation and Classification</source>
        <translation>OBIA 面向对象分割与分类</translation>
    </message>
    <message>
        <source>Smart Classification</source>
        <translation>智能分类</translation>
    </message>
    <message>
        <source>Covers high-resolution import, MeanShift object-based segmentation and Random Forest object classification.</source>
        <translation>包含高分辨率影像导入、MeanShift 面向对象分割以及随机森林 (Random Forest) 对象分类。</translation>
    </message>
    <message>
        <source>Water Index (NDWI) Extraction</source>
        <translation>水体指数 (NDWI) 提取</translation>
    </message>
    <message>
        <source>Band Math</source>
        <translation>波段运算</translation>
    </message>
    <message>
        <source>Covers the normalized-difference water index from the green and NIR bands.</source>
        <translation>包含绿光与近红外波段水体归一化差值指数计算。</translation>
    </message>
    <message>
        <source>Remote-sensing classification, denoising filters and class merging</source>
        <translation>遥感分类、降噪过滤与类别合并</translation>
    </message>
    <message>
        <source>Remote-Sensing Image Classification</source>
        <translation>遥感图像分类</translation>
    </message>
    <message>
        <source>Covers supervised/unsupervised classification, 3x3 majority-filter denoising and class recoding/merging end to end.</source>
        <translation>包含监督/非监督分类、3x3 众数滤波降噪以及类别重编码合并全流程。</translation>
    </message>
    <message>
        <source>No matching preset pipeline template</source>
        <translation>无匹配的预设流程模板</translation>
    </message>
</context>
</TS>