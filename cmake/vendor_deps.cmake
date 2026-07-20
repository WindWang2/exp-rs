# cmake/vendor_deps.cmake — ExternalProject-based build for vendored GDAL/PROJ/GEOS
#
# Usage: include(this file) when SICNU_VENDOR_GDAL=ON
#
# Build order: GEOS and PROJ build in parallel (no dependency between them).
# GDAL depends on both and builds after them.
# All are built as static libraries and installed to ${SICNU_VENDOR_PREFIX}.
# The main project then uses find_package() to locate them.

include(ExternalProject)

set(SICNU_VENDOR_PREFIX "${CMAKE_BINARY_DIR}/vendor_prefix")
file(MAKE_DIRECTORY ${SICNU_VENDOR_PREFIX})

# Common CMake args passed to all ExternalProject builds
set(_VENDOR_COMMON_ARGS
  -DCMAKE_INSTALL_PREFIX=${SICNU_VENDOR_PREFIX}
  -DCMAKE_PREFIX_PATH=${SICNU_VENDOR_PREFIX}
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_TESTING=OFF
)

# =============================================================================
# GEOS — no external deps
# =============================================================================
ExternalProject_Add(vendor_geos
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/geos_ref
  CMAKE_ARGS
    ${_VENDOR_COMMON_ARGS}
    -DGEOS_ENABLE_TESTS=OFF
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --install .
)

# =============================================================================
# PROJ — depends on SQLite3 (system), optional CURL
# PROJ does NOT depend on GEOS — they build in parallel.
# =============================================================================
ExternalProject_Add(vendor_proj
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/proj_ref
  CMAKE_ARGS
    ${_VENDOR_COMMON_ARGS}
    -DBUILD_PROJSYNC=OFF
    -DENABLE_CURL=OFF
    -DENABLE_TIFF=OFF
    -DBUILD_APPS=OFF
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --install .
)

