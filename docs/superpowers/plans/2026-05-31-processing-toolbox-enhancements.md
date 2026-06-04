# Processing Toolbox Enhancements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enhance the Processing Toolbox with complete GDAL, OTB, and QGIS tools for raster and vector processing.

**Architecture:** The Processing Toolbox uses three providers:
1. `QgisAlgorithmsProvider` — QGIS native algorithms
2. `GdalToolsProvider` — GDAL-based algorithms (40+ tools)
3. `OtbToolsProvider` — OTB-based algorithms (20+ tools)

**Tech Stack:** Qt6, QGIS Processing Framework, GDAL, OTB, C++17

---

## Current State Summary

| Provider | Registered | Total Available | Coverage |
|----------|-----------|-----------------|----------|
| QgisAlgorithms | 13+ | 50+ | 26% |
| GdalTools | 16 | 40+ | 40% |
| OtbTools | 22 | 30+ | 73% |

---

## Complete GDAL Tools Inventory

### Raster Tools (30+)

**Format Conversion & Information:**
- [x] gdal_translate — format conversion, resampling, clipping
- [ ] gdalinfo — detailed metadata information
- [ ] gdal-config — GDAL configuration info
- [ ] gdal_version — version information

**Reprojection & Warping:**
- [x] gdalwarp — reprojection, warping, mosaicking
- [ ] gdaltransform — coordinate transformation

**Mosaicking & Tiling:**
- [x] gdal_merge.py — merge multiple rasters
- [x] gdalbuildvrt — build virtual raster
- [x] gdal_retile.py — tile rasters
- [x] gdaltindex — tile index creation

**Analysis & Processing:**
- [x] gdaldem — DEM analysis (hillshade, slope, aspect, etc.)
- [x] gdal_calc.py — raster calculator
- [x] gdal_proximity.py — proximity calculation
- [x] gdal_sieve.py — sieve filtering
- [x] gdal_fillnodata.py — fill nodata values
- [ ] gdal_grid — gridding from point data
- [ ] gdal_rasterize — rasterize vector data

**Contour & Polygon:**
- [x] gdal_contour — generate contour lines
- [x] gdal_polygonize.py — polygonize raster

**Color & Palette:**
- [ ] pct2rgb.py — convert palette to RGB
- [ ] rgb2pct.py — convert RGB to palette
- [ ] gdaladdo — add overviews

**Management:**
- [x] gdalmanage — manage raster datasets

**Web & Tiles:**
- [ ] gdal2tiles.py — generate tiles for web maps
- [ ] gdal2xyz.py — convert raster to XYZ

**Editing:**
- [ ] gdal_edit.py — edit raster metadata

### Vector Tools (10+)

**Format Conversion:**
- [x] ogr2ogr — format conversion, reprojection
- [ ] ogrinfo — detailed metadata information

**Spatial Operations:**
- [ ] ogr2ogr — spatial queries, clipping, filtering

**Index & Management:**
- [x] ogrtindex — tile index creation

---

## Complete OTB Tools Inventory

### Image Manipulation (10)

- [x] ExtractROI — extract region of interest
- [x] ConcatenateImages — concatenate bands
- [x] DynamicConvert — dynamic range conversion
- [x] Rescale — rescale pixel values
- [x] Convert — format conversion
- [ ] BandMathX — advanced band math
- [ ] PixelInfo — pixel information
- [ ] ReadImageInfo — image metadata
- [ ] ComputeImagesStatistics — compute statistics
- [ ] MultiResolutionPyramid — pyramid generation

### Filtering (8)

- [x] MeanShiftSmoothing — mean shift filtering
- [x] Smoothing — gaussian/anisotropic smoothing
- [x] BinaryMorphologicalOperation — binary morphology
- [ ] GrayScaleMorphologicalOperation — grayscale morphology
- [ ] MorphologicalProfilesAnalysis — morphological profiles
- [ ] BilateralFilter — bilateral filtering
- [ ] GaussianBlur — gaussian blur
- [ ] MedianFilter — median filtering

### Feature Extraction (6)

- [x] FeatureExtraction — feature extraction
- [x] HaralickTexture — texture features
- [x] RadiometricIndices — spectral indices
- [ ] GrayLevelCooccurrenceMatrix — GLCM
- [ ] LocalStatisticExtraction — local statistics
- [ ] LineSegmentDetection — line detection

### Segmentation (5)

- [x] Segmentation — generic segmentation
- [ ] MeanShiftSmoothing — mean shift segmentation
- [ ] LSMSSegmentation — large scale segmentation
- [ ] LSMSVectorization — vectorize segments
- [ ] SegmentationRegionsClassification — classify segments

### Classification (5)

