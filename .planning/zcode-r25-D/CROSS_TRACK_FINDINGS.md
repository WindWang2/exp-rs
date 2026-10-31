# R25-D CROSS-TRACK FINDINGS (read-only observations for other tracks)

1. **#1638 (src/gui vendored QGIS stubs)** — 3 sub-items (REMOVE_ITEM no-op guard, nullptr QgsSettingsEntry* stubs, QgsLayoutHtmlWidget stub) live in the vendored src/gui tree which D treats as zero-touch. Owner: platform/vendor track.
2. **#1565 (ladderLabelsZh clamp)** — src/app/teaching/ is E-track exclusive UI. Verified still LIVE at base SHA (clamp to literal 5). Owner: E.
3. **#1695 sub-items in E zone** — teaching_admin_dock i18n plural + GUI-thread discoverSubmissions + ~dtor join; lab::recordAnswer whitespace; publishBatchOutputsAtomic two-file generation gap. Verified against base: still present. Owner: E.
4. **exp_gui::SpectralProfileWidget** (duplicate class name, no production caller, test-covered) — left in place deliberately; pool discipline already fixed. If a future track wants removal, tests/test_spectral_profile_widget.cpp:420+ must move first.
5. **AsyncGdalRunner** — production-dead (comments only), test-covered, lifecycle fixed (#1695 comments in-tree). Same removal note.
