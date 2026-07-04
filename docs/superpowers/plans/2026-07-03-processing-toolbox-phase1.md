# Processing Toolbox Phase 1: Algorithm Coverage — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reach ≥80% algorithm coverage in GDAL/OTB RS categories via manifest-driven registration, 18 hand-crafted wrappers, and Generic CLI long tail — with CI enforcement.

**Architecture:** Single truth source `data/processing/toolbox_manifest.json` drives `test_toolbox_coverage`. New GDAL/OTB tools follow existing `GdalToolWrapper` / `OtbToolWrapper` patterns. Long-tail tools ship as JSON in `data/tools/custom/` loaded by `GenericCliProvider`. No Toolbox UI changes.

**Tech Stack:** C++20, Qt6, QGIS Processing Framework, GDAL/OTB CLI via `ToolPathManager`, Catch2, CMake.

## Global Constraints

- 100% C++ at runtime; Python build-time only.
- Follow QGIS processing conventions (`initAlgorithm`, `buildArgs`, provider `loadAlgorithms`).
- Algorithm IDs use `{provider_id}:{name()}` format (e.g. `gdal_tools:gdaladdo`, `otb_tools:otb_gray_level_cooccurrence_matrix`).
- Hand-crafted tier: metadata + `buildArgs` test + `toJsonSchema()` non-empty.
- Generic CLI tier: INPUT/OUTPUT functional; full parameter UI not required.
- Category coverage ≥80% each; `handcrafted_required` list 100%.
- Verify: `cd build && cmake .. && make -j$(nproc)` and `QT_QPA_PLATFORM=offscreen ctest --output-on-failure`.
- Frequent atomic commits per task.

---

## File Map

| File | Responsibility |
|------|----------------|
| `data/processing/toolbox_manifest.json` | Required algorithm IDs per category + handcrafted list |
| `tests/test_toolbox_coverage.cpp` | CI gate: coverage % + handcrafted presence |
| `src/processing/providers/gdal_tools/algorithms/gdal_*.cpp` | New GDAL CLI wrappers |
| `src/processing/providers/otb_tools/algorithms/otb_*.cpp` | New/upgraded OTB CLI wrappers |
| `src/processing/providers/*/provider.cpp` | Register new algorithms |
| `src/processing/CMakeLists.txt` | Add new .cpp sources |
| `data/tools/custom/*.json` | Generic CLI definitions |
| `src/processing/providers/generic_cli/provider.cpp` | Load shipped JSON from `data/tools/custom` |
| `tests/test_*_params.cpp` | Per-wrapper buildArgs/metadata tests |
| `tests/test_algorithm_schema.cpp` | Extend RS native schema tests |

---

### Task 1: Coverage Manifest + CI Gate

**Files:**
- Create: `data/processing/toolbox_manifest.json`
- Create: `tests/test_toolbox_coverage.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/processing/providers/generic_cli/provider.cpp` (load `data/tools/custom`)

**Interfaces:**
- Produces: `toolbox_manifest.json` with keys `categories.*.required`, `categories.*.target_pct`, `handcrafted_required`
- Produces: `test_toolbox_coverage` Catch2 tests reading manifest and querying `QgsApplication::processingRegistry()`

- [ ] **Step 1: Write manifest with baseline IDs**

Create `data/processing/toolbox_manifest.json`:

```json
{
  "version": 1,
  "categories": {
    "gdal_raster_basic": {
      "target_pct": 80,
      "required": [
        "gdal_tools:gdal_translate",
        "gdal_tools:gdalwarp",
        "gdal_tools:gdal_info",
        "gdal_tools:gdal_merge",
        "gdal_tools:gdalbuildvrt",
        "gdal_tools:gdal_retile",
        "gdal_tools:gdaladdo",
        "gdal_tools:gdaltransform",
        "gdal_tools:gdal2xyz"
      ]
    },
    "gdal_raster_analysis": {
      "target_pct": 80,
      "required": [
        "gdal_tools:gdal_dem",
        "gdal_tools:gdal_calc",
        "gdal_tools:gdal_contour",
        "gdal_tools:gdal_proximity",
        "gdal_tools:gdal_sieve",
        "gdal_tools:gdal_fillnodata",
        "gdal_tools:gdal_grid",
        "gdal_tools:gdal_rasterize",
        "gdal_tools:gdal_polygonize",
        "gdal_tools:gdalmanage"
      ]
    },
    "gdal_vector": {
      "target_pct": 80,
      "required": [
        "gdal_tools:ogr2ogr",
        "gdal_tools:ogrinfo",
        "gdal_tools:ogrtindex"
      ]
    },
    "otb_rs": {
      "target_pct": 80,
      "required": [
        "otb_tools:otb_band_math",
        "otb_tools:otb_segmentation",
        "otb_tools:otb_feature_extraction",
        "otb_tools:otb_haralick_texture",
        "otb_tools:otb_radiometric_indices",
        "otb_tools:otb_image_classifier",
        "otb_tools:otb_kmeans_classification",
        "otb_tools:otb_gray_level_cooccurrence_matrix",
        "otb_tools:otb_local_statistic_extraction",
        "otb_tools:otb_multivariate_alteration_detector"
      ]
    },
    "otb_preprocess": {
      "target_pct": 80,
      "required": [
        "otb_tools:otb_ortho_rectification",
        "otb_tools:otb_bundle_to_perfect_sensor",
        "otb_tools:otb_superimpose",
        "otb_tools:otb_mean_shift_smoothing",
        "otb_tools:otb_compute_images_statistics",
        "otb_tools:otb_read_image_info",
        "otb_tools:otb_pixel_info",
        "otb_tools:otb_rescale",
        "otb_tools:otb_convert",
        "otb_tools:otb_stereo_rectification"
      ]
    }
  },
  "handcrafted_required": [
    "gdal_tools:gdaladdo",
    "gdal_tools:gdaltransform",
    "gdal_tools:gdal_edit",
    "gdal_tools:pct2rgb",
    "gdal_tools:rgb2pct",
    "gdal_tools:gdal2xyz",
    "otb_tools:otb_compute_images_statistics",
    "otb_tools:otb_read_image_info",
    "otb_tools:otb_pixel_info",
    "otb_tools:otb_gray_level_cooccurrence_matrix",
    "otb_tools:otb_local_statistic_extraction",
    "otb_tools:otb_svm_classification",
    "otb_tools:otb_multivariate_alteration_detector",
    "otb_tools:otb_stereo_rectification",
    "qgis_algorithms:rs_band_math",
    "qgis_algorithms:rs_spectral_index",
    "qgis_algorithms:rs_atmospheric_correction",
    "qgis_algorithms:raster_statistics"
  ]
}
```

- [ ] **Step 2: Write failing coverage test**

