# HANDOFF — R35-T04
## → T06 (StudyReport消费方)
- `StudyReport::fromJson` 现在对 curve point / envelope band / elapsed_ms 的数值字段做严格JSON number准入(WP18):
  - 合法 `mean=0.0` 原样保留; 缺失/null/字符串/bool → typed failure, code=`study.report_invalid_point`
  - `run_count` 只接受非负、整数、≤2^53 的 double
- UI含义: 恢复坏报告得到的是 typed Result failure(带diagnostics), 不再是"全部变0的可信点"。
  按规约, 未采纳新坏报告时保留带标签的last-good是合法回退; 不要求清空旧结果。
  不需要也不能在UI里把0猜测为missing。
## → T01
- 无新请求。`src/experiment/evaluation.*`未触碰(MetricRecord归T01)。
## → T03
- 无新请求。`durable_sidecar`/`portable`/`atomic_fs`按现状API调用。
