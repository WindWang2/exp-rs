# R25-D RISK REGISTER

| Risk | Sev | Mitigation |
|---|---|---|
| Shared-host build lock starvation (5 tracks × serialized builds) | high | build queued once; targeted builds only after baseline; ccache warm |
| github.com unreachable (TLS eof) — FetchContent | high | D7: exact-version sibling cache via FETCHCONTENT_SOURCE_DIR_*; no network needed for tests |
| Wall-clock flakiness in perf assertions | med | structural counts only (D6); measurements recorded, not gated |
| Full-shell dock test link cost / moc drift | med | rides sicnu_geo_rs_shell (#1295 precedent) instead of TU embeds |
| Ribbon/menu source-scan tests brittle to unrelated refactors | low | scans anchored to stable ids/anchors; narrow blocks |
| Sibling PR collisions on shell files | med | PR_COLLISION_MATRIX watch list + pre-push intersect |
| QgsLayoutDesignerDialog offscreen behavior in tests | med | designer test only asserts signal emission, never opens the dialog |
| offscreen render of CP11 gated cases hangs on unhealthy host | med | gate is opt-out (SICNU_CP11_RENDER=0); timeout via ctest; host probed first with one case |
