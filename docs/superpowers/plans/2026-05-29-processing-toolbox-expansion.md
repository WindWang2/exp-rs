# Processing Toolbox Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the processing toolbox from 16 algorithms to 100+ by adding GDAL Tools, OTB Tools, and QGIS Algorithms providers.

**Architecture:** Multi-provider architecture with 4 independent providers registered in QgsProcessingRegistry. GDAL/OTB tools use QProcess wrappers for external command-line tools. QGIS algorithms use native QgsProcessingAlgorithm implementations. ToolPathManager handles bundled tool discovery.

**Tech Stack:** C++17, Qt6, QGIS Processing Framework, QProcess, CMake FetchContent

---

## File Structure

```
src/processing/
├── tools/
│   ├── tool_path_manager.h          # Tool discovery (app dir > env > PATH)
│   └── tool_path_manager.cpp
├── providers/
│   ├── sicnu_native/                 # REFACTOR: move from src/processing/
│   │   ├── provider.h
│   │   ├── provider.cpp
│   │   └── algorithms/
│   │       ├── vector/
│   │       │   ├── buffer_algorithm.h/.cpp
│   │       │   ├── centroids_algorithm.h/.cpp
│   │       │   ├── convex_hull_algorithm.h/.cpp
│   │       │   ├── dissolve_algorithm.h/.cpp
│   │       │   ├── simplify_algorithm.h/.cpp
│   │       │   ├── clip_algorithm.h/.cpp
│   │       │   ├── intersection_algorithm.h/.cpp
│   │       │   ├── union_algorithm.h/.cpp
│   │       │   ├── difference_algorithm.h/.cpp
│   │       │   └── extract_by_attribute_algorithm.h/.cpp
│   │       ├── raster/
│   │       │   ├── clip_raster_algorithm.h/.cpp
│   │       │   ├── raster_statistics_algorithm.h/.cpp
│   │       │   └── hillshade_algorithm.h/.cpp
│   │       └── projection/
│   │           ├── reproject_algorithm.h/.cpp
│   │           └── assign_projection_algorithm.h/.cpp
│   ├── gdal_tools/                   # NEW
│   │   ├── provider.h/.cpp
│   │   ├── gdal_tool_wrapper.h/.cpp  # Base class for GDAL tools
│   │   └── algorithms/
│   │       ├── gdal_translate.h/.cpp
│   │       ├── gdal_warp.h/.cpp
│   │       ├── gdal_info.h/.cpp
│   │       ├── gdal_dem.h/.cpp
│   │       ├── gdal_contour.h/.cpp
│   │       ├── gdal_polygonize.h/.cpp
│   │       ├── gdal_merge.h/.cpp
│   │       ├── gdal_calc.h/.cpp
│   │       ├── gdal_retile.h/.cpp
│   │       ├── gdalbuildvrt.h/.cpp
│   │       ├── gdaltindex.h/.cpp
│   │       ├── gdalmanage.h/.cpp
│   │       ├── ogr2ogr.h/.cpp
│   │       ├── ogrinfo.h/.cpp
│   │       └── ogrtindex.h/.cpp
│   ├── otb_tools/                    # NEW
│   │   ├── provider.h/.cpp
│   │   ├── otb_tool_wrapper.h/.cpp   # Base class for OTB tools
│   │   └── algorithms/
│   │       ├── otb_band_math.h/.cpp
│   │       ├── otb_concatenate_images.h/.cpp
│   │       ├── otb_extract_roi.h/.cpp
│   │       ├── otb_dynamic_convert.h/.cpp
│   │       ├── otb_rescale.h/.cpp
│   │       ├── otb_convert.h/.cpp
│   │       ├── otb_mean_shift_smoothing.h/.cpp
│   │       ├── otb_lsms.h/.cpp
│   │       ├── otb_segmentation.h/.cpp
│   │       ├── otb_train_vector_classifier.h/.cpp
│   │       ├── otb_image_classifier.h/.cpp
│   │       ├── otb_kmeans_classification.h/.cpp
│   │       ├── otb_feature_extraction.h/.cpp
│   │       ├── otb_haralick_texture.h/.cpp
│   │       ├── otb_radiometric_indices.h/.cpp
│   │       ├── otb_ortho_rectification.h/.cpp
│   │       ├── otb_bundle_to_perfect_sensor.h/.cpp
│   │       ├── otb_superimpose.h/.cpp
│   │       └── otb_binary_morphological.h/.cpp
│   └── qgis_algorithms/             # NEW
│       ├── provider.h/.cpp
│       └── algorithms/
│           ├── raster/
│           │   ├── raster_calculator.h/.cpp
│           │   ├── raster_resample.h/.cpp
│           │   ├── raster_clip.h/.cpp
│           │   ├── raster_merge_bands.h/.cpp
│           │   ├── raster_ndvi.h/.cpp
│           │   └── raster_statistics.h/.cpp
│           └── vector/
│               ├── vector_buffer.h/.cpp
│               ├── vector_clip.h/.cpp
│               ├── vector_dissolve.h/.cpp
│               ├── vector_merge.h/.cpp
│               ├── vector_spatial_query.h/.cpp
│               ├── vector_attribute_query.h/.cpp
│               └── vector_reproject.h/.cpp
└── CMakeLists.txt                    # Processing library build
```

---

## Phase 1: Infrastructure

### Task 1: Create ToolPathManager

**Files:**
- Create: `src/processing/tools/tool_path_manager.h`
- Create: `src/processing/tools/tool_path_manager.cpp`

- [ ] **Step 1: Create tool_path_manager.h**