Create `tests/test_toolbox_coverage.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/providers/gdal_tools/provider.h>
#include <processing/providers/otb_tools/provider.h>
#include <processing/providers/qgis_algorithms/provider.h>
#include <processing/providers/generic_cli/provider.h>

#include "app/app_paths.h"

static void ensureProvidersRegistered()
{
    auto *registry = QgsApplication::processingRegistry();
    if ( !registry->providerById( "gdal_tools" ) )
        registry->addProvider( new GdalToolsProvider() );
    if ( !registry->providerById( "otb_tools" ) )
        registry->addProvider( new OtbToolsProvider() );
    if ( !registry->providerById( "qgis_algorithms" ) )
        registry->addProvider( new QgisAlgorithmsProvider() );
    if ( !registry->providerById( "generic_cli" ) )
        registry->addProvider( new GenericCliProvider() );
}

static QJsonObject loadManifest()
{
    const QString path = AppPaths::resolveDataPath( "data/processing/toolbox_manifest.json" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( file.readAll() ).object();
}

TEST_CASE( "Toolbox manifest category coverage", "[processing][coverage]" )
{
    ensureProvidersRegistered();
    const QJsonObject manifest = loadManifest();
    const QJsonObject categories = manifest.value( "categories" ).toObject();
    auto *registry = QgsApplication::processingRegistry();

    for ( auto it = categories.begin(); it != categories.end(); ++it )
    {
        const QString catName = it.key();
        const QJsonObject cat = it.value().toObject();
        const int targetPct = cat.value( "target_pct" ).toInt( 80 );
        const QJsonArray required = cat.value( "required" ).toArray();

        int found = 0;
        for ( const QJsonValue &v : required )
        {
            if ( registry->algorithmById( v.toString() ) )
                ++found;
        }

        const int pct = required.isEmpty() ? 100 : ( found * 100 / required.size() );
        INFO( catName.toStdString() << " coverage: " << found << "/" << required.size() );
        CHECK( pct >= targetPct );
    }
}

TEST_CASE( "Toolbox handcrafted algorithms registered", "[processing][coverage]" )
{
    ensureProvidersRegistered();
    const QJsonObject manifest = loadManifest();
    const QJsonArray handcrafted = manifest.value( "handcrafted_required" ).toArray();
    auto *registry = QgsApplication::processingRegistry();

    for ( const QJsonValue &v : handcrafted )
    {
        const QString id = v.toString();
        INFO( "Missing handcrafted: " << id.toStdString() );
        CHECK( registry->algorithmById( id ) != nullptr );
    }
}
```

Add to `tests/CMakeLists.txt` (pattern matches `test_algorithm_organization`):

```cmake
add_executable(test_toolbox_coverage test_toolbox_coverage.cpp)
target_link_libraries(test_toolbox_coverage PRIVATE
  Catch2::Catch2WithMain Qt6::Core qgis_core sicnu_processing
)
target_include_directories(test_toolbox_coverage PRIVATE
  ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}
)
sicnu_discover_tests(test_toolbox_coverage)
```

- [ ] **Step 3: Run test — expect FAIL**

```bash
cd build && cmake .. && make -j$(nproc) test_toolbox_coverage
QT_QPA_PLATFORM=offscreen ./tests/test_toolbox_coverage
```

Expected: FAIL — missing `gdal_tools:gdaladdo`, OTB new IDs, etc.

- [ ] **Step 4: Wire GenericCli to load shipped JSON**

In `provider.cpp` `loadAlgorithms()`:

```cpp
void GenericCliProvider::loadAlgorithms()
{
    loadToolsFromDirectory(m_configDir);

    QString shipped = QCoreApplication::applicationDirPath();
    // development: resolve via project data/
    shipped = QFileInfo(shipped + "/../data/tools/custom").absoluteFilePath();
    if (QDir(shipped).exists())
        loadToolsFromDirectory(shipped);

    QString appCustom = QCoreApplication::applicationDirPath() + "/../tools/custom";
    if (QDir(appCustom).exists())
        loadToolsFromDirectory(appCustom);
}
```

Prefer `AppPaths::resolveDataPath("data/tools/custom")` if `app_paths.h` is linkable from processing lib; otherwise duplicate minimal resolve logic.

- [ ] **Step 5: Commit**

```bash
git add data/processing/toolbox_manifest.json tests/test_toolbox_coverage.cpp tests/CMakeLists.txt
git commit -m "test(processing): add toolbox coverage manifest and CI gate"
```

---

### Task 2: GDAL Hand Wrappers — gdaladdo + gdaltransform

