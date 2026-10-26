# R22-03 REVIEW R2（第二独立审查，不复用 R1 结论）

- 审查对象：`b55aa5fd9a`（修复）+ `1381e5d80a`（R1 P3 测试补强），对比 base `911e0b17`
- 审查者：另一独立 subagent（只读；明确指示不得把 R1 结论当默认正确；强制独立构造反例）
- 关键独立结论（非转述，R2 自行推导）：
  1. **混合数学重推导**（python 独立计算）：fw(1)=0.85355/fw(2)=0.5/fw(3)=0.14645/fw(4)=0；x=8/14/18/24 全部波段值、argmax 翻转位置（x=15↔16 之间）、uncertainty margin=0.2525698——与断言逐一吻合。
  2. **per-slot 除法语义正确性反例**：poison 几何 x=18，band2 per-slot=0.1（左 tile 真实预测），全局分母会算出 0.1·0.14645/1.14645=**0.0128**（用从未贡献该 band 的权重伪造近零概率）——per-slot 是对的。
  3. **runMultiInput 与 accumulator 不可达**（:2917-2926 显式拒绝 Feather），无第二构造点。
  4. **fail-closed 不会改变任何全覆盖像元**：旧全局守卫在此类像元恒真，所有 tally/confidence/mask 路径逐位一致；C=1 数值位相同。
  5. resetWindow 无空槽/无 desync；row 指标系一致；权重不可能 NaN；概率模式 slot 数=writerBands 含 uncertainty 波段；:1714 通道一致性检查先于任何 add()。
  6. 测试设施：4D Mat 布局 ((b·C+c)·H+y)·W+x 精确；模型 NaN 直通（mask 恢复只置不清）；Labels 3类→Byte/NoData=255 确证。
- 发现：**0×P0/P1/P2**；2×P3：
  - 注释算术漂移（softmax margin 第四位小数：0.25262→0.25257；断言本身正确）→ **已修**（本轨后续 commit）
  - derived finalize 的 uncertainty 分支为不可达死代码（守卫 :1092 拒绝 derived+uncertainty），但若可达仍正确 → 无需动作，记录备忘
- **VERDICT: APPROVE**

## R1+R2 双审闭环状态

- R1 APPROVE（0 P0-P2，4 P3：测试缺口已补/内存已注记/性能噪声已说明/疑点已证伪）
- R2 APPROVE（0 P0-P2，1 P3 注释已修，1 P3 备忘）
- 两审均由真实分离的 subagent 执行（agent_fbf1e99b / agent_7ebb3b26），非自审。
