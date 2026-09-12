# 实验11：制图出图——专题数据生产与合规地图排版

> 平台能力：`rs:temporal_composite` / `rs:threshold_raster`（headless 管道）+
> MapSpec 声明式排版 + `cartography:compose` / `cartography:export`（代理工具，GUI 二进制 `--mcp` 模式）

## 实验目的

1. 理解「分析结果 → 制图产品」的完整链路：专题栅格生产的可复现管道与地图排版的声明式分离；
2. 掌握时序数据的合成制图：把 12 期 NDVI 压缩为「年内均值」单幅专题图层；
3. 掌握阈值法专题分级（Otsu 自动阈值）并解释其在制图语境下的含义（类别划分 ≠ 数据分析）；
4. 掌握专题图合规五要素：标题、图例、比例尺、指北针、来源注记，能用 MapSpec 声明式文档表达它们；
5. 理解制图交付治理：output 块只声明交付面、导出必须是显式动作（`cartography:export`）。

## 原理讲解

### 1. 从分析结果到专题图层

分析产物要成为地图，需要一次「制图抽象」：选一个主题变量（本实验：年内 NDVI 均值 = 全年平均光合能力），再把它离散化为读者可理解的类别（本实验：Otsu 阈值把均值直方图切成植被/非植被两类）。注意方向性：**分析时阈值是「假设」，制图时阈值是「表达选择」**——同一份数据允许多种分级方案，但图上必须注明分级方法。

- 年内均值：x̄ = (1/N)·Σ x_t
- Otsu：σ_B²(t) = ω_0(t)ω_1(t)[μ_0(t) − μ_1(t)]² 取最大

### 2. 专题图合规五要素

一张可交付的专题图至少要回答五个问题：图叫什么（**主标题**）、颜色/符号代表什么（**图例**）、多远是一公里（**比例尺**）、哪边是北（**指北针**）、数据从哪来（**来源注记**，含坐标系 EPSG:4326 与时间基准）。平台 preflight 把五要素编码为规则（`MAP_MISSING_TITLE/LEGEND/SCALE_BAR/NORTH_ARROW/SOURCE_NOTE`），缺失会被逐条点名——这就是「合规」的机器可查形式。

### 3. MapSpec：把排版写成文档

MapSpec 是版本化的声明式制图文档（当前 **spec_version 5**；集合含 map_frames / legends / scale_bars / north_arrows / titles / labels / source_notes / constraints / output 等，每项带稳定 `id` 与可选 `semantic_role`，几何用页面毫米 `rect_mm`）。`MapSpecCompiler` 把它翻译成 QGIS 打印布局并渲染。声明式排版的好处：地图可评审、可 diff、可复现——改文字不改几何，改几何不动数据。

### 4. 交付治理：声明 vs 导出

spec_version 5 起文档可携带 `output` 块（formats/dpi/dir）**声明**交付面，但编译绝不自动导出——导出必须显式调用 `cartography:export`（PNG/PDF/SVG，72–1200 dpi，原子写出 + SHA-256）。这把「生成」与「交付」分成两个可审计的动作：导出前 validate/preflight 必须先通过，导出后有字体替换诊断可查。

## 实验数据

| 项目 | 说明 |
|------|------|
| 源数据 | 复用实验8 的 12 期 256×256 Red/NIR 影像（2024 年逐月，EPSG:4326） |
| AOI | [102.5, 30.244, 102.756, 30.5]（与影像 geotransform 一致） |
| 专题产品 | 年内均值合成 NDVI → Otsu 二值长势掩膜（UInt8，1=植被） |
| 页面 | A4 横向 297×210 mm，导出 200 dpi |
| 数据规格 | `data/labs/data-specs/lab11_cartographic_mapping.json`（复用实验8 规格 + 制图元数据） |
| 本地临时数据 | `python3 scripts/gen_lab_fixtures.py temporal --out data/labs/_tmp`（gitignored） |

## 实验步骤

### 11.1 专题变量设计

明确地图要回答的问题：「2024 年实验区植被长势的空间分布」。选定专题变量 = 年内 NDVI 均值；分级 = Otsu 二值。讨论其他方案（固定阈值 0.45、四级分级）的取舍，把设计决策写进实验报告。

### 11.2 生产专题栅格（headless 管道）

```bash
QT_QPA_PLATFORM=offscreen build/sicnu_geo_rs_cli \
  --pipeline data/labs/pipelines/lab11_cartographic_mapping.pipeline.json
```

