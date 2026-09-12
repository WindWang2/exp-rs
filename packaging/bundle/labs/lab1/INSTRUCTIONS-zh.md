# 实验 1：NDVI 植被指数（约 5 分钟）

本目录是离线机房实验包的「实验 1」材料。全程不需要任何网络。

## 前置条件

- 已按 `README-zh.md` 完成 `setup.cmd`（一次即可）。
- `data/samples/landsat_sample.tif` 已存在（没有就先双击包根目录的
  `GENERATE_SAMPLES.cmd`，10 秒生成）。

## 运行（任选其一）

**一键**：双击包根目录的 `RUN.cmd`。它会依次：
生成示例数据（若缺失）→ 运行本流水线 → 用 `ndvi_basics` 规则批改输出 → 写出
`output/lab1_report.json`。

**手动**（学习用）：

```bat
cd /d %BUNDLE_ROOT%
bin\sicnu_geo_rs_cli.exe --offline --pipeline labs\lab1\lab1_ndvi.pipeline.json
bin\sicnu_geo_rs_cli.exe --offline lab --lab ndvi_basics --grade output\lab1_ndvi.tif --out output\lab1_report.json
```

## 流水线说明

`lab1_ndvi.pipeline.json` 只有一行算法：`rs:spectral_index`（NDVI）。

- 输入：`data/samples/landsat_sample.tif`（256×256，7 波段，Landsat OLI 波段序）。
- 波段：`nir: 5`（B5 近红外）、`red: 4`（B4 红）——**从 1 开始数**。
- 输出：`output/lab1_ndvi.tif`（Float32，值域约 -1..1，植被≈0.4..0.8，水体≈-0.2..0）。

## 批改

`ndvi_basics` 规则检查五件事：网格一致、值域、均值/方差窗口、无效像元比例、
直方图双峰性。退出码：0=通过，1=未达线，2=用法错误，3=无法判读。

## 常见问题

- `No pipeline specified`：必须在包根目录运行（相对路径以包根为基准）。
- 报告中出现 `remote` / `offline refusal`：流水线引用了网络数据源；实验包内
  所有材料均为本地文件，请使用本包自带流水线。