```cpp
// src/processing/tools/tool_path_manager.h
#pragma once

#include <QString>

class ToolPathManager
{
public:
    static ToolPathManager &instance();

    // GDAL tools
    QString gdalToolPath(const QString &toolName) const;
    bool isGdalAvailable() const;

    // OTB tools
    QString otbToolPath(const QString &appName) const;
    bool isOtbAvailable() const;

    // Set custom paths (for user configuration)
    void setGdalPath(const QString &path);
    void setOtbPath(const QString &path);

private:
    ToolPathManager();
    QString findInAppDir(const QString &subdir, const QString &toolName) const;
    QString findInEnv(const QString &envVar, const QString &toolName) const;
    QString findInSystemPath(const QString &toolName) const;

    QString m_customGdalPath;
    QString m_customOtbPath;
};
```

- [ ] **Step 2: Create tool_path_manager.cpp**

```cpp
// src/processing/tools/tool_path_manager.cpp
#include "tool_path_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

ToolPathManager &ToolPathManager::instance()
{
    static ToolPathManager s_instance;
    return s_instance;
}

ToolPathManager::ToolPathManager() = default;

QString ToolPathManager::gdalToolPath(const QString &toolName) const
{
    // 1. Custom path
    if (!m_customGdalPath.isEmpty()) {
        QString p = QDir(m_customGdalPath).filePath(toolName);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory
    QString appPath = findInAppDir("tools/gdal", toolName);
    if (!appPath.isEmpty()) return appPath;

    // 3. Environment variable
    QString envPath = findInEnv("SICNU_GDAL_PATH", toolName);
    if (!envPath.isEmpty()) return envPath;

    // 4. System PATH
    return findInSystemPath(toolName);
}

bool ToolPathManager::isGdalAvailable() const
{
    return !gdalToolPath("gdal_translate").isEmpty();
}

QString ToolPathManager::otbToolPath(const QString &appName) const
{
    QString cliName = "otbcli_" + appName;

    // 1. Custom path
    if (!m_customOtbPath.isEmpty()) {
        QString p = QDir(m_customOtbPath).filePath(cliName);
        if (QFileInfo::exists(p)) return p;
    }

    // 2. App directory
    QString appPath = findInAppDir("tools/otb", cliName);
    if (!appPath.isEmpty()) return appPath;

    // 3. Environment variable
    QString envPath = findInEnv("SICNU_OTB_PATH", cliName);
    if (!envPath.isEmpty()) return envPath;

    // 4. System PATH
    return findInSystemPath(cliName);
}

bool ToolPathManager::isOtbAvailable() const
{
    return !otbToolPath("BandMath").isEmpty();
}

void ToolPathManager::setGdalPath(const QString &path)
{
    m_customGdalPath = path;
}

void ToolPathManager::setOtbPath(const QString &path)
{
    m_customOtbPath = path;
}

QString ToolPathManager::findInAppDir(const QString &subdir, const QString &toolName) const
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString p = QDir(appDir).filePath(subdir + "/" + toolName);
    return QFileInfo::exists(p) ? p : QString();
}

QString ToolPathManager::findInEnv(const QString &envVar, const QString &toolName) const
{
    QString envDir = QProcessEnvironment::systemEnvironment().value(envVar);
    if (envDir.isEmpty()) return QString();
    QString p = QDir(envDir).filePath(toolName);
    return QFileInfo::exists(p) ? p : QString();
}

QString ToolPathManager::findInSystemPath(const QString &toolName) const
{
    QProcess proc;
    proc.start("which", QStringList() << toolName);
    proc.waitForFinished(3000);
    QString result = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return result.isEmpty() ? QString() : result;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/processing/tools/
git commit -m "feat(processing): add ToolPathManager for external tool discovery"
```

---

### Task 2: Create GdalToolWrapper Base Class

**Files:**
- Create: `src/processing/providers/gdal_tools/gdal_tool_wrapper.h`
- Create: `src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp`

- [ ] **Step 1: Create gdal_tool_wrapper.h**

```cpp
// src/processing/providers/gdal_tools/gdal_tool_wrapper.h
#pragma once

#include <QgsProcessingAlgorithm.h>
#include <QProcess>

class GdalToolWrapper : public QgsProcessingAlgorithm
{
public:
    GdalToolWrapper() = default;

    // Subclasses implement these
    virtual QString toolName() const = 0;
    virtual QString displayName() const = 0;
    virtual QString group() const { return "GDAL"; }
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    // Common implementation
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    QString provider() const override { return "gdal_tools"; }
    QgsProcessingAlgorithm::Flags flags() const override { return QgsProcessingAlgorithm::FlagSupportsBatch; }

protected:
    // Helper to run an external tool
    bool runExternalTool(const QString &program, const QStringList &args,
                         QgsProcessingFeedback *feedback);

    // Common parameter helpers
    void addInputRasterLayerParameter(const QString &name = "INPUT",
                                      const QString &description = "Input raster layer");
    void addOutputRasterLayerParameter(const QString &name = "OUTPUT",
                                       const QString &description = "Output raster layer");
    void addInputVectorLayerParameter(const QString &name = "INPUT",
                                      const QString &description = "Input vector layer");
    void addOutputVectorLayerParameter(const QString &name = "OUTPUT",
                                       const QString &description = "Output vector layer");
    void addExtentParameter(const QString &name = "EXTENT");
    void addCrsParameter(const QString &name = "TARGET_CRS",
                         const QString &description = "Target CRS");
};
```

- [ ] **Step 2: Create gdal_tool_wrapper.cpp**

