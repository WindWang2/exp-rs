cmake_minimum_required(VERSION 3.20)
project(sicnu_geo VERSION 1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CheckFunctionExists)
include(CreateQgsVersion)
include(GenerateExportHeader)

# Global cross-platform definitions
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    add_compile_options(/utf-8 /bigobj /W4 /wd4251 /wd4275 /wd4100 /wd4127 /wd4244 /wd4267)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

set(COMPLETE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
option(ENABLE_LOCAL_BUILD_SHORTCUTS "Skip git version detection for faster local builds" ON)
CREATE_QGSVERSION()

set(QT_VERSION_BASE "Qt6")
set(QT_VERSION_BASE_LOWER "qt6")

# =============================================================================
# Vendored GDAL/PROJ/GEOS (ExternalProject-based, no system install needed)
# Default to OFF for now (requires first-time build of vendored deps)
# Set -DSICNU_VENDOR_GDAL=ON to build and use vendored GDAL/PROJ/GEOS
# =============================================================================
option(SICNU_VENDOR_GDAL "Build and use vendored GDAL/PROJ/GEOS from vendor/" OFF)
if(SICNU_VENDOR_GDAL)
    include(vendor_deps)
    sicnu_use_vendored_deps()
    message(STATUS "SICNU_VENDOR_GDAL=ON — building GDAL/PROJ/GEOS from vendor/")
endif()

# All Qt components required anywhere in the build are declared HERE, at the
# top level: importing them in subdirectory scopes (e.g. src/gui) is unreliable
# on some Qt 6.4 distro configs ("UiTools target mentioned as a dependency for
# qgis_gui, but not declared" — the umbrella config short-circuits on a cached
# Qt6_FOUND and the new component targets are never created).
# The minimum is 6.8: the code uses QtTypes (6.5+) and other post-6.4 APIs; CI
# provides Qt 6.8 via aqtinstall on every platform (Ubuntu's distro Qt 6.4.2 is
# too old for this codebase).
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Svg Xml Sql Multimedia
                                       MultimediaWidgets Qml QuickWidgets SvgWidgets UiTools
                                       Core5Compat Test)
qt_standard_project_setup(REQUIRES 6.8)

# When vendoring, directly include the config files to bypass Find module cache
if(SICNU_VENDOR_GDAL)
    include("${SICNU_VENDOR_PREFIX}/lib/cmake/gdal/GDALConfig.cmake")
    include("${SICNU_VENDOR_PREFIX}/lib/cmake/proj/proj-config.cmake")
    include("${SICNU_VENDOR_PREFIX}/lib/cmake/GEOS/geos-config.cmake")
    set(GDAL_FOUND TRUE)
    set(PROJ_FOUND TRUE)
    set(GEOS_FOUND TRUE)
    message(STATUS "Found vendored GDAL ${GDAL_VERSION}")
    message(STATUS "Found vendored PROJ")
    message(STATUS "Found vendored GEOS")
else()
    find_package(GDAL REQUIRED)
    find_package(PROJ REQUIRED)
    find_package(GEOS REQUIRED)
endif()
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(EXPAT REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibZip REQUIRED)
# ZSTD is only required for point cloud support (WITH_EPT)
if(WITH_EPT)
    find_package(ZSTD REQUIRED)
endif()
find_package(Protobuf REQUIRED)
find_package(Qt6Keychain QUIET)
# PCRE2 and CURL are transitive dependencies (glib, Qt6, GDAL)
find_library(PCRE2_16 pcre2-16 REQUIRED)
find_library(PCRE2_8 pcre2-8 REQUIRED)
find_package(CURL REQUIRED)
# Python: always required for build-time scripts; Development only when embedding
find_package(Python REQUIRED COMPONENTS Interpreter)
if(SICNU_EMBED_PYTHON)
    find_package(Python REQUIRED COMPONENTS Interpreter Development)
    message(STATUS "SICNU_EMBED_PYTHON=ON — linking embedded Python console")
else()
    message(STATUS "SICNU_EMBED_PYTHON=OFF — Python console disabled")
endif()

# OpenCV — OPTIONAL (SIFT georeferencing + supervised classification).
# Supports OpenCV 4.x (opencv4 cmake dir) and 5.x (opencv5 cmake dir).
if(NOT OpenCV_DIR OR OpenCV_DIR STREQUAL "OpenCV_DIR-NOTFOUND")
    if(EXISTS "/usr/lib/cmake/opencv5/OpenCVConfig.cmake")
        set(OpenCV_DIR "/usr/lib/cmake/opencv5" CACHE PATH "OpenCV config directory" FORCE)
    elseif(EXISTS "/usr/lib/cmake/opencv4/OpenCVConfig.cmake")
        set(OpenCV_DIR "/usr/lib/cmake/opencv4" CACHE PATH "OpenCV config directory" FORCE)
    endif()
