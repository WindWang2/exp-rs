# R16-A-T01 PERFORMANCE — src/verification 性能预算实测

## 是否做了优化

**未做任何优化。** 原因:实测未发现 O(n²)、重复全量复制或其它需修的性能缺陷;
canonical 序列化器是单遍追加式(writer 持有输出串引用逐节点 append,无中间拷贝),
`counted()` 每节点 O(1),排序仅对每层成员名(O(m log m),m≤10000)。本轮唯一的行为改动
(负零归一、整数范围守卫、kind 类型守卫)每处都是每次访问几条比较指令,不在任何热路径
形态上改变复杂度。

## 实测(WP-F)

环境:本机 linux x86_64,GCC 16,CMake build-dev RelWithDephInfo(仓库标准配置)。
测量方式:`/usr/bin/time` 在本机未安装,改用 zsh 内建 `time` + `TIMEFMT='%E %U %S %M'`
(%M 为进程峰值 RSS,KB);数字为单次运行(该用例无随机性,两次运行 wall 相差 <0.01s)。

| 场景 | 命令 | wall | user+sys | 峰值 RSS |
|---|---|---|---|---|
| 49996 节点 canonical 化 + sha256 digest + 50001 节点拒绝对照 | `./test_verifier_r16a_14 "r16a F: a document just under the node budget canonicalizes completely"` | 0.10s | 0.10s | 16192 KB(≈15.8 MiB) |
| 整个 r16a lane(10 用例,含全部预算炸弹构造) | `./test_verifier_r16a_14` | 0.44s | 0.44s | 25356 KB(≈24.8 MiB) |

注:单用例数字含进程启动、Catch2 框架与 fixture 构造(49996 个 Json::Value 节点的堆分配),
并非纯序列化内核时间;作为预算结论已足够 —— 预算上限(50000 节点)整档文档在 0.1s 量级
完成,远低于任何调用方的合理预算。输出规模对照:49996 节点文档 canonical 文本 ≈285 KB
(断言 >250000 字节),低于 4 MiB 输出预算一个数量级,说明节点预算先于输出预算生效,
不会出现"输出预算暗中截断大报告"的形态。

## 结论

- 5 万节点档:0.10s / 15.8 MiB(含启动),无需优化。
- 未发现需要 before/after 对比的真问题;按"仅发现真问题才优化"原则保持零改动。