```cpp
// src/processing/providers/gdal_tools/gdal_tool_wrapper.cpp
#include "gdal_tool_wrapper.h"
#include "src/processing/tools/tool_path_manager.h"

#include <QgsProcessingContext.h>
#include <QgsProcessingFeedback.h>
#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterVectorLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterVectorDestination.h>
#include <QgsProcessingParameterExtent.h>
#include <QgsProcessingParameterCrs.h>

QVariantMap GdalToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().gdalToolPath(toolName());
    if (program.isEmpty()) {
        feedback->reportError(tr("GDAL tool '%1' not found. Ensure GDAL tools are installed.").arg(toolName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    if (!runExternalTool(program, args, feedback)) {
        return {};
    }

    QVariantMap results;
    if (parameters.contains("OUTPUT")) {
        results["OUTPUT"] = parameters.value("OUTPUT");
    }
    return results;
}

bool GdalToolWrapper::runExternalTool(const QString &program, const QStringList &args,
                                       QgsProcessingFeedback *feedback)
{
    feedback->pushInfo(tr("Running: %1 %2").arg(program, args.join(" ")));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        feedback->reportError(tr("Failed to start tool: %1").arg(proc.errorString()));
        return false;
    }

    while (proc.state() == QProcess::Running) {
        if (feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(tr("Tool execution canceled by user."));
            return false;
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            feedback->pushInfo(QString::fromUtf8(output));
        }
    }

    if (proc.exitCode() != 0) {
        feedback->reportError(tr("Tool failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }

    return true;
}

void GdalToolWrapper::addInputRasterLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterRasterLayer(name, description));
}

void GdalToolWrapper::addOutputRasterLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterRasterDestination(name, description));
}

void GdalToolWrapper::addInputVectorLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterVectorLayer(name, description));
}

void GdalToolWrapper::addOutputVectorLayerParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterVectorDestination(name, description));
}

void GdalToolWrapper::addExtentParameter(const QString &name)
{
    addParameter(new QgsProcessingParameterExtent(name, tr("Extent")));
}

void GdalToolWrapper::addCrsParameter(const QString &name, const QString &description)
{
    addParameter(new QgsProcessingParameterCrs(name, description, QVariant(), true));
}
```

- [ ] **Step 3: Commit**

```bash
git add src/processing/providers/gdal_tools/gdal_tool_wrapper.*
git commit -m "feat(processing): add GdalToolWrapper base class"
```

---

### Task 3: Create OtbToolWrapper Base Class

**Files:**
- Create: `src/processing/providers/otb_tools/otb_tool_wrapper.h`
- Create: `src/processing/providers/otb_tools/otb_tool_wrapper.cpp`

- [ ] **Step 1: Create otb_tool_wrapper.h**

```cpp
// src/processing/providers/otb_tools/otb_tool_wrapper.h
#pragma once

#include <QgsProcessingAlgorithm.h>
#include <QProcess>

class OtbToolWrapper : public QgsProcessingAlgorithm
{
public:
    OtbToolWrapper() = default;

    virtual QString applicationName() const = 0;
    virtual QString displayName() const = 0;
    virtual QString group() const { return "OTB"; }
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    QString provider() const override { return "otb_tools"; }

protected:
    bool runOtbApplication(const QStringList &args, QgsProcessingFeedback *feedback);
};
```

- [ ] **Step 2: Create otb_tool_wrapper.cpp**

```cpp
// src/processing/providers/otb_tools/otb_tool_wrapper.cpp
#include "otb_tool_wrapper.h"
#include "src/processing/tools/tool_path_manager.h"

#include <QgsProcessingContext.h>
#include <QgsProcessingFeedback.h>

QVariantMap OtbToolWrapper::processAlgorithm(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    QString program = ToolPathManager::instance().otbToolPath(applicationName());
    if (program.isEmpty()) {
        feedback->reportError(tr("OTB application '%1' not found. Ensure OTB is installed.").arg(applicationName()));
        return {};
    }

    QStringList args = buildArgs(parameters, context, feedback);
    if (args.isEmpty()) return {};

    feedback->pushInfo(tr("Running: %1 %2").arg(program, args.join(" ")));

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);

    if (!proc.waitForStarted(5000)) {
        feedback->reportError(tr("Failed to start OTB application: %1").arg(proc.errorString()));
        return {};
    }

    while (proc.state() == QProcess::Running) {
        if (feedback->isCanceled()) {
            proc.kill();
            feedback->reportError(tr("OTB application canceled by user."));
            return {};
        }
        proc.waitForReadyRead(100);
        QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            feedback->pushInfo(QString::fromUtf8(output));
        }
    }

    if (proc.exitCode() != 0) {
        feedback->reportError(tr("OTB application failed with exit code %1: %2")
            .arg(proc.exitCode())
            .arg(QString::fromUtf8(proc.readAllStandardError())));
        return {};
    }

    QVariantMap results;
    if (parameters.contains("OUTPUT")) {
        results["OUTPUT"] = parameters.value("OUTPUT");
    }
    return results;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/processing/providers/otb_tools/otb_tool_wrapper.*
git commit -m "feat(processing): add OtbToolWrapper base class"
```

---

### Task 4: Create CMake Download Scripts

**Files:**
- Create: `cmake/DownloadGdalTools.cmake`
- Create: `cmake/DownloadOtbTools.cmake`

- [ ] **Step 1: Create DownloadGdalTools.cmake**

```cmake
# cmake/DownloadGdalTools.cmake
include(FetchContent)

set(GDAL_TOOLS_VERSION "3.8.0" CACHE STRING "GDAL tools version")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GDAL_TOOLS_URL "https://github.com/OSGeo/gdal/releases/download/v${GDAL_TOOLS_VERSION}/gdal-${GDAL_TOOLS_VERSION}-linux-x86_64.tar.gz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(GDAL_TOOLS_URL "https://github.com/OSGeo/gdal/releases/download/v${GDAL_TOOLS_VERSION}/gdal-${GDAL_TOOLS_VERSION}-macosx-arm64.tar.gz")
else()
    message(WARNING "GDAL tools download not supported on this platform")
    return()
endif()

FetchContent_Declare(
    gdal_tools
    URL ${GDAL_TOOLS_URL}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/gdal
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

FetchContent_MakeAvailable(gdal_tools)

install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/gdal/bin/
        DESTINATION tools/gdal
        USE_SOURCE_PERMISSIONS
        FILES_MATCHING
        PATTERN "gdal*"
        PATTERN "ogr*"
        PATTERN "ogr2ogr"
        PATTERN "ogrtindex")
```