endif()

# OpenCV 5 renamed cmake components: features2d→features, calib3d→calib.
# Version pin 4.5 is incompatible with OpenCV 5's package version check.
find_package(OpenCV QUIET COMPONENTS core imgproc ml features calib dnn)
if(NOT OpenCV_FOUND)
    find_package(OpenCV QUIET COMPONENTS core features2d imgproc calib3d ml dnn)
endif()
if(OpenCV_FOUND)
    message(STATUS "OpenCV ${OpenCV_VERSION} found — SIFT enabled")
    set(SICNU_HAS_OPENCV TRUE)
else()
    message(STATUS "OpenCV not found — SIFT will be disabled (set OpenCV_DIR to enable)")
    set(SICNU_HAS_OPENCV FALSE)
endif()

# Include FetchContent for dependency management (e.g. Catch2)
include(FetchContent)

# QCA — include directly from known install path or FindQCA result
include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindQCA.cmake OPTIONAL)
if(QCA_FOUND)
  include_directories(SYSTEM ${QCA_INCLUDE_DIR} ${QCA_INCLUDE_DIR}/QtCrypto)
elseif(UNIX)
  include_directories(SYSTEM /usr/include/qt6/Qca-qt6/QtCrypto)
endif()

# Internal nlohmann_json: create interface target for vendored header
option(WITH_INTERNAL_NLOHMANN_JSON "" ON)
if(WITH_INTERNAL_NLOHMANN_JSON)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/external/nlohmann)
  add_library(nlohmann_json INTERFACE)
  add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
else()
  find_package(nlohmann_json REQUIRED)
endif()

# Internal spatialindex
option(WITH_INTERNAL_SPATIALINDEX "" ON)
if(WITH_INTERNAL_SPATIALINDEX)
  set(SPATIALINDEX_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/spatialindex/include)
  include_directories(SYSTEM ${SPATIALINDEX_INCLUDE_DIR})
endif()

# --- qgsconfig.h: all optional features OFF for P0 ---
# SICNU_GEO_RS: P3 — enable HAVE_GUI so core exposes authMethodEditWidget etc.
set(HAVE_GUI TRUE)
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(RELEASE_NAME "SICNU GEO RS")
set(QGIS_VERSION_INT 10000)
set(QGIS_PLUGIN_SUBDIR "plugins")
set(QGIS_DATA_SUBDIR "share")
set(QGIS_LIBEXEC_SUBDIR "libexec")
set(QGIS_LIB_SUBDIR "lib")
set(QGIS_QML_SUBDIR "qml")
set(QGIS_SERVER_MODULE_SUBDIR "server")
set(QGIS_INCLUDE_DIR "include/qgis")
set(QGIS_BIN_DIR "bin")
set(QGIS_LIB_DIR "lib")
set(QGISPOSTFIX "")
set(LIBRARY_TYPE SHARED)

# --- P0 options: disable optional features, use vendored deps ---
option(ENABLE_UNITY_BUILDS "" OFF)
option(WITH_INTERNAL_MESHOPTIMIZER "" ON)
option(WITH_VECTORTILE "" ON)
option(WITH_SFCGAL "" OFF)
option(USE_PRECOMPILED_HEADERS "" ON)
option(WITH_PDAL "" OFF)
option(WITH_COPC "" ON)
option(WITH_EPT "" ON)
option(WITH_POSTGRESQL "" OFF)
option(WITH_SPATIALITE "" OFF)
option(WITH_ORACLE "" OFF)
option(WITH_HANA "" OFF)
option(WITH_MESH_EXTRAS "" OFF)
option(WITH_OPENCL "" OFF)
option(WITH_3D "" OFF)
option(WITH_QTWEBENGINE "" OFF)
option(WITH_GRASS "" OFF)
option(WITH_SERVER "" OFF)
option(WITH_GUI "" ON)
option(WITH_ANALYSIS "" OFF)
option(WITH_BINDINGS "" OFF)
option(WITH_AUTH "" ON)
if(WITH_AUTH)
  set(HAVE_AUTH TRUE)
endif()
if(WITH_EPT)
  set(HAVE_EPT TRUE)
