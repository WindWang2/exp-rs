# R23-B BASELINE — 地理空间数据、模型推理运行时与实验可复现闭环

## 执行时点事实（2026-10-27 00:4x–01:1x UTC 现场采集）
- **origin/master = `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`**（与任务起草锚点一致；master 未前移）
- 默认分支 master（gh repo view 确认）；**Open PR = 0**；**Open Issues = 144**（全量落盘 `open_issues_raw.json`）
- 本地主工作树 `/home/kevin/projects/rs-studio/main` = `911e0b17a35`（behind，clean，未触碰）
- 本轨 worktree：`/home/kevin/projects/rs-studio/exp-rs-r23-B-20261026T164130Z`，branch `agent/r23-b-data-model-runtime-20261026T164130Z`，base=ad98a7a704
- 并行轨：`exp-rs-r23-A-20261026T164112Z`（r23-A，构建锁占用中）；历史 worktrees 40+（r20/r22/flash/hardening 均已合并或休眠，不触碰）
- 最近合并：#1715（#1714 冲突标记修复）、#1714 r22-agent、#1713 r22-workbench、#1712 r22-science、#1711 r22-teaching、#1710 r22-data、#1709 r22-inference、#1708 r22-infra、#1677/#1676/#1675/#1674/#1673（r21 五轨）

## 环境与构建
- 主机 cachyos x86_64（62GB RAM，44GB 可用；/home 755GB 空闲），系统 Qt 6.12.0 / GDAL 3.13.3 / PROJ 9.9.0 / GEOS 3.15.0 / OpenCV 5.0.0 / CMake 4.4.x / g++（GCC 16 系，C++20）
- 构建配方沿用 r22-infra 实证配方：`-G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DSICNU_VENDOR_GDAL=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_BUILD_OTB=OFF -DSICNU_EMBED_PYTHON=OFF -DSICNU_TOLERANT_TEST_DISCOVERY=ON`；build dir `build-r23-B`
- 并行约束：全局 `flock $HOME/.cache/exp-rs-build.lock`、`--parallel 2`（绝对上限 4）；ctest `--parallel 1`
- SICNU_ENABLE_TENSORRT / SICNU_ENABLE_OPENVINO 均 OFF（本机无 TRT/OV SDK）——这正是 W1 缺陷不可见的根因

## 前序 B 轨成果在 ad98a7a704 的源码级复验（抽样+代理全查）
- r21-B（#1673）声称修复的 issue 群：**全部 FIXED_OPEN 实锤**（真实修复提交为 10-19 sweep `9b42bc93f7`（operators 群）与 `d9f1b88f1`（runtime 群），经 PR #1662 落地；#1673 本身只贡献 mask_threshold 门控等）。逐条证据见 triage_*.md
- r22-data（#1710）6 issue 全部在位（SSRF authority/缓存几何键/崩溃一致性/并发发布/provenance/窗口校验）
- r20-02（#1669）F1–F6 复验在位（r21-B 已核，本轨抽验 tile_inference_engine 批次/羽化账本仍在）

## 新发现的 LIVE 缺陷（本轨核心输入）
1. **W1 = #1576/#1577 回归（P1 级）**：union 合并 `9988aa15a9` 把 9b42bc93f7 的 OpenVINO selectDevice 定义整块丢弃（调用点仍在）、残留未声明成员 `m_artifactPath` 赋值、`health() const` 锁非 mutable mutex；TRT `artifactPath()` 返回未声明 `m_artifactPath`。两个 option 门控 TU 全部无法编译（所有 lane 默认 OFF ⇒ 无人发现）。g++ stub-compile 实证：OV 3 错、TRT 1 错。
2. **W2 = r21-B 记录的 B5 残留**：derived output.format（labels/mask/confidence）折叠 head 0 类平面，但与 head role/confidence 契约无关联校验（embedding/detection/uncertainty 头或 distance/logit 语义可被折叠成伪造产品）。
3. **W3 = r21-B 记录的 A5 残留**：bundle 导入 metrics 先落库→run 安装失败仅补偿删除（进程崩溃窗口仍留孤儿行；补偿失败只 warning）。根治需跨 run/metric 两表单事务。
4. **W4 = #1637 sub-4 残留**：probeRemote 首跳私网检查不覆盖重定向目标（latent，生产无调用方）。

## 已知主干疑点核验
- **#1491（CI 自 10-11 起 test_io_vector_contract:352 失败）**：本机以同 GDAL 3.13.3 构建运行该族核验（见 TEST_RESULTS.md）；CI 失败为 10-12 run 的环境性快照，若本机绿则归因 CI 环境漂移，不臆断源码缺陷。

## 工具版本
gh 2.x（auth WindWang2）；git 2.4x；ninja 1.12；无 ccache（本轮构建全量）