- [ ] **Step 2: Create DownloadOtbTools.cmake**

```cmake
# cmake/DownloadOtbTools.cmake
include(FetchContent)

set(OTB_VERSION "9.1.0" CACHE STRING "OTB tools version")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OTB_URL "https://www.orfeo-toolbox.org/packages/OTB-${OTB_VERSION}-Linux64.tar.gz")
else()
    message(WARNING "OTB tools download not supported on this platform")
    return()
endif()

FetchContent_Declare(
    otb_tools
    URL ${OTB_URL}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/tools/otb
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

FetchContent_MakeAvailable(otb_tools)

install(DIRECTORY ${CMAKE_BINARY_DIR}/tools/otb/bin/
        DESTINATION tools/otb
        USE_SOURCE_PERMISSIONS
        FILES_MATCHING
        PATTERN "otbcli_*")
```

- [ ] **Step 3: Commit**

```bash
git add cmake/DownloadGdalTools.cmake cmake/DownloadOtbTools.cmake
git commit -m "build: add CMake scripts for GDAL/OTB tool download"
```

---

## Phase 2: GDAL Tools Provider

### Task 5: Create GdalToolsProvider and Core Tools

**Files:**
- Create: `src/processing/providers/gdal_tools/provider.h`
- Create: `src/processing/providers/gdal_tools/provider.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_translate.h`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_translate.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_warp.h`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_warp.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_info.h`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_info.cpp`

- [ ] **Step 1: Create provider.h**

```cpp
// src/processing/providers/gdal_tools/provider.h
#pragma once

#include <QgsProcessingProvider.h>

class GdalToolsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    GdalToolsProvider();
    QString id() const override { return "gdal_tools"; }
    QString name() const override { return "GDAL Tools"; }
    QIcon icon() const override;
    QgsProcessingProvider *clone() const override;

protected:
    void loadAlgorithms() override;
    bool supportsNonFileBasedOutput() const override { return false; }
};
```

- [ ] **Step 2: Create provider.cpp**

```cpp
// src/processing/providers/gdal_tools/provider.cpp
#include "provider.h"
#include "algorithms/gdal_translate.h"
#include "algorithms/gdal_warp.h"
#include "algorithms/gdal_info.h"

#include <QIcon>

GdalToolsProvider::GdalToolsProvider()
    : QgsProcessingProvider()
{
}

QIcon GdalToolsProvider::icon() const
{
    return QIcon::fromTheme("gdal");
}

QgsProcessingProvider *GdalToolsProvider::clone() const
{
    return new GdalToolsProvider();
}

void GdalToolsProvider::loadAlgorithms()
{
    addAlgorithm(new GdalTranslateAlgorithm());
    addAlgorithm(new GdalWarpAlgorithm());
    addAlgorithm(new GdalInfoAlgorithm());
    // More algorithms added in subsequent tasks
}
```

- [ ] **Step 3: Create gdal_translate.h**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_translate.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalTranslateAlgorithm : public GdalToolWrapper
{
public:
    GdalTranslateAlgorithm() = default;

    QString name() const override { return "gdal_translate"; }
    QString displayName() const override { return "GDAL Translate (Format Conversion)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdal_translate"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
```

- [ ] **Step 4: Create gdal_translate.cpp**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_translate.cpp
#include "gdal_translate.h"

#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterEnum.h>
#include <QgsProcessingParameterString.h>

void GdalTranslateAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    QStringList formats;
    formats << "GTiff" << "HFA" << "ENVI" << "AAIGrid" << "PNG" << "JPEG" << "NetCDF";
    addParameter(new QgsProcessingParameterEnum("FORMAT", "Output format", formats, false, 0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments", QVariant(), false, true));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalTranslateAlgorithm::buildArgs(const QVariantMap &parameters,
                                                QgsProcessingContext &context,
                                                QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-of" << parameters.value("FORMAT").toString();

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    // Input file path
    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPath = inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    // Output file path
    args << parameters.value("OUTPUT").toString();

    return args;
}
```

- [ ] **Step 5: Create gdal_warp.h and gdal_warp.cpp**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_warp.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalWarpAlgorithm : public GdalToolWrapper
{
public:
    GdalWarpAlgorithm() = default;

    QString name() const override { return "gdalwarp"; }
    QString displayName() const override { return "GDAL Warp (Reproject/Clip)"; }
    QString group() const override { return "Raster Transformation"; }
    QString toolName() const override { return "gdalwarp"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_warp.cpp
#include "gdal_warp.h"

#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterCrs.h>
#include <QgsProcessingParameterExtent.h>
#include <QgsProcessingParameterString.h>

void GdalWarpAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addCrsParameter("TARGET_CRS", "Target CRS (leave empty to skip reprojection)");
    addExtentParameter("EXTENT");
    addParameter(new QgsProcessingParameterString("EXTRA", "Additional arguments", QVariant(), false, true));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalWarpAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.contains("TARGET_CRS") && !parameters.value("TARGET_CRS").toString().isEmpty()) {
        args << "-t_srs" << parameters.value("TARGET_CRS").toString();
    }

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
        QStringList extent = parameters.value("EXTENT").toString().split(",");
        if (extent.size() == 4) {
            args << "-te" << extent[0] << extent[1] << extent[2] << extent[3];
        }
    }

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPath = inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;
    args << parameters.value("OUTPUT").toString();

    return args;
}
```

- [ ] **Step 6: Create gdal_info.h and gdal_info.cpp**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_info.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalInfoAlgorithm : public GdalToolWrapper
{
public:
    GdalInfoAlgorithm() = default;

    QString name() const override { return "gdalinfo"; }
    QString displayName() const override { return "GDAL Info (Raster Information)"; }
    QString group() const override { return "Raster Information"; }
    QString toolName() const override { return "gdalinfo"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_info.cpp
#include "gdal_info.h"

#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterFileDestination.h>

void GdalInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addParameter(new QgsProcessingParameterFileDestination("OUTPUT", "Info output (text file)",
                                                           "Text files (*.txt)"));
}

QStringList GdalInfoAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPath = inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    return args;
}
```

- [ ] **Step 7: Commit**

```bash
git add src/processing/providers/gdal_tools/
git commit -m "feat(processing): add GdalToolsProvider with translate, warp, info"
```

---

### Task 6: Add Remaining GDAL Tools

**Files:**
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_dem.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_contour.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_polygonize.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_merge.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_calc.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdal_retile.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdalbuildvrt.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdaltindex.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/gdalmanage.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/ogr2ogr.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/ogrinfo.h/.cpp`
- Create: `src/processing/providers/gdal_tools/algorithms/ogrtindex.h/.cpp`

- [ ] **Step 1: Create gdal_dem (terrain analysis)**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_dem.h
#pragma once
#include "../gdal_tool_wrapper.h"

class GdalDemAlgorithm : public GdalToolWrapper
{
public:
    GdalDemAlgorithm() = default;
    QString name() const override { return "gdaldem"; }
    QString displayName() const override { return "GDAL DEM (Terrain Analysis)"; }
    QString group() const override { return "Raster Analysis"; }
    QString toolName() const override { return "gdaldem"; }
    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;
protected:
    QStringList buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_dem.cpp
#include "gdal_dem.h"
#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterEnum.h>

void GdalDemAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input DEM raster");
    QStringList modes;
    modes << "hillshade" << "slope" << "aspect" << "TRI" << "TPI" << "roughness";
    addParameter(new QgsProcessingParameterEnum("MODE", "DEM processing mode", modes, false, 0));
    addOutputRasterLayerParameter("OUTPUT", "Output raster");
}

QStringList GdalDemAlgorithm::buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context); Q_UNUSED(feedback);
    QStringList args;
    QStringList modes = {"hillshade", "slope", "aspect", "TRI", "TPI", "roughness"};
    args << modes.value(parameters.value("MODE").toInt());
    QVariant inputVar = parameters.value("INPUT");
    args << (inputVar.canConvert<QgsRasterLayer *>() ? inputVar.value<QgsRasterLayer *>()->source() : inputVar.toString());
    args << parameters.value("OUTPUT").toString();
    return args;
}
```

- [ ] **Step 2: Create gdal_contour (raster to contour lines)**

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_contour.h
#pragma once
#include "../gdal_tool_wrapper.h"

class GdalContourAlgorithm : public GdalToolWrapper
{
public:
    GdalContourAlgorithm() = default;
    QString name() const override { return "gdal_contour"; }
    QString displayName() const override { return "GDAL Contour (Raster to Lines)"; }
    QString group() const override { return "Raster to Vector"; }
    QString toolName() const override { return "gdal_contour"; }
    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;
protected:
    QStringList buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/gdal_tools/algorithms/gdal_contour.cpp
#include "gdal_contour.h"
#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterVectorDestination.h>
#include <QgsProcessingParameterNumber.h>

void GdalContourAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster (DEM)");
    addParameter(new QgsProcessingParameterNumber("INTERVAL", "Contour interval", QgsProcessingParameterNumber::Double, 10.0, false, 0.0));
    addOutputVectorLayerParameter("OUTPUT", "Output contour lines");
}

QStringList GdalContourAlgorithm::buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context); Q_UNUSED(feedback);
    QStringList args;
    args << "-a" << "elevation" << "-i" << QString::number(parameters.value("INTERVAL").toDouble());
    QVariant inputVar = parameters.value("INPUT");
    args << (inputVar.canConvert<QgsRasterLayer *>() ? inputVar.value<QgsRasterLayer *>()->source() : inputVar.toString());
    args << parameters.value("OUTPUT").toString();
    return args;
}
```

- [ ] **Step 3: Create gdal_polygonize, gdal_merge, gdal_calc, gdal_retile, gdalbuildvrt, gdaltindex, gdalmanage, ogr2ogr, ogrinfo, ogrtindex**

For each of these tools, follow the same pattern:
1. Create header with class inheriting `GdalToolWrapper`
2. Implement `initAlgorithm()` with appropriate parameters
3. Implement `buildArgs()` to construct CLI arguments
4. Register in `GdalToolsProvider::loadAlgorithms()`

- [ ] **Step 4: Update provider.cpp to register all algorithms**

```cpp
void GdalToolsProvider::loadAlgorithms()
{
    addAlgorithm(new GdalTranslateAlgorithm());
    addAlgorithm(new GdalWarpAlgorithm());
    addAlgorithm(new GdalInfoAlgorithm());
    addAlgorithm(new GdalDemAlgorithm());
    addAlgorithm(new GdalContourAlgorithm());
    addAlgorithm(new GdalPolygonizeAlgorithm());
    addAlgorithm(new GdalMergeAlgorithm());
    addAlgorithm(new GdalCalcAlgorithm());
    addAlgorithm(new GdalRetileAlgorithm());
    addAlgorithm(new GdalbuildvrtAlgorithm());
    addAlgorithm(new GdaltindexAlgorithm());
    addAlgorithm(new GdalmanageAlgorithm());
    addAlgorithm(new Ogr2ogrAlgorithm());
    addAlgorithm(new OgrinfoAlgorithm());
    addAlgorithm(new OgrtindexAlgorithm());
}
```

- [ ] **Step 5: Commit**

```bash
git add src/processing/providers/gdal_tools/
git commit -m "feat(processing): add remaining GDAL tools (15 total)"
```