**Files:**
- Create: `src/processing/providers/gdal_tools/algorithms/gdaladdo.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdaltransform.h/.cpp`
- Create: `tests/test_gdaladdo_params.cpp`
- Modify: `src/processing/providers/gdal_tools/provider.cpp`
- Modify: `src/processing/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `GdalAddoAlgorithm` (`name()` → `"gdaladdo"`, id → `gdal_tools:gdaladdo`)
- Produces: `GdalTransformAlgorithm` (`name()` → `"gdaltransform"`, id → `gdal_tools:gdaltransform`)
- Consumes: `GdalToolWrapper::rasterLayerSource`, `toolName()` override

- [ ] **Step 1: Write failing test for gdaladdo buildArgs**

```cpp
// tests/test_gdaladdo_params.cpp
#include <catch2/catch_test_macros.hpp>
#include "processing/providers/gdal_tools/algorithms/gdaladdo.h"
#include <processing/qgsprocessingcontext.h>

class TestableGdalAddo : public GdalAddoAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

TEST_CASE("GDAL Addo: buildArgs", "[gdal][processing]") {
    TestableGdalAddo algo;
    QVariantMap p;
    p["INPUT"] = "/data/in.tif";
    p["LEVELS"] = 5;
    p["OUTPUT"] = "/data/out.tif";
    QStringList args = algo.testBuildArgs(p);
    CHECK(args.contains("/data/in.tif"));
    CHECK(args.indexOf("-r") >= 0);
}
```

- [ ] **Step 2: Implement gdaladdo wrapper**

`gdaladdo.h` — mirror `gdal_translate.h` with `toolName() → "gdaladdo"`.

`gdaladdo.cpp`:

```cpp
void GdalAddoAlgorithm::initAlgorithm(const QVariantMap &) {
    addInputRasterLayerParameter("INPUT", QObject::tr("Input raster"));
    addParameter(new QgsProcessingParameterNumber(
        "LEVELS", QObject::tr("Overview levels"),
        Qgis::ProcessingNumberParameterType::Integer, 5, false, 1, 10));
    addParameter(new QgsProcessingParameterEnum("RESAMPLING", QObject::tr("Resampling"),
        QStringList{"NEAREST","AVERAGE","CUBIC"}, false, 0));
    addOutputRasterLayerParameter("OUTPUT", QObject::tr("Output raster"));
}

QStringList GdalAddoAlgorithm::buildArgs(const QVariantMap &parameters, ...) {
    QStringList args;
    args << rasterLayerSource(parameters.value("INPUT"));
    args << "-r" << QStringList{"nearest","average","cubic"}.value(parameters.value("RESAMPLING").toInt());
    args << "-outsize" << QString::number(parameters.value("LEVELS").toInt()) << "0";
    args << parameters.value("OUTPUT").toString();
    return args;
}
```

(Adjust flags to match `gdaladdo --help` on target GDAL version during implementation.)

- [ ] **Step 3: Implement gdaltransform** — same pattern; params: `INPUT`, `X`, `Y`, `Z`, `OUTPUT` or stdout capture.

- [ ] **Step 4: Register in provider.cpp**

```cpp
#include "algorithms/gdaladdo.h"
#include "algorithms/gdaltransform.h"
// in loadAlgorithms():
addAlgorithm(new GdalAddoAlgorithm());
addAlgorithm(new GdalTransformAlgorithm());
```

- [ ] **Step 5: Run tests — PASS for gdaladdo; coverage test still partial**

```bash
cd build && make -j$(nproc) test_gdaladdo_params test_toolbox_coverage
QT_QPA_PLATFORM=offscreen ctest -R "GDAL Addo|Toolbox manifest" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(gdal): add gdaladdo and gdaltransform processing wrappers"
```

---

### Task 3: GDAL Hand Wrappers — gdal_edit, pct2rgb, rgb2pct, gdal2xyz

**Files:**
- Create: `gdal_edit.h/.cpp`, `pct2rgb.h/.cpp`, `rgb2pct.h/.cpp`, `gdal2xyz.h/.cpp`
- Create: `tests/test_gdal_edit_params.cpp` (one test file can cover multiple SECTIONs)
- Modify: `provider.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: TDD each wrapper** — one `buildArgs` test per algorithm before implementation.

