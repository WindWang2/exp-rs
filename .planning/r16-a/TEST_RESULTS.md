# R16-A 集成测试结果(全部真实执行,exit code 如实记录;第二轮评审后按精确注册名重测)

环境:Arch Linux x86_64,40 核/62GB;构建租约 flock;cmake/ctest 用
/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin;-j2;QT_QPA_PLATFORM=offscreen;jsoncpp 1.9.8(本机)。

## 重要更正(评审第二轮 P2-1)

初版联合面正则 `^test_faultlab::`/`^test_faultlab_params_r8::` 匹配**零个**测试
(这两个 legacy 目标注册时无 TEST_PREFIX,真实 ctest 名是裸的 "fault lab: …" 43 项与
params_r8 5 个裸名;`ctest -R` 无匹配返回 0 造成静默假绿)。因此初版"171/174"从未
包含既有 faultlab 测试。本文件全部数字已按**精确注册名**模式重测更新。

## 双轨基线(改动前,各自 worktree)

- T01:`ctest -j1 -R "^test_verifier_.*_14::"` → **100 tests passed, EXIT=0**
- T02:grader 四目标(带前缀)→ **49 tests passed, EXIT=0**;
  faultlab legacy 面按实名 `-R "^fault lab:|…params 裸名"` → **48 项**(43+5)

## 子轨终态(各自 worktree;细节见各轨 TEST_MATRIX.md)

- T01:110/110 ×2(七 lane 100 + 新 lane test_verifier_r16a_14 10;该 lane 注册带
  TEST_PREFIX,含于 `^test_verifier_.*_14::` 模式)
- T02:全表面 106 ×2 + 提交后复跑(构成见其 TEST_MATRIX.md,已按第二轮更正:
  43 fault lab + 5 params_r8 + 49 grader + 10 r16a faultlab 等)

## 集成最终面(integration worktree,评审两轮修复全部落地后)

精确名模式(与 ctest -N 实名列表逐一核对):

```
ctest -j1 -R '^test_verifier_.*_14::|^test_r16_a_verifier::|^fault lab:|^train_test_spatial_leakage|^wrong_scale_offset|^integral params|^every param|^test_r16_a_faultlab::|^test_grader_.*::|^test_r16_a_grader::|^test_evidence_seam::'
```

- RUN 1:**100% tests passed, 0 failed out of 223, EXIT=0**
- RUN 2:**100% tests passed, 0 failed out of 223, EXIT=0**

223 构成(ctest -N 分族计数,实测):verifier lane 族 111(100 既有 + 11 r16a——
含评审第一轮 +1 负数预算测试与 r16a lane 原 10)+ faultlab legacy 43 + params_r8 5 +
r16a faultlab 11(含评审第一轮 +1 输出预算测试)+ grader 49 + r16a grader 3(含
评审第一轮 +1 stackLimit 钉死测试)+ evidence_seam 1。

注:`^test_r16_a_verifier::` 计 0——T01 的新 lane 名为 test_verifier_r16a_14(已含于
verifier 族模式),该冗余锚无害。

## 评审修复的 RED→GREEN(双向验证)

- P2-3 budget_usage 负数:stash 修复后 1/1 失败(接受 -5)→ 恢复修复 4/4 通过
- P2-1 输出预算单标量绕过:stash 修复后 4/4 失败(接受 ~258MiB 输出)→ 恢复修复 4/4 通过
- P1-1 stackLimit 钉死:本机 1.9.8 防御性锁(GREEN;RED 仅在 jsoncpp 1.9.5 环境可复现,
  边界行为以探针实证:255 层过、256 层抛 Exceeded stackLimit)

## 跨模块回归

- `test_evidence_seam`(链 sicnu_grader):**1 test, EXIT=0**(含于 223 面)
- `test_canonical_digest_parity`:不链本 PR 三库(链 data/teaching_admin/workflow),
  本 PR 未触碰其任何输入 → 语义不变(未重跑:构建面含 Qt6+qgis 链,成本高且零交集)
- `test_verify_chain_locale_matrix`(链 sicnu_verifier+sicnu_grader+sicnu_faultlab):
  **链接成功、运行环境阻塞**(证据链见下,不称 PASS):
  1. vendored libgdal.so(Debian 构建,paleo 共享前缀)引用 26 个 wrapper2_* LAPACK 符号,
     本机全部库无一定义者;paleo 前缀旧 pcre2 遮蔽系统 pcre2。
  2. 以 /tmp 本地链接桩(abort 哨兵)+ 显式系统 libpcre2-16.so 后链接成功(源码在该
     跨模块目标下编译链接干净;桩未提交)。
  3. 运行时需 11+ 个 Ubuntu soname(libproj.so.25 等),本机与已定位闭包无法完整提供;
     三次最小闭包迭代后按停机规则收案。
  4. 失败与本 PR 无关的客观依据:未定义符号全部位于预编译第三方 .so 内;本 PR 三库为
     jsoncpp-only 叶子库。

## 幂等/确定性

- 223 面两遍输出一致(无 flake);metamorphic(键序打乱同 digest)、同 seed 三跑同摘要、
  locale 独立性测试均含于绿色面内。