---

## Phase 3: OTB Tools Provider

### Task 7: Create OtbToolsProvider and Core Tools

**Files:**
- Create: `src/processing/providers/otb_tools/provider.h/.cpp`
- Create: `src/processing/providers/otb_tools/algorithms/otb_band_math.h/.cpp`
- Create: `src/processing/providers/otb_tools/algorithms/otb_segmentation.h/.cpp`
- Create: `src/processing/providers/otb_tools/algorithms/otb_extract_roi.h/.cpp`

- [ ] **Step 1: Create provider.h**

```cpp
// src/processing/providers/otb_tools/provider.h
#pragma once

#include <QgsProcessingProvider.h>

class OtbToolsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    OtbToolsProvider();
    QString id() const override { return "otb_tools"; }
    QString name() const override { return "OTB Tools"; }
    QIcon icon() const override;
    QgsProcessingProvider *clone() const override;

protected:
    void loadAlgorithms() override;
};
```

- [ ] **Step 2: Create provider.cpp**

```cpp
// src/processing/providers/otb_tools/provider.cpp
#include "provider.h"
#include "algorithms/otb_band_math.h"
#include "algorithms/otb_segmentation.h"
#include "algorithms/otb_extract_roi.h"

#include <QIcon>

OtbToolsProvider::OtbToolsProvider() = default;

QIcon OtbToolsProvider::icon() const { return QIcon::fromTheme("otb"); }
QgsProcessingProvider *OtbToolsProvider::clone() const { return new OtbToolsProvider(); }

void OtbToolsProvider::loadAlgorithms()
{
    addAlgorithm(new OtbBandMathAlgorithm());
    addAlgorithm(new OtbSegmentationAlgorithm());
    addAlgorithm(new OtbExtractRoiAlgorithm());
}
```

- [ ] **Step 3: Create otb_band_math**

```cpp
// src/processing/providers/otb_tools/algorithms/otb_band_math.h
#pragma once
#include "../otb_tool_wrapper.h"

class OtbBandMathAlgorithm : public OtbToolWrapper
{
public:
    OtbBandMathAlgorithm() = default;
    QString name() const override { return "otb_band_math"; }
    QString displayName() const override { return "Band Math (Mathematical Expression)"; }
    QString group() const override { return "Radiometry"; }
    QString applicationName() const override { return "BandMath"; }
    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;
protected:
    QStringList buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/otb_tools/algorithms/otb_band_math.cpp
#include "otb_band_math.h"
#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterString.h>

void OtbBandMathAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster (multi-band)");
    addParameter(new QgsProcessingParameterString("EXPRESSION", "Mathematical expression (e.g., (b1-b2)/(b1+b2))", QVariant(), false, false));
    addOutputRasterLayerParameter("OUTPUT", "Output raster");
}

QStringList OtbBandMathAlgorithm::buildArgs(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context); Q_UNUSED(feedback);
    QStringList args;
    QVariant inputVar = parameters.value("INPUT");
    QString inputPath = inputVar.canConvert<QgsRasterLayer *>() ? inputVar.value<QgsRasterLayer *>()->source() : inputVar.toString();
    args << "-in" << inputPath;
    args << "-exp" << parameters.value("EXPRESSION").toString();
    args << "-out" << parameters.value("OUTPUT").toString();
    return args;
}
```

- [ ] **Step 4: Create otb_segmentation and otb_extract_roi (same pattern)**

- [ ] **Step 5: Commit**

```bash
git add src/processing/providers/otb_tools/
git commit -m "feat(processing): add OtbToolsProvider with BandMath, Segmentation, ExtractROI"
```

---

### Task 8: Add Remaining OTB Tools

Follow the same pattern for each OTB application:
- otb_concatenate_images, otb_dynamic_convert, otb_rescale, otb_convert
- otb_mean_shift_smoothing, otb_lsms
- otb_train_vector_classifier, otb_image_classifier, otb_kmeans_classification
- otb_feature_extraction, otb_haralick_texture, otb_radiometric_indices
- otb_ortho_rectification, otb_bundle_to_perfect_sensor, otb_superimpose
- otb_binary_morphological

Register all in `OtbToolsProvider::loadAlgorithms()`.

- [ ] **Step 1-16: Create each OTB algorithm following the BandMath pattern**
- [ ] **Step 17: Update provider.cpp to register all 19 algorithms**
- [ ] **Step 18: Commit**

```bash
git add src/processing/providers/otb_tools/
git commit -m "feat(processing): add remaining OTB tools (19 total)"
```

---

## Phase 4: QGIS Algorithms Provider

### Task 9: Create QgisAlgorithmsProvider and Raster Algorithms

**Files:**
- Create: `src/processing/providers/qgis_algorithms/provider.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_resample.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_clip.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/raster/raster_statistics.h/.cpp`

- [ ] **Step 1: Create provider.h**

```cpp
// src/processing/providers/qgis_algorithms/provider.h
#pragma once

#include <QgsProcessingProvider.h>

class QgisAlgorithmsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    QgisAlgorithmsProvider();
    QString id() const override { return "qgis_algorithms"; }
    QString name() const override { return "QGIS Basic Algorithms"; }
    QIcon icon() const override;
    QgsProcessingProvider *clone() const override;

protected:
    void loadAlgorithms() override;
};
```

- [ ] **Step 2: Create raster_calculator (native QgsRasterCalculator)**

