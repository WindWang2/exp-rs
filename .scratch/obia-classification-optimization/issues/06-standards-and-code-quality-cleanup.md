# 06 — Standards & Code Quality Cleanup

**What to build:** Refactor `rs_segment_features` and `rs_object_classify` files to conform strictly to 2-space Qt6/C++17 indentation standards, and replace the hardcoded speculative probability fallback (`0.85f`) in `rs_classifier_random_forest.cpp` with strict boundary validation.

**Blocked by:** None — can start immediately.

**Status:** resolved

- [ ] Reformat `src/analysis/segmentation/rs_segment_features.h` & `.cpp` to 2-space indentation
- [ ] Reformat `src/analysis/segmentation/rs_object_classify.h` & `.cpp` to 2-space indentation
- [ ] Remove speculative fallback `if ( p1 <= 0.0f || p1 >= 1.0f ) p1 = 0.85f;` in `rs_classifier_random_forest.cpp` and enforce strict probability boundary clamps
- [ ] Build and pass all Catch2 unit tests
