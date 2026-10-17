# R16-A 集成测试结果(全部真实执行,exit code 如实记录)

环境:Arch Linux x86_64,40 核/62GB;构建租约 flock;/usr/bin/cmake 未装,用
/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin 的 cmake/ctest;-j2;QT_QPA_PLATFORM=offscreen。

## 双轨基线(改动前,各自 worktree)

- T01(t01 worktree,origin/master=46791bc65):`ctest -j1 -R "^test_verifier_.*_14::"` →
  **100 tests passed, EXIT=0**
- T02(t02 worktree):`ctest -j1 -R "^(test_faultlab|...grader...)::"` → **49 tests passed, EXIT=0**
  (T02 子代理另核实 faultlab 全表面为 "fault lab:" 43 + params_r8 4 = 47 项,含在 106 总面内)

## 子轨终态(各自 worktree,两遍 GREEN;细节见各轨 TEST_MATRIX.md)

- T01:110/110 ×2(test_verifier 七 lane 100 + 新 lane test_verifier_r16a_14 10)
- T02:全表面 106 ×2 + 提交后复跑共三次(faultlab 47 + grader 前缀面 + 新 test_r16_a_faultlab 10 + test_r16_a_grader 2 等)

## 集成联合面(integration worktree,cherry-pick 全部 8 commit 后)

命令:`QT_QPA_PLATFORM=offscreen ctest -j1 -R "^test_verifier_.*_14::|^test_r16_a_verifier::|^test_faultlab::|^test_faultlab_params_r8::|^test_r16_a_faultlab::|^test_grader_.*::|^test_r16_a_grader::" --output-on-failure`

- RUN 1:**171 tests, EXIT=0**
- RUN 2:**171 tests, EXIT=0**

## 跨模块回归

- `test_evidence_seam`(链 sicnu_grader 的跨模块消费者):**1 test, EXIT=0**
- `test_canonical_digest_parity`:不链本 PR 三库(链 data/teaching_admin/workflow),
  本 PR 未触碰其任何输入 → 语义不变(未重跑:其构建面含 Qt6+qgis 链,构建成本高且无交集;
  见 REVIEW 审计对 canonical 字节稳定性的论证)。
- `test_verify_chain_locale_matrix`(链 sicnu_verifier+sicnu_grader+sicnu_faultlab 的
  locale 矩阵):**链接成功、运行环境阻塞**,证据链:
  1. 链接层:vendored libgdal.so(Debian 构建,paleo 共享前缀)引用 26 个 wrapper2_* LAPACK
     符号,本机全部库(含系统 BLAS/LAPACK、paleo 闭包、Ubuntu 数学库 deb)无一定义者;
     另 paleo 前缀旧版 libpcre2(无 PCRE2_10.47 版本节点)遮蔽系统 pcre2。
  2. 以 /tmp 本地链接桩(abort 哨兵,仅链接期)+ 显式系统 libpcre2-16.so 重配 build-dev 后
     **链接成功**(证明本 PR 源码在该跨模块目标下编译链接干净;桩未提交)。
  3. 运行层:加载需 11+ 个 Ubuntu soname(libproj.so.25、libhdf5_serial.so.310、
     libgeotiff.so.5、libodbc.so.2、libarmadillo.so.15 等),本机与已定位的各闭包
     (paleo-singlefactor-closure 前缀、pw-seismic-runtime-closure、goal7 deb 缓存 164 包)
     均无法完整提供;三次最小闭包迭代仍缺 → 按停机规则收案为环境阻塞。
  4. 该失败与本 PR 改动无关的客观依据:未定义符号全部位于预编译第三方 .so 内;
     本 PR 三库为 jsoncpp-only 叶子库,不参与其解析;且该目标在本机基线 master 上
     同样无法完成运行时加载。
- 判定:本 PR 拥有面 171×2 GREEN + evidence_seam GREEN;locale-matrix 环境阻塞如实记录,
  不计入通过,不称 PASS。

## 幂等/确定性抽查

- 集成面两遍运行 171 tests 输出一致(无 flake)。
- T01/T02 各自 metamorphic/重复运行摘要一致测试(键序打乱同 digest、同 seed 三跑同摘要)
  含于上述绿色面内。
