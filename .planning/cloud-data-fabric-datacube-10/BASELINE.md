# BASELINE — cloud-data-fabric-datacube-10

* Baseline SHA: `7d78059d1a6d316d606656759a506d17bc5e3b55`（origin/master @ 2026-09-13）
* Worktree: `/home/kevin/projects/rs-studio/exp-rs-cloud-data-fabric-datacube-10`
* Branch: `zcode/cloud-data-fabric-datacube-10`
* 主机: linux x64, GCC, GDAL 3.13.3（pkg-config），磁盘余量 ~56 GB（限制：仅一个新 build 树）

## 与本 Track 最相关的已合并 PR（近 30 内）

| PR | 内容 | 与本 track 关系 |
| --- | --- | --- |
| #958 | /goal /loop 命令体系 review + 模板 | 本 track 的指令权威（goal-template） |
| #957 | whole-repo line review dossier | F-OPS-1..5 / F-PI-1..2 新 findings；F-OPS-4 在 io 家族 |
| #956 | GF/ZY-3/HJ-1 产品适配器 | products/（不在本 track 范围，复用不改动） |
| #955 | 光谱库先验 | 无交集 |
| #954 | 验证基线绿（offline 降级） | 验证基线参考 |
| #950 | 111 个 rs 算子 capability knowledge layer | 新 io 算子需检查其 metadata 是否需要同步登记 |
| #946–#949, #951, #952, #953 | lab/i18n 族 | 无交集 |

## 前代 fabric 交付（不得重做）

8.0（`.planning/geospatial-data-fabric-8/FINAL_REPORT.md`）：
remote validators、bounded HTTP、`/vsirangecache/` VSI、STAC search、multidim slices、
atomic writers、doctor v2、canonical metadata、remote identity token + execution-cache
bridge、STAC UTC 归一、GeoParquet 写认证、Doctor 3.0、CLI `data identity`/`data cache check`。

9.0（`.planning/geospatial-data-fabric-9/FINAL_REPORT.md`）：
M0 完整性（move/rollback/dir-fsync/FidelityLoss）、M1 统一身份（li1/sd1/ri1）、
M2 range 去重/全局 in-flight 字节闸/truncated-206 闸、M3 磁盘块层（内容寻址 LRU 校验和）、
M4 STAC relative href 解析 + 有界查询缓存、M5 EO cube 描述符（4D/5D lazy、轴 instants、
JSON 对称）、M6 向量/列式（extent/stats pushdown、FlatGeobuf、批写）、M7 catalog 查询
原语（`src/geospatial/catalog`）、M8 locality hints（`src/geospatial/hints`）、
M9 doctor 能力矩阵 + scale9 证据。

## 历史 findings 对照（去重）

* `review/DEDUPE.md`（#957）：250 条历史 issue 全 CLOSED；新 findings F-OPS-1..5、
  F-PI-1..2 均已立 draft。F-OPS-4（io:reproject srcCrsOverride 死参数，P1）位于
  `src/operators/io/io_operators.cpp:306`（校验消费）vs `:363-370`（io:clip 消费），
  io:reproject 声明而不消费——master 无人认领，本 track 作为候选窄修复（见 GOAL
  Autonomy defaults #6）。
* F9 REVIEW_LOG 的 known limitation：#874 sentinel 精度语义分歧（deliberate）——不在本
  track 重开。
* `gh issue list --state open` → 空（无 open issue）。

## 当前缺口矩阵（10.0 真实增量）

| 缺口 | 证据（验证命令/位置） |
| --- | --- |
| 无跨资产虚拟镶嵌/时间立方体 | `grep -rn "mosaic" src/geospatial --include=*.h` → 空 |
| 无 query planner / chunk plan | `grep -rln "planner\|ChunkPlan" src/geospatial/` → 空 |
| 无 s3:// credential seam / provider scheme | `src/geospatial/util/resource_uri.h:40`（仅归类）；`src/geospatial/remote/` 无 object store 模块 |
| 无统一 catalog service（本地文件集 vs 远程 API 同接口） | `stac_client.h` 只面向远程 API；本地只有 `StacItem::parseFromFile` 单文件 |
| 无 bounded prefetch / offline mirror | `range_cache.h:23-24` 显式否认 mirror；无 prefetch 符号 |
| STAC 查询无 platform/sensor/assetRole 统一入口 | `stac_client.h:46-67`（query 字段无 platform/sensor/role） |

## 基线验证记录

* `git fetch --all --prune` → 干净
* `git checkout master && git pull --ff-only` → Already up to date
* `git worktree add ../exp-rs-cloud-data-fabric-datacube-10 -b zcode/cloud-data-fabric-datacube-10 origin/master` → 成功