```cpp
// src/processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.h
#pragma once

#include <QgsProcessingAlgorithm.h>

class RasterCalculatorAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterCalculatorAlgorithm() = default;

    QString name() const override { return "raster_calculator"; }
    QString displayName() const override { return "Raster Calculator"; }
    QString group() const override { return "Raster"; }
    QString provider() const override { return "qgis_algorithms"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;
    QVariantMap processAlgorithm(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.cpp
#include "raster_calculator.h"

#include <QgsProcessingParameterRasterLayer.h>
#include <QgsProcessingParameterRasterDestination.h>
#include <QgsProcessingParameterString.h>
#include <QgsRasterCalculator.h>
#include <QgsRasterLayer.h>
#include <QgsRasterPipe.h>

void RasterCalculatorAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterMultipleLayers("INPUT_LAYERS", "Input raster layers",
                                                         QgsProcessingParameterRasterLayer));
    addParameter(new QgsProcessingParameterString("EXPRESSION", "Calculation expression (e.g., A + B)",
                                                   QVariant(), false, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QVariantMap RasterCalculatorAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                         QgsProcessingContext &context,
                                                         QgsProcessingFeedback *feedback)
{
    QList<QVariant> layerVars = parameters.value("INPUT_LAYERS").toList();
    QString expression = parameters.value("EXPRESSION").toString();
    QString outputPath = parameters.value("OUTPUT").toString();

    if (layerVars.isEmpty()) {
        feedback->reportError(tr("No input layers provided."));
        return {};
    }

    // Build raster layer references for expression
    QList<QgsRasterCalculatorEntry> entries;
    char bandName = 'A';
    for (const QVariant &var : layerVars) {
        QgsRasterLayer *layer = qobject_cast<QgsRasterLayer *>(qvariant_cast<QgsMapLayer *>(var));
        if (!layer) continue;
        QgsRasterCalculatorEntry entry;
        entry.raster = layer;
        entry.bandNumber = 1;
        entry.ref = QString(bandName);
        entries.append(entry);
        bandName++;
    }

    QgsRasterLayer *firstLayer = qobject_cast<QgsRasterLayer *>(qvariant_cast<QgsMapLayer *>(layerVars.first()));
    if (!firstLayer) {
        feedback->reportError(tr("Invalid input layer."));
        return {};
    }

    QgsRasterCalculator calc(expression, outputPath, "GTiff",
                              firstLayer->extent(), firstLayer->width(), firstLayer->height(),
                              entries);

    QgsRasterCalculator::Result result = calc.processCalculation(feedback);
    if (result != QgsRasterCalculator::Success) {
        feedback->reportError(tr("Raster calculation failed with code: %1").arg(static_cast<int>(result)));
        return {};
    }

    return {{"OUTPUT", outputPath}};
}
```

- [ ] **Step 3: Create raster_resample, raster_clip, raster_merge_bands, raster_ndvi, raster_statistics**

Each follows the pattern:
1. Header with class inheriting `QgsProcessingAlgorithm`
2. `initAlgorithm()` with appropriate parameters
3. `processAlgorithm()` using QGIS C++ API (QgsRasterCalculator, QgsRasterFileWriter, etc.)

- [ ] **Step 4: Commit**

```bash
git add src/processing/providers/qgis_algorithms/
git commit -m "feat(processing): add QgisAlgorithmsProvider with raster algorithms"
```

---

### Task 10: Add QGIS Vector Algorithms

**Files:**
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_buffer.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_clip.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_merge.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_spatial_query.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_attribute_query.h/.cpp`
- Create: `src/processing/providers/qgis_algorithms/algorithms/vector/vector_reproject.h/.cpp`

- [ ] **Step 1: Create vector_buffer**

```cpp
// src/processing/providers/qgis_algorithms/algorithms/vector/vector_buffer.h
#pragma once

#include <QgsProcessingAlgorithm.h>

class VectorBufferAlgorithm : public QgsProcessingAlgorithm
{
public:
    VectorBufferAlgorithm() = default;

    QString name() const override { return "vector_buffer"; }
    QString displayName() const override { return "Buffer"; }
    QString group() const override { return "Vector Geometry"; }
    QString provider() const override { return "qgis_algorithms"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;
    QVariantMap processAlgorithm(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override;
};
```

```cpp
// src/processing/providers/qgis_algorithms/algorithms/vector/vector_buffer.cpp
#include "vector_buffer.h"

#include <QgsProcessingParameterVectorLayer.h>
#include <QgsProcessingParameterVectorDestination.h>
#include <QgsProcessingParameterNumber.h>
#include <QgsProcessingParameterEnum.h>
#include <QgsVectorLayer.h>
#include <QgsFeature.h>
#include <QgsGeometry.h>
#include <QgsVectorFileWriter.h>

void VectorBufferAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterVectorLayer("INPUT", "Input vector layer"));
    addParameter(new QgsProcessingParameterNumber("DISTANCE", "Buffer distance",
                                                  QgsProcessingParameterNumber::Double, 100.0, false, 0.0));
    addParameter(new QgsProcessingParameterEnum("CAP_STYLE", "Cap style",
                                                QStringList() << "Round" << "Flat" << "Square", false, 0));
    addParameter(new QgsProcessingParameterNumber("SEGMENTS", "Segments",
                                                  QgsProcessingParameterNumber::Integer, 25, false, 1));
    addParameter(new QgsProcessingParameterVectorDestination("OUTPUT", "Output buffered layer"));
}

