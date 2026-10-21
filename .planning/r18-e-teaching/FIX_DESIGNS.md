# R18-E 修复设计速查（应用顺序 = 依赖序；全部待 RED 捕获后应用）

1. **D6/D7** 已实现于 /tmp/r18e-fix-d6d7.patch（lab_session_state verdict stamp + lab_status 权威）。
   注册: src/teaching/CMakeLists.txt += verdict_stamp.cpp/h + link Sicnu::lab_runtime（用 target 名 sicnu_lab_runtime）。
2. **T102f** lab_feedback_projection.cpp fromJson: overall=="pass" && rows.empty() → indeterminate + issue（真实投影至少 1 行）。
3. **D8** src/lab/session_state.cpp sessionFromJson: envelope 检查后加密度不变量——收集全部事件 seq（checkpoint/answer/hint/tool/ref）排序后必须恰为 1..lastSeq（蕴含唯一+无洞+无抬升）。诊断 "lab.session.schema" "seq ledger must be dense: events must be exactly seqs 1..last_seq, each drawn once"。
4. **H3** studio_session.cpp fromJson: 校验 json["schema"]==kStudioSessionSchema，否则返回默认(空) + ok=false；匹配则填字段+ok=true。
5. **D2** lab_batch_runner.cpp discoverSubmissions: 改用 entryInfoList(Dirs|Files|System|Hidden|NoDotAndDotDot, Name) 无过滤枚举+自分类; symlink → refusedEntries (name, "symlinked submission refused (live or dangling)")；非 regular 跳过（隐藏保持不可见）; run() 把 refusedEntries 写进 summaryBodyJson 的 "refused_entries":[{name,reason}]（additive）。
6. **D5** rubric_builder.cpp validateLabRules: weight ∈ (0,100]（拒绝 0）；Σweight == 100 ± 1e-9（错误码 weight_mismatch）；severity ∈ {normal,blocking}（存在时）；passing_score 数值且 ∈ [0,100]（存在时）。同步更新 test_teaching_admin_rubric_weights_r16_c_t06 的零权重 pin（改为拒绝）。
7. **V2** check_runner.cpp runSpec: 循环前 std::set seenIds；重复 id → indeterminateResult(kSpecInvalid, "duplicate check id 'x' — ambiguous spec") + checksUnevaluated + duplicates=true；循环后 addUniqueSorted(outcome.failureCodes, kSpecInvalid)。report.cpp statusOf: 收集全部匹配 id 的 status，combineStatus 最差胜（需 include status_lattice——已有）。
8. **V4** packs_builtin.cpp scientificContractFor: 5 个派生 check 全部改为 visibly-unevaluable：kind=kUnsupportedOperatorKind（"unsupported_operator" 之外的? 不——直接用同一 wire 外 kind "contract_dimension_unsupported"）+ failureCode=kUnsupportedCheckKind + title 保留陈述。他们到达 dispatch → UNSUPPORTED_CHECK_KIND Indeterminate，绝无 vacuous Pass。
9. **V7** checks_relational.cpp runRelationalCheck: readOperand 两 operand 解析后，若 relation==Equals 且 left/right 完全同源（isArtifact==isArtifact && reference==reference && factPath==factPath）→ SPEC_INVALID "operands are tautologically identical"。
10. **V9** spec.cpp canonicalProjection 比较器: id→kind→title 平手时比较 canonicalJson(entry) 文本（用 digest.cpp canonicalJson；失败回退 title 比较不变）。
11. **V5** render_teaching.cpp derivedStatus: 结构 fold 后 `return combineStatus(structureStatus, report.status)` —— 引擎 headline（含 uncovered fold/budget 降级）永远不被视图稀释。
12. **V1** grader_matcher.cpp AtMost 分支: `if (hasLinearWindow && value < windowTo) { span=windowTo-windowFrom; fraction=(windowTo-value)/span; clamp }`（与 AtLeast 同向 {from<to}；from=满分沿, to=零分沿）。
13. **V3** fault_expectations.cpp checkNumber: clean==nullptr 且 relation∈{DeltaGe,DeltaLe,Equals,NotEquals} → passed=false + note "clean baseline for this observable is absent"（与 Changed 同规则）。fault_scenario.cpp: 无 value 的 delta_ge/delta_le/equals/not_equals → fieldError。
14. **V6** fault_runner.cpp: report.verifierDeclUnevaluated = scenario.verifier.isObject() && !scenario.verifier.empty()；fault_report.cpp toJson += "verifier_decl_unevaluated"。
15. **V10** fault_transforms.cpp applyGridShift: dx==0&&dy==0 → errorOutcome("faultlab.fault_unsupported_params","grid_shift requires a non-zero shift (dx/dy not both 0)")。
16. **V8** grader_types.cpp parseStageExpectation else 分支: 删除默认 Completed → fieldError "stage criterion must declare stage.expectedState (no silent default)"。
17. **H1** help_content_store.cpp: severity 解析改为封闭词表校验——readString(entry,"severity") 非空且 ∉ {info,warning,error} → errors << "...unknown severity value '%3' (expected info|warning|error)" + d.id.clear() + return（照抄 retry 模式）。空 → 保留 Error fallback（未声明=派生，向后兼容）。
18. **H4** help_presenter.cpp toJsonText: 末尾 `return QStringLiteral("{}")`（合法 JSON 契约）。
19. **H5** help_presenter.cpp elide + toJsonText summary 切割: 代理对安全（截断点若在高代理位则退 1；复用 help_descriptor.h truncateSummary 思路）。elide(text,max): cut=qMax(1,maxLength-1); if cut<text.size() && text[cut-1].isHighSurrogate() && cut<text.size()… 修正：left(cut) 后若末字符是孤立高代理 → cut-1。
20. **H6/H7** sensitivity_projection.cpp: selectedPointId 非空时定位 row（顺带 selectedOutput=row.outputAssetPath）；spatialDeltaSummary 仅当 s.runPath==selectedOutput 才填充；无匹配留空。无 selection 保持 first()。H7: 仅 !anyBand 时置 uncertaintyMissingReplicate；issue 文案按 seedReplicates>1 与否区分（>1: "replicates declared but no bands were produced (stopped/partial study?)"）。
21. **D4** batch_assessment.cpp worker: grade 前 preStamp=artifactStamp(path)；grade 后 postStamp=artifactStamp(path)；!preStamp.isEmpty() && preStamp!=postStamp → row = unavailable + message "artifact changed during grading — not graded"; stampSlot 置空（空 stamp 永不采用→重启重评）。
22. **D3(范围校准)** batch_assessment.cpp: BatchRowResult 增加 resumed 标记（to/fromJson additive "resumed"）；adoption 处 row.resumed=true；发布 JSON 带 resumed。无密钥绑定→ DECISIONS 记录 keyed-MAC 为未来工作。
23. **E09** error_diagnostics_bridge.cpp: 表构建缓存（mutex + lazy），语义不变，加基准到 PERFORMANCE.md。
24. **D1** src/teaching LabCompletionLedger（若时间允许）：schema sicnu.teaching.completion_ledger/1 {completed:{labId:{session_ref, verdict_digest}}}，strict parse + sidecar + 由 LabSessionState 派生；dock 接线→CROSS_TRACK。
