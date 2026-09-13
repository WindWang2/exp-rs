# 离线机房部署指南（Lab Offline Deployment）

面向教师/机房管理员的操作手册：在一台**无外网**的 Windows 机器上，从解压到
交出全班成绩单。开发者在 Linux/macOS 上的等价流程见文末。

## 1. 构建（教师机，一次）

```bat
git clone <repo> exp-rs && cd exp-rs
scripts\windows\setup.cmd
```

`setup.cmd` 依次执行：依赖检查（vcvars/cmake/ninja/Qt/winflexbison，全部
可用 `SICNU_*` 环境变量覆盖）→ 配置 `build-dev`（与 `configure_wb7.cmd`
同一套开关）→ `ninja -j2` 构建。资源上限硬编码：
`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`。

## 2. 打包离线包

```bat
scripts\build_offline_bundle.cmd --build-dir build-dev
```

产出 `dist\sicnu-lab-<version>\`，包含 `bin\`（程序 + windeployqt（含 VC 运行库）+
QGIS/GDAL 运行时 DLL 闭包）、确定性示例数据、批改规则、流水线、字体、PROJ/GDAL
数据库（proj.db，批改 CRS 断言依赖）、一键脚本与 `manifest.json`（逐文件 SHA-256；
默认体积上限 250 MB，超限构建失败，可用 `--max-mb` 显式放宽）。`QGIS_BIN` 为必填
（指向 QGIS/vcpkg 安装的 bin 目录）。

拷贝整个目录（或压缩后）到每台机房机器 / U 盘。在目标机器上校验完整性（离线可用，
无需源码仓库）：

```bat
D:\sicnu-lab-<version>\VERIFY.cmd
```

## 3. 机房：5 分钟跑完实验 1

双击包根目录 `RUN.cmd`：生成示例数据（若缺失）→ 运行 NDVI 流水线 →
用 `ndvi_basics` 规则批改 → 写出 `output\lab1_report.json`。
全部脚本设置 `SICNU_OFFLINE=1` 并给 CLI 传 `--offline`。

## 4. 机房：一键批改全班

学生提交每人一个文件（如 `20240101.tif`，文件名即学号），放进一个目录：

```bat
GRADE_ALL.cmd D:\lab1_submissions ndvi_basics grades.csv
```

- 流式批改：一次一份，逐行落盘；内存只取决于单个最大栅格，与班级人数无关。
- 容错：某个文件损坏 → 该行 `verdict=error`、错误信息进 `top_deduction` 列，
  其余照常，绝不中断。
- `grades.csv`：UTF-8 **带 BOM + CRLF**，Excel 双击打开中文不乱码；列固定为
  `student_id, lab_id, score, verdict, top_deduction, artifact_path`（默认写在
  提交目录旁边）。
- 退出码：0=全部判读完成（含不及格）；1=存在被隔离的错误行（CSV 仍完整）；
  2=用法错误。

## 5. 离线保证（`--offline` / `SICNU_OFFLINE=1`）

- 启动零网络调用（两种模式都如此）。
- 开启后任何远程请求都被**类型化拒绝**，不发出任何数据包：
  - HTTP 原语（STAC 检索、远程校验器）→ `GeoError(NetworkError)`，报文以
    `offline mode ... refusing` 开头；
  - 所有云端 GDAL 源（`/vsicurl/`、`/vsis3/`…）→ 直接按"不存在"快速失败
    （实测 ~10 ms，无 DNS、无 TCP）；
  - 数据目录解析 → `source.offline_refused` 诊断。
- 本地源不受影响（包括 `/vsimem/`、`/vsizip/` 等本地虚拟文件系统）。
- 断网冒烟：禁网环境下运行 `RUN.cmd` 等价流程即为零网络验证（POSIX 下
  `unset http_proxy https_proxy`，cmd 下 `set http_proxy=`）；执行记录（含资源日志）
  由 D7 track 的本地 EVIDENCE 档案保存。

## 6. MCP（D9 前置条件）

```bat
scripts\windows\check_mcp.cmd
```

以 `--mcp` 启动 `sicnu_geo_rs`，通过 Windows stdio 发送
`initialize` → `notifications/initialized` → `tools/list` 一轮发现握手，
断言响应 `serverInfo.name == "exp-rs-mcp"` 且工具列表为数组。

## 7. Linux/macOS 等价流程

```sh
cmake --preset dev-default && cmake --build build-dev -j2 --target sicnu_geo_rs_cli sicnu_generate_samples
scripts/build_offline_bundle.sh --build-dir build-dev --verify dist/sicnu-lab-*
export SICNU_LAB_RULES_DIR="$PWD/data/labs/grading"   # 包内则为 <bundle>/data/labs/grading
sicnu_geo_rs_cli --offline --pipeline labs/lab1/lab1_ndvi.pipeline.json
sicnu_geo_rs_cli --offline lab --lab ndvi_basics --batch submissions/ --csv grades.csv
```

## 8. 测试

```sh
ctest -R "lab_batch|lab_grading|offline" -j1 --output-on-failure   # 资源受限
```