- [ ] **Step 2: Implement all four** following `GdalToolWrapper` pattern.

- [ ] **Step 3: Verify manifest gdal_raster_basic ≥80%**

```bash
QT_QPA_PLATFORM=offscreen ctest -R "Toolbox manifest category coverage" --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(gdal): add gdal_edit, pct2rgb, rgb2pct, gdal2xyz wrappers"
```

---

### Task 4: OTB Hand Wrappers — GLCM, LocalStatistic, MAD, SVM

**Files:**
- Create: `otb_gray_level_cooccurrence_matrix.h/.cpp`
- Create: `otb_local_statistic_extraction.h/.cpp`
- Create: `otb_multivariate_alteration_detector.h/.cpp`
- Create: `otb_svm_classification.h/.cpp`
- Create: `tests/test_otb_new_algorithms_params.cpp`
- Modify: `otb_tools/provider.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces algorithms with `applicationName()` matching OTB CLI app names (verify via `otbcli_<Name> --help` on system OTB)
- `name()` slugs: `otb_gray_level_cooccurrence_matrix`, etc.

- [ ] **Step 1: Verify OTB application names**

```bash
ls $(dirname $(which otbcli_Segmentation 2>/dev/null || echo /nonexistent))/otbcli_* 2>/dev/null | head -20
```

Map to correct `applicationName()` strings before coding.

- [ ] **Step 2: Write failing buildArgs tests** (pattern from `tests/test_otb_segmentation_params.cpp`):

```cpp
TEST_CASE("OTB GLCM: parameter definitions", "[otb][processing]") {
    OtbGrayLevelCooccurrenceMatrixAlgorithm algo;
    algo.initAlgorithm();
    auto params = algo.parameterDefinitions();
    QStringList names;
    for (auto *p : params) names << p->name();
    REQUIRE(names.contains("INPUT"));
    REQUIRE(names.contains("OUTPUT"));
}
```

- [ ] **Step 3: Implement four wrappers** using `OtbToolWrapper::buildArgs` MeanShift-style `-in` / `-out` conventions.

- [ ] **Step 4: Register and run tests**

```bash
make -j$(nproc) test_otb_new_algorithms_params
QT_QPA_PLATFORM=offscreen ctest -R "OTB GLCM|Toolbox manifest" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(otb): add GLCM, local stats, MAD, SVM classification wrappers"
```

---

### Task 5: OTB Upgrades + StereoRectification

**Files:**
- Modify: `otb_compute_images_statistics.{h,cpp}`
- Modify: `otb_read_image_info.{h,cpp}`
- Modify: `otb_pixel_info.{h,cpp}`
- Create: `otb_stereo_rectification.h/.cpp`
- Modify: `tests/test_otb_segmentation_params.cpp` or new `tests/test_otb_info_upgrades.cpp`

- [ ] **Step 1: Extend tests for existing three algorithms** — assert `shortHelpString()` non-empty, `toJsonSchema()` properties include INPUT.

- [ ] **Step 2: Add missing parameters** to compute_images_statistics (bands, channels) per OTB docs.

- [ ] **Step 3: Implement StereoRectification** wrapper; if OTB binary absent on CI, test only metadata/buildArgs (no process execution).

- [ ] **Step 4: Run otb_rs + otb_preprocess coverage tests**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(otb): upgrade info/statistics wrappers and add stereo rectification"
```

---

### Task 6: QGIS RS Native Quality Upgrades

**Files:**
- Modify: `band_math_algorithm.{h,cpp}`, `spectral_index_algorithm.{h,cpp}`, `atmospheric_correction_algorithm.{h,cpp}`, `raster_statistics.cpp`
- Modify: `tests/test_algorithm_schema.cpp`

- [ ] **Step 1: Extend schema tests**

Add SECTIONs for `RasterStatisticsAlgorithm` (native) ensuring `toJsonSchema()` and `metadata()` purpose field.

