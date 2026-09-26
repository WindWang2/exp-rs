# MODULE_FAILURE_PATHS — 22/22 总表（WP-G 收口件）

清点单位 = teaching_admin 15 头 + teaching 7 头。每行：模块 | 失败路径（注入方式）| 测试名（ctest 可数）| 提交。
ctest 名 = ctest 二进制前缀 + TEST_CASE 名（teaching_admin/自定义目标带 `<target>::` 前缀；`sicnu_add_teaching_test` 目标为裸 TEST_CASE 名）。

| # | 模块 | 失败路径（注入方式） | 测试名（ctest） | 提交 |
|---|---|---|---|---|
| 1 | teaching_admin/admin_types.h | ValidationResult/issue 序列化契约（各套件 typed 断言的公共底座：severity 归一、ok 翻转） | 经 test_lab_pack_boundaries_r4::boundary B06/B15/B17 与 authoring 套件各断言行使 | 33cf816d5e |
| 2 | teaching_admin/admin_errors.h | 拒绝信封错误码合同（不新增错误码轴：TEACHING_REFUSAL 精确复现） | test_teaching_credential_paths_r4::credential paths r4: unset/empty/wrong token … | 0e2fcdd452 |
| 3 | teaching_admin/json_util.h | unsafe path 四形态拒绝（绝对/盘符/../反斜杠）；canonical 函数不可读文件返回空/-1（经 CHUNK-3 空文件与 inventory 未读文件路径行使） | test_lab_pack_boundaries_r4::boundary CHUNK-1/2/3（canonical 直测）+ core "unsafe relative path helper" | 33cf816d5e |
| 4 | teaching_admin/data_pack_manager.h | B01-B22 边界矩阵全族：CRLF/CR-only/UTF-16/BOM/NFC-NFD/大小写、缺失/目录冒名/零字节/超长路径/symlink 逃逸/路径逃逸、digest/byte 漂移（tier 强度）、pack 级 declared 漂移、空目录 | test_lab_pack_boundaries_r4::boundary B01…B22（18 用例 92 断言） | 33cf816d5e |
| 5 | teaching_admin/batch_assessment.h | 空批 typed 空跑；全败不造假 pass；throw 隔离；重复发现两行确定序；损坏/外来 checkpoint 降级 fresh start；取消行永不入 checkpoint、重启续传 resumed==2；publish 拒不可写前缀 | test_teaching_batch_r4::batch r4: empty/all-fail/throwing/duplicate/corrupt+foreign/cancellation+restart/atomic publish（7 用例） | ece627ce37 |
| 6 | teaching_admin/class_summary.h | 空报告 rate=0.0 非 NaN；全败直方仅单桶诚实 | test_teaching_batch_r4::batch r4: empty… / all-fail… | ece627ce37 |
| 7 | teaching_admin/feedback_pack.h | 跨学生泄漏 typed（既有 core 套件 mutation 覆盖，本轨复核） | test_teaching_admin_core::leak oracle…/student projection masks answers | （既有） |
| 8 | teaching_admin/rubric_builder.h | 重复 assertion id；metric-kind 缺 metricKey | test_teaching_authoring_failures_r4::authoring r4: duplicate ids… / process rubric requires a metric… | 9967de537e |
| 9 | teaching_admin/release_preflight.h | 门未过 ok=false + canonical digest 重算稳定（防静默重掷） | test_teaching_authoring_failures_r4::authoring r4: preflight report keeps ok=false… | 9967de537e |
| 10 | teaching_admin/operator_catalog.h | id-less sidecar typed issue、有效条目仍装载（非全有全无） | test_teaching_authoring_failures_r4::authoring r4: operator catalog loads valid sidecars… | 9967de537e |
| 11 | teaching_admin/labspec_authoring.h | 逃逸形 ref → unsafe_ref（区别于 dangling_ref） | test_teaching_authoring_failures_r4::authoring r4: traversal-shaped authoring refs… | 9967de537e |
| 12 | teaching_admin/grader_cli_adapter.h | exit/verdict 交叉核对失败 → typed unavailable；CLI 缺失 → typed unavailable（既有覆盖） | test_teaching_admin_core::gradeViaCli…/batch with missing grader… | （既有） |
| 13 | teaching_admin/script_adapters.h | 脚本缺失/超时/崩溃 typed | test_teaching_admin_core::offline bundle verify adapter typed exit… | （既有） |
| 14 | teaching_admin/curriculum_editor.h | duplicate module id/index + unknown key typed | test_teaching_authoring_failures_r4::authoring r4: duplicate ids in rules and curriculum… | 9967de537e |
| 15 | teaching_admin/student_projection.h | 篡改 student view 的 param 泄漏按坐标点名（steps[0].params.threshold）+ teacher_only_field 按键点名 | test_teaching_cockpit_authority_r4::cockpit r4: tampered student view… | 0e2fcdd452 |
| 16 | teaching/lab_status.h | 未知 wire 拒绝且不改写调用方值；合法 wire 往返；label/icon token 全态非空 | test_teaching_cockpit_authority_r4::cockpit r4: status wire vocabulary… | 0e2fcdd452 |
| 17 | teaching/lab_session_state.h | 合法 JSON 但未知键/负 step_index/错类型 → 整文档拒绝（未知键 issue 首条含"未知字段"） | test_teaching_parity_r4::session state r4: well-formed but contract-violating… | 18647449c8 |
| 18 | teaching/lab_step_timeline.h | 不可识别文档 → sourceKind unknown + fail-closed；LabSpec 无 steps → typed issue | test_teaching_parity_r4::step timeline r4: unrecognizable lab document… | 18647449c8 |
| 19 | teaching/lab_feedback_projection.h | 错 schema restore 直接拒绝（indeterminate 永不计 pass）；部分失败转录全行保真；乱序不变性；恢复字节稳定；并发==串行 | test_teaching_parity_r4::feedback projection r4: wrong-schema restore… + parity r4: restored/order/partial/concurrent（8 用例） | 18647449c8 |
| 20 | teaching/course_home_view_model.h | 缺失/异 schema/缺 modules 三形态 ok=false（issue 文本精确） | test_teaching_cockpit_authority_r4::cockpit r4: course home refuses… | 0e2fcdd452 |
| 21 | teaching/lab_readiness.h | 空 labId typed；全权威切片缺失 → 无 ok=true 项、level≠Ready | test_teaching_cockpit_authority_r4::cockpit r4: readiness degrades to UNKNOWN… | 0e2fcdd452 |
| 22 | teaching/autonomy_effective_display.h | 缺失/异 schema status doc → ok=false、不挖高层级 | test_teaching_cockpit_authority_r4::cockpit r4: autonomy display refuses… | 0e2fcdd452 |

配套套件：凭据三路径（WP-D）test_teaching_credential_paths_r4（5 用例）；batch 语义（WP-B）test_teaching_batch_r4（7 用例）；边界矩阵（WP-A）test_lab_pack_boundaries_r4（18 用例）；parity（WP-C）test_teaching_parity_r4（8 用例）；authoring（WP-F）test_teaching_authoring_failures_r4（7 用例）；cockpit 权威（WP-E）test_teaching_cockpit_authority_r4（7 用例）。
