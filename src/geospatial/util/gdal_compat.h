// gdal_compat.h — single first-party seam for GDAL version drift (task A,
// Verification Platform 8.0).
//
// Scope: FIRST-PARTY geospatial code only (src/geospatial/**, src/processing/**,
// src/data/**, …). The vendored QGIS tree (src/core/**, src/gui/**) keeps its
// own inline version ladders and is deliberately out of scope.
//
// Why this exists: the 7.0 merge wave broke on exactly three portability
// classes, two of them GDAL-version-shaped (#833/#834): VSI APIs moved
// between the GDAL versions this repo supports in practice (Ubuntu CI ships
// 3.8, Homebrew CI ships 3.13). Instead of re-deriving the breakpoint in
// every TU, first-party code asks this header for named capability macros:
//
//   SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR  (>= 3.12: Open()/OpenStatic()
//                                            return VSIVirtualHandleUniquePtr)
//   SICNU_GDAL_VSI_HANDLE_READ_BYTES        (>= 3.13: VSIVirtualHandle::Read/
//                                            Write are byte-oriented 2-arg)
//   SICNU_GDAL_VSI_HANDLE_ERR_API           (>= 3.10: ClearErr()/Error() on
//                                            VSIVirtualHandle)
//   SICNU_GDAL_VSI_REMOVE_HANDLER           (>= 3.9: VSIFileManager::
//                                            RemoveHandler)
//   SICNU_GDAL_INT64_DATATYPES              (>= 3.5: GDT_Int64/GDT_UInt64)
//
// The header is SELF-CONTAINED apart from <gdal_version.h> (which defines
// GDAL_VERSION_NUM / GDAL_COMPUTE_VERSION): including it must not force a
// cascade of other GDAL headers, so test TUs can probe the selected
// configuration cheaply.
#pragma once

#include <gdal_version.h> // GDAL_VERSION_NUM, GDAL_COMPUTE_VERSION

#define SICNU_GDAL_VERSION_NUM GDAL_VERSION_NUM

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 12, 0 )
#define SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR 1
#else
#define SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR 0
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 13, 0 )
#define SICNU_GDAL_VSI_HANDLE_READ_BYTES 1
#else
#define SICNU_GDAL_VSI_HANDLE_READ_BYTES 0
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 10, 0 )
#define SICNU_GDAL_VSI_HANDLE_ERR_API 1
#else
#define SICNU_GDAL_VSI_HANDLE_ERR_API 0
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 9, 0 )
#define SICNU_GDAL_VSI_REMOVE_HANDLER 1
#else
#define SICNU_GDAL_VSI_REMOVE_HANDLER 0
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 5, 0 )
#define SICNU_GDAL_INT64_DATATYPES 1
#else
#define SICNU_GDAL_INT64_DATATYPES 0
#endif
