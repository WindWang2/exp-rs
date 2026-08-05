# 07 — Active Learning Candidate Table Population

**What to build:** Wire `segmentUncertainties` computed during `RsObjectClassify::classify` into `RsObiaMainWindow`'s `mUncertaintyTable` upon task completion, allowing active learning candidate inspection and canvas centering via double-click.

**Blocked by:** 06 — Standards & Code Quality Cleanup

**Status:** resolved

- [ ] Update `RsObiaTask` and `RsObiaMainWindow` to retain and pass `RsObjectClassifyResult::segmentUncertainties`
- [ ] Populate `mUncertaintyTable` sorted descending by Shannon entropy $H$
- [ ] Verify double-clicking candidate rows centers the map canvas and highlights the segment
- [ ] Build and pass all Catch2 unit tests
