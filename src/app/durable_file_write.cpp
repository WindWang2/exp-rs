// src/app/durable_file_write.cpp — shell-side TU for the shared durable-write
// authority (R14-D WP-B, issue #1487 remainder).
//
// The implementation lives HEADER-ONLY INLINE in durable_file_write.h so that
// every consumer library (notably qgis_app_georef, compiled without a link on
// sicnu_geo_rs_shell and linked WITHOUT it by ~28 test targets) gets the
// definition regardless of static-archive order on the link line. This TU
// exists to (a) guarantee the header is self-contained under the shell's
// compile flags and (b) anchor the file pair in the sicnu_geo_rs_shell target
// sources for discoverability.
#include "durable_file_write.h"