QVariantMap VectorBufferAlgorithm::processAlgorithm(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    QgsVectorLayer *inputLayer = qvariant_cast<QgsVectorLayer *>(parameters.value("INPUT"));
    double distance = parameters.value("DISTANCE").toDouble();
    int capStyle = parameters.value("CAP_STYLE").toInt();
    int segments = parameters.value("SEGMENTS").toInt();
    QString outputPath = parameters.value("OUTPUT").toString();

    if (!inputLayer || !inputLayer->isValid()) {
        feedback->reportError(tr("Invalid input layer."));
        return {};
    }

    QgsVectorLayer *outputLayer = inputLayer->clone();
    outputLayer->startEditing();

    int featureCount = inputLayer->featureCount();
    int current = 0;

    QgsFeatureIterator it = inputLayer->getFeatures();
    QgsFeature feature;
    while (it.nextFeature(feature)) {
        if (feedback->isCanceled()) break;

        QgsGeometry geom = feature.geometry();
        QgsGeometry buffered = geom.buffer(distance, segments);

        QgsFeature outFeature = feature;
        outFeature.setGeometry(buffered);
        outputLayer->addFeature(outFeature);

        current++;
        if (current % 100 == 0) {
            feedback->setProgress(100.0 * current / featureCount);
        }
    }

    outputLayer->commitChanges();

    // Write to file
    QgsVectorFileWriter::WriterError error = QgsVectorFileWriter::writeAsVectorFormat(
        *outputLayer, outputPath, "UTF-8", outputLayer->crs(), "GPKG");

    delete outputLayer;

    if (error != QgsVectorFileWriter::NoError) {
        feedback->reportError(tr("Failed to write output layer."));
        return {};
    }

    return {{"OUTPUT", outputPath}};
}
```

- [ ] **Step 2: Create vector_clip, vector_dissolve, vector_merge, vector_spatial_query, vector_attribute_query, vector_reproject**

Each follows the same pattern with appropriate geometry operations.

- [ ] **Step 3: Update provider.cpp to register all 13 algorithms (6 raster + 7 vector)**

- [ ] **Step 4: Commit**

```bash
git add src/processing/providers/qgis_algorithms/
git commit -m "feat(processing): add QGIS vector algorithms (7 total)"
```

---

## Phase 5: Integration

### Task 11: Refactor SicnuNative Provider

**Files:**
- Create: `src/processing/providers/sicnu_native/provider.h`
- Create: `src/processing/providers/sicnu_native/provider.cpp`
- Modify: `src/processing/sicnunativealgorithms.cpp` (split into individual files)

- [ ] **Step 1: Create new provider structure**

Move existing algorithm classes from `sicnunativealgorithms.cpp` into individual files under `src/processing/providers/sicnu_native/algorithms/`.

- [ ] **Step 2: Create provider.h**

```cpp
// src/processing/providers/sicnu_native/provider.h
#pragma once

#include <QgsProcessingProvider.h>

class SicnuNativeProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    SicnuNativeProvider();
    QString id() const override { return "sicnu_native"; }
    QString name() const override { return "SICNU Native"; }
    QIcon icon() const override;
    QgsProcessingProvider *clone() const override;

protected:
    void loadAlgorithms() override;
};
```

- [ ] **Step 3: Commit**

```bash
git add src/processing/providers/sicnu_native/
git commit -m "refactor(processing): move SicnuNative to new provider structure"
```

---

### Task 12: Update CMakeLists.txt and Register All Providers

**Files:**
- Modify: `CMakeLists.txt` (add processing library)
- Create: `src/processing/CMakeLists.txt`
- Modify: `src/gui/main_window.cpp` (register all providers)

- [ ] **Step 1: Create src/processing/CMakeLists.txt**

```cmake
# src/processing/CMakeLists.txt
add_library(sicnu_processing SHARED
    tools/tool_path_manager.cpp
    providers/sicnu_native/provider.cpp
    providers/gdal_tools/provider.cpp
    providers/gdal_tools/gdal_tool_wrapper.cpp
    providers/gdal_tools/algorithms/gdal_translate.cpp
    providers/gdal_tools/algorithms/gdal_warp.cpp
    providers/gdal_tools/algorithms/gdal_info.cpp
    # ... all other algorithm .cpp files
    providers/otb_tools/provider.cpp
    providers/otb_tools/otb_tool_wrapper.cpp
    providers/otb_tools/algorithms/otb_band_math.cpp
    # ... all other OTB algorithm .cpp files
    providers/qgis_algorithms/provider.cpp
    providers/qgis_algorithms/algorithms/raster/raster_calculator.cpp
    # ... all other QGIS algorithm .cpp files
)

target_include_directories(sicnu_processing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sicnu_processing PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    qgis_core
    qgis_gui
)
```

- [ ] **Step 2: Update root CMakeLists.txt**

```cmake
# Add after add_subdirectory(src/gui):
add_subdirectory(src/processing)
```

- [ ] **Step 3: Update main_window.cpp to register all providers**

```cpp
// In SicnuMainWindow::loadProviders() or initialize():
#include "processing/providers/sicnu_native/provider.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"

void SicnuMainWindow::loadProviders()
{
    auto *registry = QgsApplication::processingRegistry();
    registry->addProvider(new SicnuNativeProvider());
    registry->addProvider(new GdalToolsProvider());
    registry->addProvider(new OtbToolsProvider());
    registry->addProvider(new QgisAlgorithmsProvider());
}
```

- [ ] **Step 4: Commit**

```bash
git add src/processing/CMakeLists.txt CMakeLists.txt src/gui/main_window.cpp
git commit -m "build: integrate processing library and register all providers"
```

---

### Task 13: Build and Test

- [ ] **Step 1: Clean build**

```bash
rm -rf build && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug
```

- [ ] **Step 2: Build**

```bash
cd build && make -j$(nproc)
```

Expected: All libraries and plugins compile successfully.

- [ ] **Step 3: Run and verify toolbox**

```bash
cd build && ./sicnu_geo_rs
```

Expected: Processing Toolbox shows 4 providers with all algorithms grouped correctly.

- [ ] **Step 4: Test GDAL tool (if GDAL bundled)**

Open Processing Toolbox, double-click "GDAL Translate", select a raster, run.

- [ ] **Step 5: Test QGIS algorithm**

Open Processing Toolbox, double-click "Raster Calculator", enter expression, run.

- [ ] **Step 6: Commit final state**

```bash
git add -A
git commit -m "feat(processing): complete processing toolbox expansion (100+ algorithms)"
```
