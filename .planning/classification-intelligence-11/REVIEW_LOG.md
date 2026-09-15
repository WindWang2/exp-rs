# REVIEW_LOG — Classification & Object Intelligence 11.0

## Review 1（主 agent 全 diff self-review）

- 状态：未开始（Phase 7 前执行）。

## Review 2（独立 adversarial reviewer = subagent #2，只读）

- 状态：未开始。

（每条 finding：severity / 位置 / 描述 / disposition(修复 commit 或理由) / 重跑 gate。）

## Review 2（独立 adversarial reviewer = subagent #2，只读，2026-09-16）

范围：`git diff origin/master...HEAD` 全部 55 文件 + 契约文档 + DECISIONS；
FNV 已知答案经外部 python 参考实现独立复算一致。

### 结论：P0=0；P1=3；P2=6；P3 若干 → 全部 disposition 如下

| # | 级别 | 位置 | 问题 | disposition |
|---|---|---|---|---|
| R2-1 | P1 | rs_classifier_normalbayes.cpp save | legacy 加载（无类序）后 save 写出 `[]` 旁车 → 模型自毁不可再加载 | **修复**：`mClassLabels.isEmpty()` 时 save 返回 false（fail-closed） |
| R2-2 | P1 | rs_classification_pipeline.cpp ModelSaveFailed | backend->save 失败（NB labels.json / SVM ovr 分片写失败）后主 YAML 残留，可与旧 sidecar 配对 | **修复**：失败分支补 `QFile::remove(modelSavePath)`；SVM save 失败时清除已写出的 `.ovr<i>.yaml` 分片 |
| R2-3 | P1 | pipeline 逐像素块 + docs §5 | 退化概率行（NB 下溢全零）uncertainty 三波段写 -1 但契约未披露；概率栅格同行写 0（与"真置信 0"不可区分）| **修复**：概率栅格同行也写 -1、meanConfidence 排除退化行（header 注释同步）；§5 补披露（含 NB predictProb 下溢的 ADR 0094 历史行为对照）|
| R2-4 | P2 | rs_feature_schema.cpp fromJson | 删除 fingerprint 键可旁路漂移门 | **修复**：fingerprint 字段强制、不一致即拒绝 |
| R2-5 | P2 | fitPlatt/fitIsotonic | classIds 之外的标签被静默当负样本 | **修复**：fit 入口 `columnOf<0` → false；补负测试（未知标签 Platt+isotonic）|
| R2-6 | P2 | rs_spatial_cross_validation.cpp | groupOverlap 计样本数，文档语义为去重组数 | **修复**：改为去重组数（既有测试 `>0` 断言仍成立）|
| R2-7 | P2 | randomFolds std::shuffle | 跨平台排列不可复现 | **修复**：手写 Fisher-Yates + `rng() % n` |
| R2-8 | P2 | pipeline rename | uncertainty 栅格 rename 失败静默 | **修复**：检查 rename 返回值，失败报 OutputCreateFailed |
| R2-9 | P2 | e2e 测试 | LABELS 调试 fprintf 残留 | **修复**：移除 |
| R2-10 | P3 | training 节无条件写 | 与"缺省不写"漂移 | **修复**：仅 trainSamples>0 时写 |
| R2-11 | P3 | Platt ridge/收敛语义 | 文档缺"无条件 ridge / maxIter 耗尽接受有限解" | **修复**：§3 补记 |
| R2-12 | P3 | spatial cv 头注释 "round-robin" | 与 balance-first 实现漂移 | **修复**：改写 |
| R2-13 | P3 | postprocess 头自我更正残迹 | 注释混乱 | **修复**：清理 |
| R2-14 | P3 | find() 对缺失 key 的 operator[] 插入 | 防御缺口 | **修复**：count 守卫 |
| R2-15 | P3 | scale 测试冗余 labels 赋值 | 复制残留 | **修复**：移除 |
| R2-16 | P3 | studio REQUIRE(true) 烟雾门弱 | 面板内部私有 | **接受**：纯数据面板的过滤逻辑已由 setter 语义锁定，绘制由 smoke 保障；深化属 follow-up |
| R2-17 | P3 | votes 双层 map 内存 | 2e6 段上限下可观 | **接受**：maxSegments 拒绝线已存在；量级记录于 PERFORMANCE.md |
| R2-18 | P3 | sidecar v2 被旧读者拒绝 | 单向不兼容 | **接受**：D-008 已裁决 |
| 深坑核验 | — | applyMinAreaRule stale offender/members/tie、v2 缺 classOrder、margin 单类行、NoData area 语义、randomFolds QHash 顺序 | 无问题（reviewer 确认）| — |

修复后全量重跑：见 EVIDENCE Phase 8（双跑记录）。
