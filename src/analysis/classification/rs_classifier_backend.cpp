// rs_classifier_backend.cpp — Phase 10A Task 10.8.
//
// The base class is pure virtual; this translation unit exists so the
// `qgis_analysis_export.h` export macros emit a vtable in the shared
// library when concrete backends derive from it across DSO boundaries.

#include "rs_classifier_backend.h"

// Intentionally empty — RsClassifierBackend has no non-virtual methods.
