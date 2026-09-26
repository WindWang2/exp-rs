# MODULE_FAILURE_PATHS — 22/22 总表（WP-G 收口件）

清点单位 = teaching_admin 15 头 + teaching 7 头。每行：模块 | 失败路径 | 测试名（ctest 可数）| 提交号。
"r4" 前缀 = 本轨道新增断言；提交号在提交后回填。

| # | 模块 | 失败路径（注入方式） | 测试名 | 提交 |
|---|---|---|---|---|
| 1 | teaching_admin/admin_types.h | ValidationResult severity 归一/ok 翻转（经各套件的 typed 断言间接覆盖 + B 矩阵 issue 结构） | boundary/authoring 套件各断言 | TBD |
| 2 | teaching_admin/admin_errors.h | 空 detail → 裸 code；非空 → code: detail（凭据 refusal 信封错误码合同） | credential paths r4 refusal code | TBD |
| 3 | teaching_admin/json_util.h | 不可读文件 → 空 digest；unsafe path 四形态（绝对/盘符/../反斜杠） | unsafe path helper（既有）+ canonical 边界套件 | TBD |
| 4 | teaching_admin/data_pack_manager.h | 边界矩阵 B01-B22 全族（digest/byte/missing/traversal/escape/directory/long-path/name-identity/manifest drift） | test_lab_pack_boundaries_r4 全套 | TBD |
| 5 | teaching_admin/batch_assessment.h | 空批/全败/throw 隔离/重复发现/损坏与外来 checkpoint/取消不入 checkpoint/续传 | test_teaching_batch_r4 全套 | TBD |
| 6 | teaching_admin/class_summary.h | 空报告 rate=0 非NaN；全败桶直方诚实 | batch r4 空批+全败用例 | TBD |
| 7 | teaching_admin/feedback_pack.h | 跨学生泄漏 typed | （既有 core 套件覆盖，r4 复核行） | TBD |
| 8 | teaching_admin/rubric_builder.h | 重复 assertion id/缺 metric/权重漂移 | authoring failures r4 | TBD |
| 9 | teaching_admin/release_preflight.h | 门未过 ok=false + canonical digest 稳定（防静默重掷） | authoring failures r4 preflight | TBD |
| 10 | teaching_admin/operator_catalog.h | id-less sidecar typed issue、有效条目仍装载（非全有全无） | authoring failures r4 catalog | TBD |
| 11 | teaching_admin/labspec_authoring.h | 逃逸形 ref → unsafe_ref（区别于 dangling_ref） | authoring failures r4 labspec | TBD |
| 12 | teaching_admin/grader_cli_adapter.h | 权威 exit/verdict 交叉核对失败 → typed unavailable（既有覆盖，r4 引用 gradeFromTranscript 合同） | （既有 core 套件） | TBD |
| 13 | teaching_admin/script_adapters.h | 脚本缺失/超时/崩溃 typed（既有 offline adapter 套件覆盖） | （既有 core 套件） | TBD |
| 14 | teaching_admin/curriculum_editor.h | duplicate module id/index + unknown key | authoring failures r4 curriculum | TBD |
| 15 | teaching_admin/student_projection.h | 篡改 student view 的 param 泄漏按坐标点名 + teacher_only_field | cockpit authority r4 | TBD |
| 16 | teaching/lab_status.h | 未知 wire → false + Unknown；label/icon token 全态非空 | cockpit authority r4 status | TBD |
| 17 | teaching/lab_session_state.h | 合法 JSON 但未知键/负 step_index/错类型 → 整文档拒绝 | parity r4 session | TBD |
| 18 | teaching/lab_step_timeline.h | 不可识别文档 → sourceKind unknown + fail-closed | parity r4 timeline | TBD |
| 19 | teaching/lab_feedback_projection.h | 错 schema restore 拒绝 + 恢复后 indeterminate 永不计 pass | parity r4 feedback | TBD |
| 20 | teaching/course_home_view_model.h | 缺失/异 schema/缺 modules 三形态 ok=false | cockpit authority r4 course | TBD |
| 21 | teaching/lab_readiness.h | 空 labId + 全权威切片缺失 → UNKNOWN、无 ok=true 项 | cockpit authority r4 readiness | TBD |
| 22 | teaching/autonomy_effective_display.h | 缺失/异 schema status doc → ok=false、不挖高层级 | cockpit authority r4 autonomy | TBD |

parity 残余四用例（WP-C）：restore 字节稳定 / 数组序不变性 / 部分失败转录完整性 / 并发==串行 → test_teaching_parity_r4。
凭据三路径（WP-D）：labAsk / harness:autonomy_status / labReference × 四 token 态 → test_teaching_credential_paths_r4。
批量失败语义（WP-B）：断点续传语义 7 用例 → test_teaching_batch_r4（DECISIONS.md 已文档化二选一）。
