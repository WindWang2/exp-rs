# R16-A 独立评审 第一轮(Reviewer:未参与实现的独立子代理)

完整报告原文:/tmp/r16a-review-round1.md(评审期间只读审计,零仓库改动)。

## 结论:FAIL → 全部 P1/P2 已修(见下)→ 复跑联合面 ×2 GREEN

## 发现与处置

| 级别 | 发现 | 处置 | 证据 |
|---|---|---|---|
| P1-1 | grader parseJsonStrict 未显式设 stackLimit;jsoncpp 默认值版本相关(1.9.5/1.9.6=1000,1.9.8=256),Ubuntu 24.04 CI(1.9.5)上 513–1000 层文本可解析但 canonicalize 拒绝,"parse-legal 必可 canonicalize" 不变量为假 | 已修:builder["stackLimit"]=256 显式钉死;grader_json.cpp 两处注释更正;新增版本无关回归锁测试(parse 254 层必须过、300 层必须类型化拒绝) | 本机 1.9.8 上该测试 GREEN(防御性锁:RED 仅在 1.9.5 可复现,如实记录);jsoncpp 边界探针:255 层过、256 层抛 Exceeded stackLimit |
| P2-1 | faultlab 输出预算仅在节点入口采样,单个巨型标量(控制字符经 \uXXXX 放大 ~6×)可越过 256MiB 预算 | 已修:tryToCanonicalString walk 后补终检(硬上限);新增 RED 测试(43MiB \x01 → ~258MiB 输出必须拒绝) | RED(修复前):4/4 断言失败(接受超预算);GREEN(修复后):4/4 通过 |
| P2-2 | faults.schema.json 的 base_fixture.params 仍声明自由对象,与 loader 新契约(非空拒绝)不一致 | 已修:schema 加 "maxProperties": 0 + 描述 | data/faultlab/faults.schema.json |
| P2-3 | report.cpp readCount 修了上界漏下界:{"nodes":-5} 强转巨大 size_t,错误文案却称 "non-negative integer" | 已修:镜像 requireCount 的 <0 检查;新增 RED 测试 | RED(修复前):1/1 失败(接受 -5);GREEN(修复后):4/4 通过 |
| P3-1 | isBool() 子句死代码 + 注释失实(实证 jsoncpp isIntegral() 对 bool=false,新旧代码均拒 bool) | 已修:移除两处死子句,注释改为实证表述 | 本机探针:bool isIntegral=0 isBool=1 |
| P3-2 | "jsoncpp parses 1e999 无解析错误" 措辞普适化(1.9.5 实为解析错误) | 已修:四处注释加版本限定 | canonical_json.h / fault_scenario.cpp / test_r16_a_faultlab.cpp ×2 |
| P3-3 | 仓库根未跟踪 r16a-init-cache.cmake | 已删(受控副本在 .planning/r16-a/) | git status 无该文件 |
| P3-4 | faultlab 既有 47 项仅在 t02 worktree 复跑 | 联合面正则已含 ^test_faultlab::/^test_faultlab_params_r8::;本轮复跑 ×2 覆盖 | ctest 输出 174 项(171+新增 3)×2 全绿 |
| P3-5 | 深树析构为 jsoncpp 上游属性 | 不作为(两轨已如实注明) | — |
| P3-6 | "depth 256 = 2×stackLimit128" 措辞精度 | 注释已随 P1-1 修一并精确化(计层口径差) | — |

## 修复后回归

- 联合面(含三个新增评审锁测试,共 174 tests)×2:全绿,EXIT=0
- RED→GREEN 证据链:P2-3(1/1 失败→4/4 过)、P2-1(4/4 失败→4/4 过)以 git stash 双向验证;P1-1 为版本防御锁(本机不可 RED,已注明)

## 评审确认无发现的维度(摘)

WP-A -0 归一语义正确且无既有 "-0" 钉子;fault_runner fail-open 全关;API 迁移完整;并发无新共享可变状态;测试断言与实现逐一对应;CMake 零新增链接、无第二摘要器;canonical 字节稳定(RFC 8785 家族零触碰);文档主张与 diff 相符、locale-matrix 阻塞如实记录。