- [x] TrainVectorClassifier — train classifier
- [x] ImageClassifier — classify image
- [x] KMeansClassification — k-means clustering
- [ ] SVMClassification — SVM classification
- [ ] RandomForestClassification — random forest

### Geometry (4)

- [x] OrthoRectification — orthorectification
- [x] BundleToPerfectSensor — pan-sharpening
- [x] Superimpose — superimpose images
- [ ] GridBasedImageResampling — grid resampling

### Stereopsis (3)

- [ ] StereoRectification — stereo rectification
- [ ] BlockMatching — block matching
- [ ] DisparityMapToElevationMap — disparity to elevation

### Hyperspectral (2)

- [ ] DimensionalityReduction — PCA, etc.
- [ ] EndmemberEstimation — endmember extraction

### Change Detection (2)

- [ ] MultivariateAlterationDetector — MAD
- [ ] BandDifference — band difference

### Calibration (2)

- [ ] OpticalCalibration — optical calibration
- [ ] SARCalibration — SAR calibration

---

## QGIS Native Algorithms Inventory

### Vector Geometry (15+)

- [x] Buffer — create buffers
- [x] Centroids — calculate centroids
- [x] ConvexHull — convex hull
- [x] Dissolve — dissolve features
- [x] Simplify — simplify geometry
- [ ] Multipart to Singleparts — split multipart
- [x] Singleparts to Multipart — merge to multipart
- [ ] Polygons to Lines — convert to lines
- [ ] Lines to Polygons — convert to polygons
- [ ] Densify — add vertices
- [ ] Smooth — smooth geometry
- [ ] Boundary — extract boundary
- [ ] Point on Surface — point on surface
- [ ] Pole of Inaccessibility — pole point
- [ ] Minimum Enclosing Geometry — minimum enclosing

### Vector Overlay (8)

- [x] Clip — clip by polygon
- [x] Intersection — intersection
- [x] Union — union
- [ ] Difference — difference
- [ ] Symmetrical Difference — symmetrical difference
- [ ] Identity — identity overlay
- [ ] Spatial Join — spatial join

### Vector Selection (6)

- [ ] Select by Location — spatial selection
- [ ] Select by Expression — expression selection
- [ ] Extract by Location — extract by location
- [ ] Extract by Expression — extract by expression
- [ ] Random Selection — random selection

### Vector Table (8)

- [ ] Field Calculator — calculate fields
- [ ] Add Field — add attribute field
- [ ] Delete Field — delete attribute field
- [ ] Rename Field — rename field
- [ ] Refactor Fields — refactor fields

### Vector Analysis (8)

- [ ] Nearest Nearest — nearest neighbor
- [ ] Distance Matrix — distance matrix
- [ ] Line Intersections — line intersections
- [ ] Hub Lines — hub lines

### Raster Analysis (10+)

- [x] Raster Calculator — raster calculator
- [x] Raster Resample — resample
- [x] Raster Clip — clip raster
- [x] Raster Statistics — statistics
- [x] Raster NDVI — NDVI calculation
- [ ] Raster Histogram — histogram
- [ ] Raster Unique Values — unique values
- [ ] Raster Zonal Statistics — zonal statistics

---

## Task Plan

### Task 5B.8: Add Missing GDAL Raster Tools

**Goal:** Add all essential GDAL raster tools to toolbox.

**Priority:** HIGH

