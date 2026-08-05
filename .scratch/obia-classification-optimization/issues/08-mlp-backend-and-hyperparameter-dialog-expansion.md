# 08 — Neural Network (MLP) Backend & Hyperparameter Dialog Expansion

**What to build:** Implement `RsMlpBackend` wrapping `cv::ml::ANN_MLP`, register it in `RsClassifierBackendFactory`, and add `minSampleCount` hyperparameter configuration to `RsObiaMainWindow`'s Random Forest tuning dialog.

**Blocked by:** 06 — Standards & Code Quality Cleanup

**Status:** resolved

- [ ] Create `RsMlpBackend` wrapping OpenCV `ANN_MLP` with probability prediction support
- [ ] Register `"MLP"` in `RsClassifierBackendFactory`
- [ ] Add `mRfMinSampleCount` input to `showClassifierConfigDialog` in `RsObiaMainWindow`
- [ ] Write Catch2 unit test for MLP classifier backend
- [ ] Build and pass all Catch2 unit tests
