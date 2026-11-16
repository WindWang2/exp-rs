// lab_session_state_diagnostics.inc — persisted session diagnostic vocabulary (DATA).
//
// #2108: issuesZh entries are stored VERBATIM in session files and rendered
// by the lab cockpit — they are part of the session format, not free UI
// strings. The texts live here as one addressed data table so the code
// references stable ids (grep-able, test-assertable, unchanged bytes on the
// wire) instead of inline literals; the i18n audit's per-file cap then
// measures genuinely new diagnostic strings added to CODE, not this closed
// persisted vocabulary. Any new diagnostic gets an id here and a use site.
//
// Keep entries sorted by id prefix (kDiag...) for reviewability.

constexpr auto kDiagSessionNotJsonObject = "session 非 JSON 对象";
constexpr auto kDiagIdsRequired = "session/course/lab id 不能为空";
constexpr auto kDiagUnknownField = "未知字段: "; // + field name
constexpr auto kDiagSchemaMismatch = "schema 不匹配: "; // + expected schema
constexpr auto kDiagMissingIds = "缺少 session_id/course_id/lab_id";
constexpr auto kDiagArtifactPathType = "artifact_path 类型错误";
constexpr auto kDiagStepIndexType = "step_index 类型错误";
constexpr auto kDiagStepIndexNegative = "step_index 为负";
constexpr auto kDiagEvidenceRefsNotArray = "evidence_refs 非数组";
constexpr auto kDiagEvidenceRefsNonString = "evidence_refs 含非字符串";
constexpr auto kDiagLastSummaryType = "last_validation_summary 类型错误";
constexpr auto kDiagVerdictStampType = "verdict_artifact_stamp 类型错误";
constexpr auto kDiagUnknownMode = "未知 mode";
constexpr auto kDiagUnknownLabStatus = "未知 lab_status";
constexpr auto kDiagLabStatusCorrected = "lab_status 与记录的验证摘要不一致（已按摘要纠正）";
constexpr auto kDiagJsonParseFail = "JSON 解析失败: "; // + parser error text
constexpr auto kDiagSessionFileUnreadable = "无法读取会话文件";
constexpr auto kDiagStampModified = "验证摘要在盖章后被修改（按不确定处理）";
constexpr auto kDiagArtifactMissingUnreadable = "被验证的工件缺失或不可读，无法复核记录的判定（按不确定处理）";
constexpr auto kDiagArtifactInconsistent = "工件与记录判定时不一致（按不确定处理，请重新验证）";