**Files:**
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_info.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_proximity.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_sieve.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_fillnodata.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_grid.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_rasterize.h/.cpp`
- Modify: `src/processing/providers/gdal_tools/provider.cpp`

**Steps:**
- [ ] Add gdalinfo algorithm
- [ ] Add gdal_proximity algorithm
- [ ] Add gdal_sieve algorithm
- [ ] Add gdal_fillnodata algorithm
- [ ] Add gdal_grid algorithm
- [ ] Add gdal_rasterize algorithm
- [ ] Build and verify

---

### Task 5B.9: Add Missing GDAL Vector Tools

**Goal:** Add all essential GDAL vector tools to toolbox.

**Priority:** HIGH

**Files:**
- Create: `src/processing/providers/gdal_tools/algorithms/ogrinfo.h/.cpp`
- Modify: `src/processing/providers/gdal_tools/provider.cpp`

**Steps:**
- [ ] Add ogrinfo algorithm
- [ ] Enhance ogr2ogr with more options
- [ ] Build and verify

---

### Task 5B.10: Add Missing OTB Tools

**Goal:** Add all essential OTB tools to toolbox.

**Priority:** MEDIUM

**Files:**
- Create: New algorithm files as needed
- Modify: `src/processing/providers/otb_tools/provider.cpp`

**Steps:**
- [ ] Add BandMathX algorithm
- [ ] Add ComputeImagesStatistics algorithm
- [ ] Add MultiResolutionPyramid algorithm
- [ ] Add GrayScaleMorphologicalOperation algorithm
- [ ] Add PixelInfo algorithm
- [ ] Add ReadImageInfo algorithm
- [ ] Build and verify

---

### Task 5B.11: Add Missing QGIS Vector Tools

**Goal:** Add all essential QGIS vector tools to toolbox.

**Priority:** HIGH

**Files:**
- Create: New algorithm files as needed
- Modify: `src/processing/providers/qgis_algorithms/provider.cpp`

**Steps:**
- [ ] Add Difference algorithm
- [ ] Add Symmetrical Difference algorithm
- [ ] Add Select by Location algorithm
- [ ] Add Extract by Location algorithm
- [ ] Add Field Calculator algorithm
- [ ] Add Nearest Neighbor algorithm
- [ ] Add Distance Matrix algorithm
- [ ] Build and verify

---

### Task 5B.12: Add Custom RS Algorithms to Toolbox

**Goal:** Make Band Math, Spectral Index, and Atmospheric Correction available in Processing Toolbox.

**Priority:** HIGH

**Files:**
- Create: `src/processing/providers/qgis_algorithms/algorithms/native/band_math_algorithm.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/native/spectral_index_algorithm.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/native/atmospheric_correction_algorithm.h/.cpp`
- Modify: `src/processing/providers/qgis_algorithms/provider.cpp`

**Steps:**
- [ ] Create BandMathAlgorithm class wrapping `BandMath::evaluate()`
- [ ] Create SpectralIndexAlgorithm class wrapping `SpectralIndices::*`
- [ ] Create AtmosphericCorrectionAlgorithm class wrapping `AtmosphericCorrection::*`
- [ ] Register algorithms in provider under "Remote Sensing" group
- [ ] Build and verify

---

### Task 5B.14: Add Preset Coordinate Reference Systems

**Goal:** Add commonly used CRS presets for remote sensing workflows.

**Priority:** HIGH

**Files:**
- Create: `src/app/crs_presets.h/.cpp` — CRS preset definitions
- Modify: `src/app/main_window.cpp` — add CRS preset menu/toolbar

**Steps:**
- [ ] Define commonly used CRS presets:
  - WGS84 (EPSG:4326)
  - UTM zones (EPSG:32601-32660 for North, EPSG:32701-32760 for South)
  - Web Mercator (EPSG:3857)
  - China specific: CGCS2000 (EPSG:4547-4555), Beijing 1954 (EPSG:21413-21483), Xian 1980 (EPSG:2326-2349)
  - Common regional CRS
- [ ] Create CRS preset selection dialog
- [ ] Add CRS preset menu to Settings menu
- [ ] Add CRS preset toolbar buttons
- [ ] Add CRS preset to layer properties dialog
- [ ] Build and verify

---

### Task 5B.13: Algorithm Organization and Search

**Goal:** Improve algorithm categorization and search functionality.

**Priority:** MEDIUM

**Files:**
- Modify: All provider files — add proper group/subgroup metadata
- Modify: Algorithm files — add descriptions and keywords

**Steps:**
- [ ] Add proper `group()` and `subGroup()` methods to all algorithms
- [ ] Add algorithm descriptions and help text
- [ ] Add keywords/tags for better search
- [ ] Add algorithm icons where appropriate
- [ ] Test search functionality
- [ ] Build and verify

---

## Priority Order

1. **Task 5B.8** — Add Missing GDAL Raster Tools ✅ (HIGH)
2. **Task 5B.9** — Add Missing GDAL Vector Tools ✅ (HIGH)
3. **Task 5B.11** — Add Missing QGIS Vector Tools ✅ (HIGH)
4. **Task 5B.12** — Add Custom RS Algorithms to Toolbox ✅ (HIGH)
5. **Task 5B.10** — Add Missing OTB Tools ✅ (MEDIUM)
6. **Task 5B.14** — Add Preset Coordinate Reference Systems (HIGH)
7. **Task 5B.13** — Algorithm Organization and Search (MEDIUM)

---

## Notes

- GDAL has 40+ tools, currently only 16 registered (40% coverage)
- OTB has 30+ tools, currently 22 registered (73% coverage)
- QGIS has 50+ algorithms, currently 13+ registered (26% coverage)
- Main gaps are in GDAL raster tools and QGIS vector tools
- Custom RS algorithms (Band Math, Spectral Index, Atmospheric Correction) need to be added to toolbox
- Adding these would make the toolbox complete for remote sensing workflows

---

*Last updated: 2026-05-31*