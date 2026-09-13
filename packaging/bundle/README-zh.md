# SICNU GEO RS 离线实验包（教师快速开始）

本包面向**无外网的机房 Windows 机器**。解压即用：不需要联网、不需要安装
Python、不需要注册任何账号。整个过程零网络访问。

## 包内容

| 目录/文件 | 用途 |
| --- | --- |
| `bin\` | 命令行程序（CLI、示例数据生成器）及全部运行时 DLL |
| `data\samples\` | 确定性示例遥感数据（可随时重新生成，字节一致） |
| `data\labs\grading\` | 各实验的批改规则（已知答案 DSL） |
| `data\pipelines\` | 可运行的处理流水线 |
| `data\fonts\` | 报告渲染字体（IBM Plex） |
| `labs\lab1\` | 实验 1 材料（NDVI）+ 中文说明 |
| `RUN.cmd` | 一键运行实验 1（生成→处理→批改→报告） |
| `GENERATE_SAMPLES.cmd` | 重新生成示例数据 |
| `GRADE_ALL.cmd` | 一键批量批改全班作业 |
| `VERIFY.cmd` / `VERIFY.ps1` | 完整性自检（拷贝到新机器 / U 盘后先跑一次） |
| `manifest.json` | 完整性清单（SHA-256） |

## 五分钟上手

1. 解压到任意本地目录（如 `D:\sicnu-lab`），先双击 `VERIFY.cmd` 校验完整性。
2. 双击 `RUN.cmd`。完成三步：生成示例数据 → 运行 NDVI 流水线 →
   自动批改并在 `output\lab1_report.json` 写出报告。
3. 收作业后，把全班提交文件放进一个文件夹（如 `D:\lab1_submissions`，
   每人一个文件，文件名即学号），然后：

   ```bat
   GRADE_ALL.cmd D:\lab1_submissions ndvi_basics grades.csv
   ```

   `grades.csv` 为 UTF-8（带 BOM），Excel 直接双击打开中文不乱码。
   列：`student_id, lab_id, score, verdict, top_deduction, artifact_path`。
   某个文件损坏不会中断整批——该行 verdict 记为 `error`，其余照常。

## 学生提交什么？

实验 1 让每位学生运行 `labs\lab1\lab1_ndvi.pipeline.json`（可改参数），
提交生成的 `lab1_ndvi.tif`。教师端只看批改 CSV。

## 常见问题

- **完全离线如何保证？** 所有脚本都设置 `SICNU_OFFLINE=1`，CLI 运行一律带
  `--offline`：任何需要网络的数据源都会得到明确的拒绝信息，而不是挂住。
- **换一台机器结果一样吗？** 示例数据由固定种子生成，字节级一致；批改是
  确定性的——同一份作业在任何机器得分相同。
- **杀毒软件报毒？** 本包不含安装器，只有绿色可执行文件；如被拦截请加白
  `bin\` 目录。
- **给学生的说明**：见 `labs\lab1\INSTRUCTIONS-zh.md`。