endif()
if(WITH_COPC)
  set(HAVE_COPC TRUE)
endif()
option(ENABLE_TESTS "" ON)
option(SICNU_EMBED_PYTHON "Embed optional Python console in desktop app" OFF)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_SANITIZERS)
    # Compile all source files with sanitizer instrumentation
    if(MSVC)
        # MSVC: only AddressSanitizer is supported
        add_compile_options(/fsanitize=address)
    else()
        # GCC/Clang: AddressSanitizer + UndefinedBehaviorSanitizer
        # Use -g1 (minimal debug info) to avoid GCC 16 ICE in dwarf2out.cc
        # -fno-sanitize=vptr: the vptr (typeinfo) check emits an undefined
        # reference to the typeinfo of every polymorphic static type at each
        # virtual call site. Our layering builds sicnu_data/sicnu_jobs as STATIC
        # libraries absorbed into several SHARED libraries (sicnu_processing,
        # sicnu_operators, ...), so a lib that USES RSOperator (defined in
        # libsicnu_operators) without linking it — e.g. atomic_algorithm_adapter
        # and job_engine — gets an unresolvable _ZTI reference and every test
        # binary aborts at load. vptr checks cannot function across this
        # boundary; heap/UB coverage (ASan + remaining UBSan) is unchanged.
        add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -g1)
        # Link sanitizer runtime into executables only (not shared libraries)
        # to avoid ld.so conflicts with non-instrumented system libraries (Qt6, GDAL, etc.)
        # --allow-shlib-undefined: our qgis_gui stubs have unresolved typeinfo for
        # header-only QScintilla stubs; the symbols are never called at runtime.
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined -Wl,--allow-shlib-undefined")
        # Also allow undefined symbols in shared libraries (same QScintilla stub reason)
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--allow-shlib-undefined")
    endif()
    # Disable LeakSanitizer: QGIS uses intentional singleton patterns (QgsExpression
    # function registry, QgsNumericFormatRegistry, Qt/GDAL driver allocations) that
    # live for the process lifetime and are reported as leaks. These are not bugs.
    set(ENV{LSAN_OPTIONS} "detect_leaks=0")
    # Also write CTestCustom.cmake so ctest sets LSAN_OPTIONS for test discovery
    file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
      "set(ENV{LSAN_OPTIONS} \"detect_leaks=0\")\n")
endif()

# Release optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    option(ENABLE_LTO "Enable Link-Time Optimization for Release builds" ON)
    if(ENABLE_LTO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

option(WITH_DRACO "" OFF)
option(WITH_INTERNAL_POLY2TRI "" ON)
option(WITH_INTERNAL_LAZPERF "" ON)
if(WITH_INTERNAL_LAZPERF)
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/external/lazperf)
endif()