三步：`ndvi`（12 期 NDVI 栈）→ `composite`（年内均值）→ `thematic`（Otsu 掩膜）。检查合成图统计与掩膜值域 {0,1}，记录 Otsu 阈值与植被占比。

### 11.3 声明式排版（MapSpec）

阅读 `data/labs/mapspecs/lab11_thematic_map.mapspec.json`：识别合规五要素分别对应哪个集合；把 `map_frames[0].layers` 改成你加载专题栅格后的**实际图层名**；运行 `cartography:validate` 与 `cartography:preflight` 确认零缺失。

### 11.4 编译与导出

- GUI 路径：把 `thematic_mask.tif` 加载进工程（图层命名 `lab11_thematic_mask`），用代理/助手执行 compose 与 export；
- Headless 路径：GUI 二进制 `--mcp` 模式 + 脚本：

```bash
QT_QPA_PLATFORM=offscreen python3 scripts/export_lab_map_mcp.py \
  --binary build/sicnu_geo_rs \
  --mapspec data/labs/mapspecs/lab11_thematic_map.mapspec.json \
  --directory data/labs/_tmp/out/lab11/map
```

导出前脚本会先跑 validate + preflight（合规门禁），再 compose，最后显式 export。

### 11.5 读图与互评

用合规五要素清单互查出图：标题是否点明主题与时间、图例是否可读、比例尺在 EPSG:4326 下如何换算、来源注记是否含坐标系与数据基准。

## 预期结果

| 产物 | 预期 | 判分容差 |
|------|------|----------|
| `ndvi_composite_mean.tif` | 林地 0.65–0.85、水体 −0.20–0.00、扰动地块被采伐拉低 | 意图 K1 |
| `thematic_mask.tif` | UInt8 {0,1}，植被占比 25%–55% | 意图 K2 |
| 阈值统计 | Otsu 阈值 ∈ [−0.05, 0.5] 且 masked 比例自洽 | 意图 K3 |
| MapSpec 文档 | validate 零问题、preflight 五要素零缺失 | 意图 K4 |
| 导出 PNG | A4 横向 @200 dpi，非空 | 意图 K5 |

判分意图全文：`data/labs/grading/lab11_cartographic_mapping.intent.json`（判分器由 D4 实现）。

## 思考题

1. 同一份年内均值 NDVI，固定阈值 0.45 与 Otsu 自动阈值会给出什么不同的「植被」范围？哪种更可复现？哪种更可跨区比较？
2. EPSG:4326 经纬度网格下比例尺怎么画？为什么高纬度地区「1 厘米 = X 公里」的注记会失真？
3. MapSpec 里删掉 `source_notes` 集合，preflight 会报什么？为什么「数据从哪来」是合规要素而不是可选项？
4. v5 为什么规定「output 块只声明、编译不自动导出」？把「生成」与「交付」分开能防住什么事故？
5. 把 `map_frames[].layers` 改成一个不存在的图层名，编译/导出在哪一步失败？这类「引用完整性」为什么要在校验而非渲染时发现？

## 术语表

| 术语 | 英文 | 释义 |
|------|------|------|
| 专题图 | thematic map | 以单一主题变量为主体的地图产品，与普通影像图相对 |
| 合成 | composite | 把多期观测压缩为单幅代表值（均值/最优像元/中位数）的制图预处理 |
| MapSpec | MapSpec | 平台版本化声明式制图文档（当前 spec_version 5），由 MapSpecCompiler 翻译为 QGIS 打印布局 |
| 合规五要素 | cartographic compliance set | 标题、图例、比例尺、指北针、来源注记；preflight 以 MAP_MISSING_* 规则逐条检查 |

## 诚实范围（可执行子集）

- 制图 compose/preflight/validate/export **不在** `sicnu_geo_rs_cli` 管道算子面内——它们是代理工具（GUI 二进制 `--mcp` 模式或 GUI）。本实验 headless 证据由两层构成：数据链用 runner 管道；排版导出用实验链测试（MapSpecCompiler 离屏编译 + 导出）与 MCP 导出脚本。「把 compose/export 做成 rs: 管道算子」记入 ISSUES.md；
- 平台级 `test_mapspec` 当前为红（`docs/verification/READINESS.md:38`，**由 D10 负责修绿**）：本实验判分断言自包含（K4/K5 针对本实验自己的文档与导出），不依赖该全局用例；
- 文档中「MapSpec 3.0」沿用平台品牌名；文档模型当前 envelope 为 spec_version 5，以 `src/agent/mapspec/mapspec.h` 版本史为准。

完整算子缺口清单见仓库 `ISSUES.md`。