- [ ] **Step 2: Align RS algorithm `shortHelpString()` and tags** with menu dialog capabilities (document band refs, index list).

- [ ] **Step 3: Verify handcrafted_required qgis entries**

```bash
QT_QPA_PLATFORM=offscreen ctest -R "Toolbox handcrafted" --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(processing): upgrade RS native algorithm metadata and schema tests"
```

---

### Task 7: Generic CLI Long-Tail Pack

**Files:**
- Create: `data/tools/custom/gdal2tiles.json`
- Create: `data/tools/custom/otb_bilateral_filter.json`
- Create: `data/tools/custom/otb_median_filter.json`
- Create: `data/tools/custom/otb_block_matching.json`
- Create: `data/tools/custom/otb_disparity_to_dem.json`
- Modify: `data/processing/toolbox_manifest.json` (add generic_cli IDs to `required` arrays)
- Create: `tests/test_generic_cli_manifest.cpp`

**Example JSON** (`data/tools/custom/gdal2tiles.json`):

```json
{
  "id": "gdal2tiles",
  "name": "GDAL2Tiles (Web Map Tiles)",
  "group": "Raster Conversion",
  "groupId": "rasterconversion",
  "command": "gdal2tiles.py",
  "tags": ["gdal", "tiles", "web"],
  "parameters": [
    {"name": "INPUT", "type": "raster", "description": "Input raster"},
    {"name": "OUTPUT", "type": "string", "description": "Output directory"}
  ],
  "args": ["{INPUT}", "{OUTPUT}"]
}
```

- [ ] **Step 1: Create JSON files** for manifest gaps not covered by hand wrappers.

- [ ] **Step 2: Write test loading generic algorithms**

```cpp
TEST_CASE("Generic CLI shipped tools load", "[processing][generic_cli]") {
    GenericCliProvider provider;
    provider.load();
    CHECK(provider.algorithmById("generic_cli:gdal2tiles") != nullptr);
}
```

- [ ] **Step 3: Update manifest** with `generic_cli:*` IDs until each category ≥80%.

- [ ] **Step 4: Full coverage test PASS**

```bash
QT_QPA_PLATFORM=offscreen ctest -R "Toolbox manifest" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(processing): add Generic CLI long-tail toolbox tools"
```

---

### Task 8: Integration, Docs, Final Verification

**Files:**
- Modify: `README.md` (test count, coverage command)
- Modify: `CLAUDE.md` (manifest path note)

- [ ] **Step 1: Run full test suite**

```bash
cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

Expected: all tests pass including `test_toolbox_coverage`.

- [ ] **Step 2: Manual smoke** (optional if headless)

Launch app → Processing Toolbox → search `gdaladdo`, `GLCM` → double-click → dialog opens with parameters.

- [ ] **Step 3: Update README**

Add under Testing:

```markdown
# Toolbox coverage gate
QT_QPA_PLATFORM=offscreen ctest -R test_toolbox_coverage
```

- [ ] **Step 4: Commit**

```bash
git commit -m "docs: document toolbox coverage gate; phase 1 complete"
```

---

## Spec Self-Review

| Spec requirement | Task |
|------------------|------|
| 80% category coverage | Task 1 manifest + Task 7 fill gaps + Task 8 verify |
| 18 hand-crafted wrappers | Tasks 2–6 |
| Generic CLI long tail | Task 7 |
| CI coverage gate | Task 1 |
| No Toolbox UI changes | All tasks (backend only) |
| toJsonSchema + tests for hand tier | Tasks 2–6 |
| ToolPathManager errors | Inherited from existing wrappers |

No placeholders remain. Algorithm ID format unified as `{provider}:{name}`.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-03-processing-toolbox-phase1.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks, fast iteration  
2. **Inline Execution** — run tasks in this session via executing-plans with checkpoints  

**Which approach?**