# ─── SICNU_BUILD_OTB: vendored ITK + OTB algorithm libraries ───────────
option(SICNU_BUILD_OTB "Build vendored OTB + ITK algorithm libraries" OFF)
if(SICNU_BUILD_OTB)
    message(STATUS "SICNU_BUILD_OTB=ON — configuring vendored ITK + OTB")
    # Force -fPIC for all OTB/ITK libraries (needed for shared module applications)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)

    # ── ITK: only algorithm core modules ──────────────────────────────────
    set(ITK_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)

    # ── ITK: only algorithm core modules ──────────────────────────────────
    set(ITK_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        # Core
        ITKCommon
        ITKFiniteDifference
        ITKGPUCommon
        ITKGPUFiniteDifference
        ITKImageAdaptors
        ITKImageFunction
        ITKMesh
        ITKQuadEdgeMesh
        ITKSpatialObjects
        ITKTransform
        # Filtering
        ITKAnisotropicSmoothing
        ITKAntiAlias
        ITKBiasCorrection
        ITKBinaryMathematicalMorphology
        ITKColormap
        ITKConvolution
        ITKCurvatureFlow
        ITKDeconvolution
        ITKDenoising
        ITKDisplacementField
        ITKDistanceMap
        ITKFastMarching
        ITKFFT
        ITKGPUAnisotropicSmoothing
        ITKGPUImageFilterBase
        ITKGPURegistrationCommon
        ITKGPUTotalVariation
        ITKGPUThresholding
        ITKGPUSmoothing
        ITKGPUPDEDeformableRegistration
        ITKImageCompare
        ITKImageCompose
        ITKImageFeature
        ITKImageFilterBase
        ITKImageFusion
        ITKImageGradient
        ITKImageGrid
        ITKImageIntensity
        ITKImageLabel
        ITKImageSources
        ITKImageStatistics
        ITKLabelMap
        ITKMathematicalMorphology
        ITKPath
        ITKQuadEdgeMeshFiltering
        ITKSmoothing
        ITKSpatialFunction
        ITKThresholding
        # ThirdParty / Numerics
        ITKEigen
        ITKNarrowBand
        ITKOptimizers
        ITKOptimizersv4
        ITKPolynomials
        ITKStatistics
        # Registration
        ITKRegistrationCommon
        ITKMetricsv4
        ITKPDEDeformableRegistration
        ITKRegistrationMethodsv4
        # Segmentation
        ITKClassifiers
        ITKConnectedComponents
        ITKDeformableMesh
        ITKKLMRegionGrowing
        ITKLabelVoting
        ITKLevelSets
        ITKLevelSetsv4
        ITKMarkovRandomFieldsClassifiers
        ITKRegionGrowing
        ITKSignedDistanceFunction
        ITKVoronoi
        ITKWatersheds
        # IO
        ITKIOImageBase
        ITKIOTIFF
        ITKIOGDAL
        ITKIOGEOM
        ITKIOPNG
        ITKIOJPEG
        ITKIOMeta
        ITKIORAW
        ITKNrrdIO
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    set(ITK_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(ITK_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/itk_ref")
        add_subdirectory(itk_ref)
    else()
        message(WARNING "SICNU_BUILD_OTB=ON but itk_ref/ is missing — skipping ITK (vendored OTB build will not work without it)")
    endif()

    # Tell OTB where to find the built ITK
    set(ITK_DIR "${CMAKE_BINARY_DIR}/itk_ref" CACHE PATH "ITK build directory" FORCE)

    # ── OTB: only core modules (Applications/Wrappers disabled — use CLI wrappers) ──
    set(OTB_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        OTBCommon
        OTBImageBase
        OTBImageManipulation
        OTBObjectList
        OTBImageIO
        OTBStreaming
        OTBStatistics
        OTBLabelling
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    # Disable all Application/Wrapper modules (use CLI wrappers instead)
    set(Module_OTBApplicationEngine OFF CACHE BOOL "" FORCE)
    set(Module_OTBAppCore OFF CACHE BOOL "" FORCE)
    set(Module_OTBCommandLine OFF CACHE BOOL "" FORCE)
    # Disable all Application submodules and TestKernel
    file(GLOB OTB_APP_MODULES "${CMAKE_SOURCE_DIR}/otb_ref/Modules/*/Applications/otb-module.cmake")
    foreach(app_module ${OTB_APP_MODULES})
        get_filename_component(app_dir ${app_module} DIRECTORY)
        get_filename_component(app_name ${app_dir} NAME)
        set(Module_${app_name} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(Module_OTBTestKernel OFF CACHE BOOL "" FORCE)

    # Tell OTB where to find GDAL
    find_path(GDAL_INCLUDE_DIR gdal.h HINTS /usr/include/gdal)
    find_library(GDAL_LIBRARY NAMES gdal)
    set(GDAL_INCLUDE_DIRS "${GDAL_INCLUDE_DIR}" CACHE PATH "" FORCE)
    set(GDAL_LIBRARIES "${GDAL_LIBRARY}" CACHE PATH "" FORCE)
    set(GDAL_FOUND TRUE CACHE BOOL "" FORCE)

    # OTB requires Boost (filesystem, serialization).
    # Use vendored Boost headers + system Boost libraries
    set(SICNU_BOOST_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/refs/boost" CACHE PATH "Vendored Boost include directory")

    # Find system Boost libraries
    find_library(Boost_FILESYSTEM_LIBRARY NAMES boost_filesystem)
    find_library(Boost_SERIALIZATION_LIBRARY NAMES boost_serialization)

    if(Boost_FILESYSTEM_LIBRARY AND Boost_SERIALIZATION_LIBRARY)
        message(STATUS "Found system Boost libraries: ${Boost_FILESYSTEM_LIBRARY}")
        # Create imported targets
        add_library(Boost::filesystem STATIC IMPORTED)
        set_target_properties(Boost::filesystem PROPERTIES
            IMPORTED_LOCATION "${Boost_FILESYSTEM_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}"
        )
        add_library(Boost::serialization STATIC IMPORTED)
        set_target_properties(Boost::serialization PROPERTIES
            IMPORTED_LOCATION "${Boost_SERIALIZATION_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SICNU_BOOST_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "Boost libraries not found. Please install libboost-filesystem-dev and libboost-serialization-dev")
    endif()

    # Pre-set all Boost variables so OTB's patched Boost init skips find_package
    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "${SICNU_BOOST_INCLUDE_DIR}")
    set(Boost_LIBRARIES "Boost::filesystem;Boost::serialization")
    set(Boost_INCLUDE_DIR "${SICNU_BOOST_INCLUDE_DIR}")
    set(Boost_FILESYSTEM_FOUND TRUE)
    set(Boost_SERIALIZATION_FOUND TRUE)
    set(Boost_VERSION "1.84.0")
    set(Boost_VERSION_STRING "1.84.0")
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    
    # CMake policy CMP0167: FindBoost is removed. Set to NEW to use CONFIG mode.
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()

    set(OTB_USE_QT OFF CACHE BOOL "" FORCE)
    set(OTB_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_MODULE_AS_STANDALONE OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OTB_DATA_ROOT "" CACHE PATH "OTB data root" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

    # Disable optional OTB ThirdParty activation options
    set(OTB_USE_OPENMP OFF CACHE BOOL "" FORCE)
    set(OTB_USE_SSE_FLAGS OFF CACHE BOOL "" FORCE)

    # OTB's GDAL init uses legacy GDAL_INCLUDE_DIR/GDAL_LIBRARY variables.
    # Modern GDAL (4.x) CMake config doesn't set these, so provide them.
    if(NOT GDAL_INCLUDE_DIR)
        if(TARGET GDAL::GDAL)
            get_target_property(GDAL_INCLUDE_DIR GDAL::GDAL INTERFACE_INCLUDE_DIRECTORIES)
        endif()
        if(NOT GDAL_INCLUDE_DIR)
            find_path(GDAL_INCLUDE_DIR gdal.h PATHS /usr/include /usr/local/include)
        endif()
    endif()
    if(NOT GDAL_LIBRARY)
        if(TARGET GDAL::GDAL)
            get_target_property(GDAL_LIBRARY GDAL::GDAL IMPORTED_LOCATION)
            if(NOT GDAL_LIBRARY)
                get_target_property(GDAL_LIBRARY GDAL::GDAL IMPORTED_LOCATION_RELEASE)
            endif()
        endif()
        if(NOT GDAL_LIBRARY)
            find_library(GDAL_LIBRARY NAMES gdal PATHS /usr/lib /usr/local/lib)
        endif()
    endif()
    # Skip GDAL config checking (try_run tests fail on Linux due to empty TEMP var)
    set(GDAL_CONFIG_CHECKING OFF CACHE BOOL "Tests to check gdal config." FORCE)

    # TinyXML is not available as a system package on this system.
    # Build a vendored copy from source and pre-set variables for OTB.
    add_subdirectory(cmake/TinyXML)
    set(TINYXML_FOUND TRUE CACHE BOOL "" FORCE)
    set(TINYXML_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/cmake/TinyXML/src" CACHE PATH "" FORCE)
    set(TINYXML_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/cmake/TinyXML/src" CACHE PATH "" FORCE)
    set(TINYXML_LIBRARY "tinyxml_vendored" CACHE STRING "" FORCE)
    set(TINYXML_LIBRARIES "tinyxml_vendored" CACHE STRING "" FORCE)
    set(TINYXML_VERSION "2.6.2" CACHE STRING "" FORCE)
    set(TIXML_TEST_STL TRUE CACHE BOOL "TinyXML STL test result" FORCE)

    # OTB CMAKE_SOURCE_DIR patches applied: GDAL init, GenerateExportHeaderCustom,
    # PreventInSourceBuilds, GetVersionFromGitTag, uninstall target all use OTB_SOURCE_DIR.
    # TinyXML init skips find_package when pre-set.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/otb_ref")
        add_subdirectory(otb_ref)
    else()
        message(WARNING "SICNU_BUILD_OTB=ON but otb_ref/ is missing — skipping OTB")
    endif()

    set(SICNU_HAS_OTB TRUE)
endif()

# GSL: required for Helmert/projective least-squares in the georeferencer
# (QgsLeastSquares::helmert uses LU decomposition, ::projective uses SVD).
# Mirrors upstream QGIS: WITH_GSL gates the HAVE_GSL config macro.
set(WITH_GSL TRUE CACHE BOOL "Determines whether GSL library should be used")
if(WITH_GSL)
  find_package(GSL REQUIRED)
  set(HAVE_GSL TRUE)
endif()

# configure_file AFTER all HAVE_* variables are resolved
configure_file(cmake_templates/qgsconfig.h.in ${CMAKE_BINARY_DIR}/qgsconfig.h)
include_directories(${CMAKE_BINARY_DIR})

# jsoncpp: normalize the CMake package across platforms. Upstream configs and
# vcpkg export a `jsoncpp` target, but Ubuntu 24.04's libjsoncpp-dev exports
# only `jsoncpp_lib`/`jsoncpp_static` (and no JSONCPP_* variables). Create a
# canonical `jsoncpp` target plus JSONCPP_* variables so every consumer below
# (processing, operators, workflow, jobs, cli, app, tests) links a real target
# that carries the include dir (e.g. /usr/include/jsoncpp) instead of a bare
# -ljsoncpp with no -I path.
find_package(jsoncpp QUIET)
if(NOT TARGET jsoncpp)
  if(TARGET jsoncpp_lib)
    add_library(jsoncpp INTERFACE IMPORTED)
    set_target_properties(jsoncpp PROPERTIES INTERFACE_LINK_LIBRARIES jsoncpp_lib)
    get_target_property(JSONCPP_INCLUDE_DIRS jsoncpp_lib INTERFACE_INCLUDE_DIRECTORIES)
    set(JSONCPP_LIBRARIES jsoncpp_lib)
  elseif(TARGET jsoncpp_static)
    add_library(jsoncpp INTERFACE IMPORTED)
    set_target_properties(jsoncpp PROPERTIES INTERFACE_LINK_LIBRARIES jsoncpp_static)
    get_target_property(JSONCPP_INCLUDE_DIRS jsoncpp_static INTERFACE_INCLUDE_DIRECTORIES)
    set(JSONCPP_LIBRARIES jsoncpp_static)
  endif()
endif()
if(JSONCPP_INCLUDE_DIRS)
  message(STATUS "jsoncpp include dir: ${JSONCPP_INCLUDE_DIRS}")
  # jsoncpp headers are consumed transitively via public processing headers
  # (e.g. algorithm_descriptor.h includes <json/json.h>), so every target that
  # pulls those headers needs the include path — not just targets that link
  # jsoncpp themselves (e.g. sicnu_python_isolated).
  include_directories(SYSTEM ${JSONCPP_INCLUDE_DIRS})
endif()

add_subdirectory(src/core)
add_subdirectory(src/data)
add_subdirectory(src/analysis)
add_subdirectory(src/app/georeferencer)
add_subdirectory(src/app/classification)
add_subdirectory(src/app/obia)
# SICNU_GEO_RS: P3 GUI options — disable optional deps not installed on this system
option(WITH_QWTPOLAR      "" OFF)
option(WITH_QSCINTILLA    "" OFF)
option(WITH_QTWEBENGINE   "" OFF)
option(WITH_QTGAMEPAD     "" OFF)
option(WITH_QUICK         "" OFF)
add_subdirectory(src/ui)
add_subdirectory(src/native)
add_subdirectory(src/gui)
add_subdirectory(src/processing)
add_subdirectory(src/agent)

# SICNU_GEO_RS: extra include paths needed by unconditional core sources
if(QCA_FOUND)
  target_include_directories(qgis_core SYSTEM PRIVATE ${QCA_INCLUDE_DIR} ${QCA_INCLUDE_DIR}/QtCrypto)
elseif(UNIX)
  target_include_directories(qgis_core SYSTEM PRIVATE /usr/include/qt6/Qca-qt6/QtCrypto)
endif()
target_include_directories(qgis_core PRIVATE
  ${CMAKE_SOURCE_DIR}/src/core/providers/vpc
  ${CMAKE_SOURCE_DIR}/src/core/providers/copc
  ${CMAKE_SOURCE_DIR}/src/core/providers/ept
)

# SICNU_GEO_RS: Plugin architecture subdirectories
add_subdirectory(src/plugins)
add_subdirectory(src/operators)
add_subdirectory(src/workflow)
add_subdirectory(src/jobs)
add_subdirectory(src/python/isolated)
# Headless CLI pipeline executor (depends on sicnu_operators)
if(TARGET sicnu_operators)
  add_subdirectory(src/cli)
endif()
add_subdirectory(src/app)

# SICNU_GEO_RS: Unit tests (Catch2)
if(ENABLE_TESTS)
    enable_testing()
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()