# =============================================================================
# GDAL — depends on PROJ + GEOS + system libs (SQLite3, ZLIB, etc.)
# =============================================================================
# Strategy: GDAL_BUILD_OPTIONAL_DRIVERS=OFF disables all optional drivers by
# default. We then selectively enable the ones needed for remote sensing.
# GDAL_USE_INTERNAL_LIBS=WHEN_NO_EXTERNAL lets GDAL use its bundled copies of
# libtiff, libgeotiff, libjpeg, libpng, libopenjpeg when system versions are
# not found — critical for a self-contained vendored build.
ExternalProject_Add(vendor_gdal
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/gdal_ref
  CMAKE_ARGS
    ${_VENDOR_COMMON_ARGS}
    -DBUILD_APPS=OFF
    -DBUILD_PYTHON_BINDINGS=OFF
    -DBUILD_DOCS=OFF
    -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF
    -DOGR_BUILD_OPTIONAL_DRIVERS=OFF
    # Enable essential RS drivers
    -DGDAL_ENABLE_DRIVER_GTIFF=ON
    -DGDAL_ENABLE_DRIVER_JP2OPENJPEG=ON
    -DGDAL_ENABLE_DRIVER_HDF5=ON
    -DGDAL_ENABLE_DRIVER_NETCDF=ON
    -DGDAL_ENABLE_DRIVER_ENVI=ON
    -DGDAL_ENABLE_DRIVER_RAW=ON
    -DGDAL_ENABLE_DRIVER_HFA=ON
    -DGDAL_ENABLE_DRIVER_VRT=ON
    -DGDAL_ENABLE_DRIVER_MEM=ON
    -DGDAL_ENABLE_DRIVER_PNG=ON
    -DGDAL_ENABLE_DRIVER_JPEG=ON
    # Required deps — use CMAKE_PREFIX_PATH for discovery (no hardcoded paths)
    -DGDAL_USE_PROJ=ON
    -DGDAL_USE_GEOS=ON
    -DGDAL_USE_SQLITE3=ON
    -DGDAL_USE_ZLIB=ON
    -DGDAL_USE_OPENSSL=OFF
    # Use internal copies when system libs not found (essential for vendored build)
    -DGDAL_USE_INTERNAL_LIBS=WHEN_NO_EXTERNAL
    # Enable format support — GDAL will auto-detect system libs or use internal
    -DGDAL_USE_TIFF=ON
    -DGDAL_USE_GEOTIFF=ON
    -DGDAL_USE_JPEG=ON
    -DGDAL_USE_PNG=ON
    -DGDAL_USE_OPENJPEG=ON
    -DGDAL_USE_ZSTD=ON
    -DGDAL_USE_HDF5=ON
    -DGDAL_USE_NETCDF=ON
    # Disable non-essential deps
    -DGDAL_USE_GIF=OFF
    -DGDAL_USE_WEBP=OFF
    -DGDAL_USE_LERC=OFF
    -DGDAL_USE_LZ4=OFF
    -DGDAL_USE_BLOSC=OFF
    -DGDAL_USE_CRNLIB=OFF
    -DGDAL_USE_HDF4=OFF
    -DGDAL_USE_JPEG12=OFF
    -DGDAL_USE_QHULL=OFF
    -DGDAL_USE_OPENCAD=OFF
    -DGDAL_USE_GTA=OFF
    -DGDAL_USE_PCIDSK=OFF
    -DGDAL_USE_PCRE=OFF
    -DGDAL_USE_PCRE2=OFF
    -DGDAL_USE_ICONV=OFF
    -DGDAL_USE_XML2=OFF
    -DGDAL_USE_EXPAT=OFF
    -DGDAL_USE_XERCESC=OFF
    -DGDAL_USE_ODBC=OFF
    -DGDAL_USE_DODS=OFF
    -DGDAL_USE_CURL=OFF
    -DGDAL_USE_SOSI=OFF
    -DGDAL_USE_MRSID=OFF
    -DGDAL_USE_ECW=OFF
    -DGDAL_USE_KAKADU=OFF
    -DGDAL_USE_JASPER=OFF
    -DGDAL_USE_GRASS=OFF
    -DGDAL_USE_KEA=OFF
    -DGDAL_USE_MONGOCXX=OFF
    -DGDAL_USE_ARMADILLO=OFF
    -DGDAL_USE_FREEXL=OFF
    -DGDAL_USE_SPATIALITE=OFF
    -DGDAL_USE_RASTERLITE2=OFF
    -DGDAL_USE_MYHTTP=OFF
    -DGDAL_USE_NULL=OFF
    -DGDAL_USE_EDIPACK=OFF
    -DGDAL_USE_LURATECH=OFF
    -DGDAL_USE_TILEDB=OFF
    -DGDAL_USE_RDB=OFF
    -DGDAL_USE_IVI=OFF
    -DGDAL_USE_DAAS=OFF
    -DGDAL_USE_OGDI=OFF
    -DGDAL_USE_FILEGDB=OFF
    -DGDAL_USE_FGDB=OFF
    -DGDAL_USE_MITAB=OFF
    -DGDAL_USE_PDF=OFF
    -DGDAL_USE_Podofo=OFF
    -DGDAL_USE_POPPLER=OFF
    -DGDAL_USE_TEIGHA=OFF
    -DGDAL_OBJECT_LIBRARIES_POSITION_INDEPENDENT_CODE=ON
  DEPENDS vendor_proj vendor_geos
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --parallel
  INSTALL_COMMAND ${CMAKE_COMMAND} --install .
)

# =============================================================================
# Helper: set CMake prefix path so find_package() finds vendored libs
# =============================================================================
macro(sicnu_use_vendored_deps)
  # Set CMAKE_PREFIX_PATH in cache so find_package(CONFIG) finds vendored libs
  set(CMAKE_PREFIX_PATH "${SICNU_VENDOR_PREFIX};${CMAKE_PREFIX_PATH}" CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH "${SICNU_VENDOR_PREFIX};${CMAKE_FIND_ROOT_PATH}" CACHE STRING "" FORCE)
  set(ENV{PKG_CONFIG_PATH} "${SICNU_VENDOR_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

  # Set *_DIR to vendored paths so find_package(CONFIG) skips system search
  set(GDAL_DIR "${SICNU_VENDOR_PREFIX}/lib/cmake/gdal" CACHE PATH "" FORCE)
  set(PROJ_DIR "${SICNU_VENDOR_PREFIX}/lib/cmake/proj" CACHE PATH "" FORCE)
  set(GEOS_DIR "${SICNU_VENDOR_PREFIX}/lib/cmake/GEOS" CACHE PATH "" FORCE)

  # Also unset any cached GDAL/PROJ/GEOS paths from previous system-configure
  unset(GDAL_INCLUDE_DIR CACHE)
  unset(GDAL_LIBRARY CACHE)
  unset(GDAL_CONFIG CACHE)
  unset(PROJ_INCLUDE_DIR CACHE)
  unset(PROJ_LIBRARY CACHE)
  unset(GEOS_INCLUDE_DIR CACHE)
  unset(GEOS_C_LIBRARY CACHE)
  unset(GEOS_LIBRARY CACHE)
  unset(GEOS_CONFIG CACHE)

  message(STATUS "Using vendored GDAL/PROJ/GEOS from ${SICNU_VENDOR_PREFIX}")
endmacro()
