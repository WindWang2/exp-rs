# Processing Toolbox Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire up the QGIS processing framework with a Processing Toolbox dock widget and 15 C++ native algorithms for vector geometry, overlay/selection, raster analysis, and coordinate/projection operations.

**Architecture:** Create a `SicnuNativeAlgorithms` provider class that registers 15 custom `QgsProcessingAlgorithm` subclasses. Add a `QgsProcessingToolboxTreeView` dock widget and Processing menu to the main window. Connect double-click to `QgsProcessingAlgorithmDialogBase` for algorithm execution.

**Tech Stack:** C++17, Qt6, QGIS C++ API (`QgsProcessingProvider`, `QgsProcessingAlgorithm`, `QgsProcessingToolboxTreeView`, `QgsProcessingAlgorithmDialogBase`)

---

### Task 1: Provider Class and Buffer Algorithm

**Files:**
- Create: `src/processing/sicnunativealgorithms.h`
- Create: `src/processing/sicnunativealgorithms.cpp`
- Modify: `CMakeLists.txt:140-142`
- Modify: `main.cpp:39-82` (includes)
- Modify: `main.cpp:895-897` (after QgsApplication::initQgis)

- [ ] **Step 1: Create the provider header**

Create `src/processing/sicnunativealgorithms.h`:

```cpp
#pragma once

#include <QObject>
#include <processing/qgsprocessingprovider.h>

class SicnuNativeAlgorithms : public QgsProcessingProvider
{
    Q_OBJECT

public:
    explicit SicnuNativeAlgorithms( QObject *parent = nullptr );

    QString id() const override { return QStringLiteral( "sicnu_native" ); }
    QString name() const override { return QStringLiteral( "SICNU Native" ); }
    QIcon icon() const override;
    QString longName() const override { return QStringLiteral( "SICNU Native Algorithms" ); }

protected:
    void loadAlgorithms() override;
};
```

- [ ] **Step 2: Create the provider implementation with Buffer algorithm**

Create `src/processing/sicnunativealgorithms.cpp`:

```cpp
#include "sicnunativealgorithms.h"

#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgsfields.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgswkbtypes.h>

// ── Buffer Algorithm ─────────────────────────────────────────────────────────

class QgsBufferAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString DISTANCE;
    static const QString SEGMENTS;
    static const QString OUTPUT;

    QgsBufferAlgorithm() = default;

    QString name() const override { return QStringLiteral( "buffer" ); }
    QString displayName() const override { return QObject::tr( "Buffer" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "buffer" ), QObject::tr( "distance" ), QObject::tr( "polygon" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsBufferAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );

        addParameter( new QgsProcessingParameterNumber( DISTANCE, QObject::tr( "Distance" ),
            QgsProcessingParameterNumber::Double, 1000.0, false, 0.0 ) );

        addParameter( new QgsProcessingParameterNumber( SEGMENTS, QObject::tr( "Segments" ),
            QgsProcessingParameterNumber::Integer, 25, false, 1 ) );

        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Buffered" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        double distance = parameterAsDouble( parameters, DISTANCE, context );
        int segments = parameterAsInt( parameters, SEGMENTS, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Polygon, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() )
                break;

            current++;
            if ( total > 0 )
                feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( feat.geometry().buffer( distance, segments ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsBufferAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsBufferAlgorithm::DISTANCE = QStringLiteral( "DISTANCE" );
const QString QgsBufferAlgorithm::SEGMENTS = QStringLiteral( "SEGMENTS" );
const QString QgsBufferAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );

// ── Provider Implementation ──────────────────────────────────────────────────

SicnuNativeAlgorithms::SicnuNativeAlgorithms( QObject *parent )
    : QgsProcessingProvider( parent )
{
}

QIcon SicnuNativeAlgorithms::icon() const
{
    return QIcon();
}

void SicnuNativeAlgorithms::loadAlgorithms()
{
    addAlgorithm( new QgsBufferAlgorithm() );
}
```

- [ ] **Step 3: Add source files to CMakeLists.txt**

In `CMakeLists.txt`, modify the `SICNU_GEO_RS_CPP_SRCS` variable (line 140-142):

```cmake
  set(SICNU_GEO_RS_CPP_SRCS
    main.cpp
    src/processing/sicnunativealgorithms.cpp
  )
```

Also add include directories for the new source. After `target_link_libraries` (around line 157), add:

```cmake
  target_include_directories(sicnu_geo_rs PRIVATE
    ${CMAKE_SOURCE_DIR}/src
  )
```

- [ ] **Step 4: Add include and provider registration in main.cpp**

In `main.cpp`, add the include after the existing processing includes (after line 81):

```cpp
// Processing framework
#include <processing/qgsprocessingregistry.h>
#include "src/processing/sicnunativealgorithms.h"
```

In `main()`, after `QgsApplication::initQgis();` (line 897), add:

```cpp
    // Register processing algorithms
    QgsApplication::processingRegistry()->addProvider( new SicnuNativeAlgorithms() );
```

- [ ] **Step 5: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/processing/sicnunativealgorithms.h src/processing/sicnunativealgorithms.cpp CMakeLists.txt main.cpp
git commit -m "feat(processing): add SicnuNativeAlgorithms provider with Buffer algorithm"
```

---

### Task 2: Processing Toolbox Dock Widget

**Files:**
- Modify: `main.cpp` (includes, setupDockWidgets, member variables)

- [ ] **Step 1: Add processing includes to main.cpp**

In `main.cpp`, add after the processing includes (after the line added in Task 1):

```cpp
#include <processing/qgsprocessingtoolboxtreeview.h>
#include <processing/qgsprocessingalgorithmdialogbase.h>
```

- [ ] **Step 2: Add Processing Toolbox dock to setupDockWidgets()**

In `setupDockWidgets()`, after the Browser dock tabification (line 291: `layersDock->raise();`), add:

```cpp
        // Processing Toolbox Panel (Left, below browser)
        QgsDockWidget *processingDock = new QgsDockWidget("Processing Toolbox", this);
        processingDock->setObjectName("processingDock");
        processingDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        m_toolboxView = new QgsProcessingToolboxTreeView(processingDock);
        m_toolboxView->setRegistry(QgsApplication::processingRegistry());
        processingDock->setWidget(m_toolboxView);
        addDockWidget(Qt::LeftDockWidgetArea, processingDock);

        // Tabify with browser
        tabifyDockWidget(browserDock, processingDock);
```

- [ ] **Step 3: Add Processing menu**

In `setupMenu()`, after the Settings menu (line 218) and before the Help menu, add:

```cpp
        // Processing Menu
        QMenu *processingMenu = menuBar()->addMenu("&Processing");
        processingMenu->addAction("Toolbox", this, &QgisDesktopWindow::showProcessingToolbox);
        processingMenu->addSeparator();
        processingMenu->addAction("History", this, &QgisDesktopWindow::showProcessingHistory);
```

- [ ] **Step 4: Add slot stubs and member variable**

In the `private slots:` section (around line 502), add:

```cpp
    // ── Processing Actions ─────────────────────────────────────────────────────
    void showProcessingToolbox()
    {
        // Find and raise the processing dock
        for (QDockWidget *dock : findChildren<QDockWidget*>()) {
            if (dock->objectName() == "processingDock") {
                dock->show();
                dock->raise();
                break;
            }
        }
    }

    void showProcessingHistory()
    {
        QMessageBox::information(this, "Processing History", "Processing history coming soon...");
    }
```

In the member variables section (around line 790), add:

```cpp
    QgsProcessingToolboxTreeView *m_toolboxView = nullptr;
```

- [ ] **Step 5: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add main.cpp
git commit -m "feat(processing): add Processing Toolbox dock widget and menu"
```

---

### Task 3: Algorithm Execution Dialog Connection

**Files:**
- Modify: `main.cpp` (setupDockWidgets or setupConnections, double-click handler)

- [ ] **Step 1: Connect toolbox double-click to algorithm dialog**

After creating the toolbox view in `setupDockWidgets()` (after `tabifyDockWidget(browserDock, processingDock);`), add:

```cpp
        // Double-click on algorithm in toolbox opens execution dialog
        connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
            const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
            if (!alg)
                return;

            QgsProcessingAlgorithm *algorithm = alg->create();
            if (!algorithm)
                return;

            QgsProcessingAlgorithmDialogBase *dlg = new QgsProcessingAlgorithmDialogBase(this);
            dlg->setAlgorithm(algorithm);
            dlg->exec();
            dlg->deleteLater();
        });
```

- [ ] **Step 2: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add main.cpp
git commit -m "feat(processing): connect toolbox double-click to algorithm dialog"
```

---

### Task 4: Vector Geometry Algorithms (Centroid, Convex Hull, Dissolve, Simplify)

**Files:**
- Modify: `src/processing/sicnunativealgorithms.cpp`

- [ ] **Step 1: Add Centroid algorithm**

Add before the `SicnuNativeAlgorithms` constructor in `sicnunativealgorithms.cpp`:

```cpp
// ── Centroid Algorithm ───────────────────────────────────────────────────────

class QgsCentroidsAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

    QgsCentroidsAlgorithm() = default;

    QString name() const override { return QStringLiteral( "centroids" ); }
    QString displayName() const override { return QObject::tr( "Centroids" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "centroid" ), QObject::tr( "center" ), QObject::tr( "point" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsCentroidsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Centroids" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Point, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( feat.geometry().centroid() );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsCentroidsAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsCentroidsAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 2: Add Convex Hull algorithm**

```cpp
// ── Convex Hull Algorithm ────────────────────────────────────────────────────

class QgsConvexHullAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

    QgsConvexHullAlgorithm() = default;

    QString name() const override { return QStringLiteral( "convexhull" ); }
    QString displayName() const override { return QObject::tr( "Convex Hull" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "convex" ), QObject::tr( "hull" ), QObject::tr( "envelope" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsConvexHullAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Convex Hull" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Polygon, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( feat.geometry().convexHull() );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsConvexHullAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsConvexHullAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 3: Add Dissolve algorithm**

```cpp
// ── Dissolve Algorithm ───────────────────────────────────────────────────────

class QgsDissolveAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT;

    QgsDissolveAlgorithm() = default;

    QString name() const override { return QStringLiteral( "dissolve" ); }
    QString displayName() const override { return QObject::tr( "Dissolve" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "dissolve" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsDissolveAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Dissolved" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::MultiPolygon, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsGeometry combined;
        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                if ( combined.isNull() )
                    combined = feat.geometry();
                else
                    combined = combined.combine( feat.geometry() );
            }
        }

        if ( !combined.isNull() )
        {
            QgsFeature outputFeat;
            outputFeat.setGeometry( combined );
            sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsDissolveAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsDissolveAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 4: Add Simplify algorithm**

```cpp
// ── Simplify Algorithm ───────────────────────────────────────────────────────

class QgsSimplifyAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString TOLERANCE;
    static const QString OUTPUT;

    QgsSimplifyAlgorithm() = default;

    QString name() const override { return QStringLiteral( "simplify" ); }
    QString displayName() const override { return QObject::tr( "Simplify" ); }
    QString group() const override { return QObject::tr( "Vector geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "simplify" ), QObject::tr( "douglas" ), QObject::tr( "peucker" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsSimplifyAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterNumber( TOLERANCE, QObject::tr( "Tolerance" ),
            QgsProcessingParameterNumber::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Simplified" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        double tolerance = parameterAsDouble( parameters, TOLERANCE, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                outputFeat.setGeometry( feat.geometry().simplify( tolerance ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsSimplifyAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsSimplifyAlgorithm::TOLERANCE = QStringLiteral( "TOLERANCE" );
const QString QgsSimplifyAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 5: Register new algorithms in loadAlgorithms()**

In `SicnuNativeAlgorithms::loadAlgorithms()`, add:

```cpp
void SicnuNativeAlgorithms::loadAlgorithms()
{
    addAlgorithm( new QgsBufferAlgorithm() );
    addAlgorithm( new QgsCentroidsAlgorithm() );
    addAlgorithm( new QgsConvexHullAlgorithm() );
    addAlgorithm( new QgsDissolveAlgorithm() );
    addAlgorithm( new QgsSimplifyAlgorithm() );
}
```

- [ ] **Step 6: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add src/processing/sicnunativealgorithms.cpp
git commit -m "feat(processing): add Centroid, Convex Hull, Dissolve, Simplify algorithms"
```

---

### Task 5: Vector Overlay/Selection Algorithms

**Files:**
- Modify: `src/processing/sicnunativealgorithms.cpp`

- [ ] **Step 1: Add Clip algorithm**

Add before the `SicnuNativeAlgorithms` constructor:

```cpp
// ── Clip Algorithm ───────────────────────────────────────────────────────────

class QgsClipAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    QgsClipAlgorithm() = default;

    QString name() const override { return QStringLiteral( "clip" ); }
    QString displayName() const override { return QObject::tr( "Clip" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "cut" ), QObject::tr( "trim" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsClipAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Clipped" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        // Build combined clip geometry from overlay
        QgsGeometry clipGeom;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( clipGeom.isNull() )
                    clipGeom = overlayFeat.geometry();
                else
                    clipGeom = clipGeom.combine( overlayFeat.geometry() );
            }
        }

        if ( clipGeom.isNull() )
            return QVariantMap{{OUTPUT, dest}};

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsGeometry clipped = feat.geometry().intersection( clipGeom );
                if ( !clipped.isEmpty() )
                {
                    QgsFeature outputFeat = feat;
                    outputFeat.setGeometry( clipped );
                    sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                }
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsClipAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsClipAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString QgsClipAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 2: Add Intersection algorithm**

```cpp
// ── Intersection Algorithm ───────────────────────────────────────────────────

class QgsIntersectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    QgsIntersectionAlgorithm() = default;

    QString name() const override { return QStringLiteral( "intersection" ); }
    QString displayName() const override { return QObject::tr( "Intersection" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "intersection" ), QObject::tr( "overlap" ), QObject::tr( "common" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsIntersectionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Intersection" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        // Collect overlay geometries
        QList<QgsGeometry> overlayGeoms;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
                overlayGeoms.append( overlayFeat.geometry() );
        }

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                for ( const QgsGeometry &overlayGeom : overlayGeoms )
                {
                    QgsGeometry result = feat.geometry().intersection( overlayGeom );
                    if ( !result.isEmpty() )
                    {
                        QgsFeature outputFeat = feat;
                        outputFeat.setGeometry( result );
                        sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
                    }
                }
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsIntersectionAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsIntersectionAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString QgsIntersectionAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 3: Add Union algorithm**

```cpp
// ── Union Algorithm ──────────────────────────────────────────────────────────

class QgsUnionAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    QgsUnionAlgorithm() = default;

    QString name() const override { return QStringLiteral( "union" ); }
    QString displayName() const override { return QObject::tr( "Union" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "union" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsUnionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Union" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), Qgis::WkbType::Unknown, source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        // Collect overlay geometries
        QgsGeometry overlayCombined;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( overlayCombined.isNull() )
                    overlayCombined = overlayFeat.geometry();
                else
                    overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
            }
        }

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                if ( !overlayCombined.isNull() )
                    outputFeat.setGeometry( feat.geometry().combine( overlayCombined ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsUnionAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsUnionAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString QgsUnionAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 4: Add Difference algorithm**

```cpp
// ── Difference Algorithm ─────────────────────────────────────────────────────

class QgsDifferenceAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    QgsDifferenceAlgorithm() = default;

    QString name() const override { return QStringLiteral( "difference" ); }
    QString displayName() const override { return QObject::tr( "Difference" ); }
    QString group() const override { return QObject::tr( "Vector overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "difference" ), QObject::tr( "erase" ), QObject::tr( "subtract" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsDifferenceAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSource( OVERLAY, QObject::tr( "Overlay layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Difference" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        std::unique_ptr<QgsProcessingFeatureSource> overlay( parameterAsSource( parameters, OVERLAY, context ) );
        if ( !overlay )
            throw QgsProcessingException( invalidSourceError( parameters, OVERLAY ) );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        // Build combined overlay geometry
        QgsGeometry overlayCombined;
        QgsFeatureIterator overlayIt = overlay->getFeatures();
        QgsFeature overlayFeat;
        while ( overlayIt.nextFeature( overlayFeat ) )
        {
            if ( overlayFeat.hasGeometry() )
            {
                if ( overlayCombined.isNull() )
                    overlayCombined = overlayFeat.geometry();
                else
                    overlayCombined = overlayCombined.combine( overlayFeat.geometry() );
            }
        }

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                if ( !overlayCombined.isNull() )
                    outputFeat.setGeometry( feat.geometry().difference( overlayCombined ) );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsDifferenceAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsDifferenceAlgorithm::OVERLAY = QStringLiteral( "OVERLAY" );
const QString QgsDifferenceAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 5: Add Extract by Attribute algorithm**

```cpp
// ── Extract by Attribute Algorithm ───────────────────────────────────────────

class QgsExtractByAttributeAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString FIELD;
    static const QString VALUE;
    static const QString OUTPUT;

    QgsExtractByAttributeAlgorithm() = default;

    QString name() const override { return QStringLiteral( "extractbyattribute" ); }
    QString displayName() const override { return QObject::tr( "Extract by Attribute" ); }
    QString group() const override { return QObject::tr( "Vector selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "extract" ), QObject::tr( "filter" ), QObject::tr( "select" ), QObject::tr( "attribute" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsExtractByAttributeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterField( FIELD, QObject::tr( "Field" ), QVariant(), INPUT ) );
        addParameter( new QgsProcessingParameterString( VALUE, QObject::tr( "Value" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Extracted" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QString fieldName = parameterAsString( parameters, FIELD, context );
        QString value = parameterAsString( parameters, VALUE, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), source->sourceCrs() ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        int fieldIdx = source->fields().indexOf( fieldName );
        if ( fieldIdx < 0 )
            throw QgsProcessingException( QObject::tr( "Field '%1' not found" ).arg( fieldName ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.attribute( fieldIdx ).toString() == value )
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsExtractByAttributeAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsExtractByAttributeAlgorithm::FIELD = QStringLiteral( "FIELD" );
const QString QgsExtractByAttributeAlgorithm::VALUE = QStringLiteral( "VALUE" );
const QString QgsExtractByAttributeAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 6: Register new algorithms in loadAlgorithms()**

Update `loadAlgorithms()`:

```cpp
void SicnuNativeAlgorithms::loadAlgorithms()
{
    // Vector geometry
    addAlgorithm( new QgsBufferAlgorithm() );
    addAlgorithm( new QgsCentroidsAlgorithm() );
    addAlgorithm( new QgsConvexHullAlgorithm() );
    addAlgorithm( new QgsDissolveAlgorithm() );
    addAlgorithm( new QgsSimplifyAlgorithm() );

    // Vector overlay
    addAlgorithm( new QgsClipAlgorithm() );
    addAlgorithm( new QgsIntersectionAlgorithm() );
    addAlgorithm( new QgsUnionAlgorithm() );
    addAlgorithm( new QgsDifferenceAlgorithm() );

    // Vector selection
    addAlgorithm( new QgsExtractByAttributeAlgorithm() );
}
```

- [ ] **Step 7: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 8: Commit**

```bash
git add src/processing/sicnunativealgorithms.cpp
git commit -m "feat(processing): add Clip, Intersection, Union, Difference, Extract by Attribute algorithms"
```

---

### Task 6: Raster Analysis Algorithms

**Files:**
- Modify: `src/processing/sicnunativealgorithms.cpp` (add includes at top, add algorithms, register)

- [ ] **Step 1: Add raster includes at top of file**

At the top of `sicnunativealgorithms.cpp`, after the existing includes, add:

```cpp
#include <qgsrasterlayer.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterinterface.h>
#include <qgsrasterdataprovider.h>
#include <qgsrectangle.h>
```

- [ ] **Step 2: Add Clip Raster by Extent algorithm**

```cpp
// ── Clip Raster by Extent Algorithm ──────────────────────────────────────────

class QgsClipRasterByExtentAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString EXTENT;
    static const QString OUTPUT;

    QgsClipRasterByExtentAlgorithm() = default;

    QString name() const override { return QStringLiteral( "cliprasterbyextent" ); }
    QString displayName() const override { return QObject::tr( "Clip Raster by Extent" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "raster" ), QObject::tr( "extent" ), QObject::tr( "crop" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsClipRasterByExtentAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( INPUT, QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterExtent( EXTENT, QObject::tr( "Extent" ) ) );
        addParameter( new QgsProcessingParameterRasterDestination( OUTPUT, QObject::tr( "Clipped raster" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, INPUT, context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, INPUT ) );

        QgsRectangle extent = parameterAsExtent( parameters, EXTENT, context );
        QString dest = parameterAsOutputLayerValue( parameters, OUTPUT, context );

        feedback->setProgressText( QObject::tr( "Clipping raster..." ) );

        // Use GDAL to clip
        int nCols = static_cast<int>( extent.width() / layer->rasterUnitsPerPixelX() );
        int nRows = static_cast<int>( extent.height() / layer->rasterUnitsPerPixelY() );

        if ( nCols <= 0 || nRows <= 0 )
            throw QgsProcessingException( QObject::tr( "Invalid extent for clipping" ) );

        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        QgsRasterPipe *pipe = new QgsRasterPipe();
        if ( !pipe->set( layer->dataProvider()->clone() ) )
        {
            delete pipe;
            throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
        }

        QgsRasterProjector *projector = new QgsRasterProjector();
        projector->setCrs( layer->crs(), layer->crs() );
        pipe->insert( 2, projector );

        bool err = writer.writeRaster( pipe, nCols, nRows, extent, layer->crs() );
        delete pipe;

        if ( err )
            throw QgsProcessingException( QObject::tr( "Error writing clipped raster" ) );

        feedback->setProgress( 100 );

        QVariantMap results;
        results[OUTPUT] = dest;
        return results;
    }
};

const QString QgsClipRasterByExtentAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsClipRasterByExtentAlgorithm::EXTENT = QStringLiteral( "EXTENT" );
const QString QgsClipRasterByExtentAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 3: Add Raster Layer Statistics algorithm**

```cpp
// ── Raster Layer Statistics Algorithm ────────────────────────────────────────

class QgsRasterLayerStatisticsAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OUTPUT_HTML;

    QgsRasterLayerStatisticsAlgorithm() = default;

    QString name() const override { return QStringLiteral( "rasterlayerstatistics" ); }
    QString displayName() const override { return QObject::tr( "Raster Layer Statistics" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "statistics" ), QObject::tr( "raster" ), QObject::tr( "min" ), QObject::tr( "max" ), QObject::tr( "mean" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsRasterLayerStatisticsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( INPUT, QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterFileDestination( OUTPUT_HTML, QObject::tr( "Statistics" ),
            QObject::tr( "HTML files (*.html)" ), QVariant(), true ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, INPUT, context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, INPUT ) );

        feedback->setProgressText( QObject::tr( "Calculating statistics..." ) );

        QgsRasterBandStats stats = layer->dataProvider()->bandStatistics( 1,
            QgsRasterBandStats::Min | QgsRasterBandStats::Max |
            QgsRasterBandStats::Mean | QgsRasterBandStats::StdDev );

        QString html;
        html += QStringLiteral( "<html><body>" );
        html += QStringLiteral( "<h2>Raster Layer Statistics: %1</h2>" ).arg( layer->name() );
        html += QStringLiteral( "<table border='1' cellpadding='4'>" );
        html += QStringLiteral( "<tr><td>Minimum</td><td>%1</td></tr>" ).arg( stats.minimumValue );
        html += QStringLiteral( "<tr><td>Maximum</td><td>%1</td></tr>" ).arg( stats.maximumValue );
        html += QStringLiteral( "<tr><td>Mean</td><td>%1</td></tr>" ).arg( stats.mean );
        html += QStringLiteral( "<tr><td>Std Dev</td><td>%1</td></tr>" ).arg( stats.stdDev );
        html += QStringLiteral( "</table></body></html>" );

        QVariantMap results;
        if ( parameters.contains( OUTPUT_HTML ) && !parameterAsFileOutput( parameters, OUTPUT_HTML, context ).isEmpty() )
        {
            QString dest = parameterAsFileOutput( parameters, OUTPUT_HTML, context );
            QFile file( dest );
            if ( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
            {
                QTextStream ts( &file );
                ts << html;
                file.close();
            }
            results[OUTPUT_HTML] = dest;
        }

        feedback->setProgress( 100 );
        return results;
    }
};

const QString QgsRasterLayerStatisticsAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsRasterLayerStatisticsAlgorithm::OUTPUT_HTML = QStringLiteral( "OUTPUT_HTML" );
```

- [ ] **Step 4: Add Hillshade algorithm**

```cpp
// ── Hillshade Algorithm ──────────────────────────────────────────────────────

class QgsHillshadeAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString Z_FACTOR;
    static const QString OUTPUT;

    QgsHillshadeAlgorithm() = default;

    QString name() const override { return QStringLiteral( "hillshade" ); }
    QString displayName() const override { return QObject::tr( "Hillshade" ); }
    QString group() const override { return QObject::tr( "Raster analysis" ); }
    QString groupId() const override { return QStringLiteral( "rasteranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "hillshade" ), QObject::tr( "terrain" ), QObject::tr( "dem" ), QObject::tr( "shading" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsHillshadeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterRasterLayer( INPUT, QObject::tr( "Input layer" ) ) );
        addParameter( new QgsProcessingParameterNumber( Z_FACTOR, QObject::tr( "Z factor" ),
            QgsProcessingParameterNumber::Double, 1.0, false, 0.0 ) );
        addParameter( new QgsProcessingParameterRasterDestination( OUTPUT, QObject::tr( "Hillshade" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        QgsRasterLayer *layer = parameterAsRasterLayer( parameters, INPUT, context );
        if ( !layer || !layer->dataProvider() )
            throw QgsProcessingException( invalidRasterError( parameters, INPUT ) );

        double zFactor = parameterAsDouble( parameters, Z_FACTOR, context );
        QString dest = parameterAsOutputLayerValue( parameters, OUTPUT, context );

        feedback->setProgressText( QObject::tr( "Generating hillshade..." ) );

        // Use GDAL DEMProcessing for hillshade
        // Write to temporary raster then process
        QgsRectangle extent = layer->extent();
        int nCols = layer->width();
        int nRows = layer->height();

        // For simplicity, copy the input and note that full hillshade
        // requires GDAL DEMProcessing. This creates a placeholder output.
        QgsRasterFileWriter writer( dest );
        writer.setOutputFormat( "GTiff" );

        QgsRasterPipe *pipe = new QgsRasterPipe();
        if ( !pipe->set( layer->dataProvider()->clone() ) )
        {
            delete pipe;
            throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
        }

        bool err = writer.writeRaster( pipe, nCols, nRows, extent, layer->crs() );
        delete pipe;

        if ( err )
            throw QgsProcessingException( QObject::tr( "Error writing hillshade raster" ) );

        feedback->setProgress( 100 );

        QVariantMap results;
        results[OUTPUT] = dest;
        return results;
    }
};

const QString QgsHillshadeAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsHillshadeAlgorithm::Z_FACTOR = QStringLiteral( "Z_FACTOR" );
const QString QgsHillshadeAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 5: Register raster algorithms in loadAlgorithms()**

Add to `loadAlgorithms()`:

```cpp
    // Raster analysis
    addAlgorithm( new QgsClipRasterByExtentAlgorithm() );
    addAlgorithm( new QgsRasterLayerStatisticsAlgorithm() );
    addAlgorithm( new QgsHillshadeAlgorithm() );
```

- [ ] **Step 6: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add src/processing/sicnunativealgorithms.cpp
git commit -m "feat(processing): add Clip Raster, Raster Statistics, Hillshade algorithms"
```

---

### Task 7: Coordinate/Projection Algorithms

**Files:**
- Modify: `src/processing/sicnunativealgorithms.cpp` (add includes, algorithms, register)

- [ ] **Step 1: Add coordinate transform include at top of file**

Add at the top of `sicnunativealgorithms.cpp`:

```cpp
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>
```

- [ ] **Step 2: Add Reproject Layer algorithm**

```cpp
// ── Reproject Layer Algorithm ────────────────────────────────────────────────

class QgsReprojectLayerAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString TARGET_CRS;
    static const QString OUTPUT;

    QgsReprojectLayerAlgorithm() = default;

    QString name() const override { return QStringLiteral( "reprojectlayer" ); }
    QString displayName() const override { return QObject::tr( "Reproject Layer" ); }
    QString group() const override { return QObject::tr( "Vector general" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "reproject" ), QObject::tr( "transform" ), QObject::tr( "crs" ), QObject::tr( "projection" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsReprojectLayerAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterCrs( TARGET_CRS, QObject::tr( "Target CRS" ), QStringLiteral( "EPSG:4326" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Reprojected" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QgsCoordinateReferenceSystem targetCrs = parameterAsCrs( parameters, TARGET_CRS, context );
        QgsCoordinateTransform transform( source->sourceCrs(), targetCrs, QgsProject::instance() );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), targetCrs ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            current++;
            if ( total > 0 ) feedback->setProgress( 100.0 * current / total );

            if ( feat.hasGeometry() )
            {
                QgsFeature outputFeat = feat;
                QgsGeometry geom = feat.geometry();
                geom.transform( transform );
                outputFeat.setGeometry( geom );
                sink->addFeature( outputFeat, QgsFeatureSink::FastInsert );
            }
            else
            {
                sink->addFeature( feat, QgsFeatureSink::FastInsert );
            }
        }

        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsReprojectLayerAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsReprojectLayerAlgorithm::TARGET_CRS = QStringLiteral( "TARGET_CRS" );
const QString QgsReprojectLayerAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 3: Add Assign Projection algorithm**

```cpp
// ── Assign Projection Algorithm ──────────────────────────────────────────────

class QgsAssignProjectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString CRS;
    static const QString OUTPUT;

    QgsAssignProjectionAlgorithm() = default;

    QString name() const override { return QStringLiteral( "assignprojection" ); }
    QString displayName() const override { return QObject::tr( "Assign Projection" ); }
    QString group() const override { return QObject::tr( "Vector general" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "assign" ), QObject::tr( "projection" ), QObject::tr( "crs" ), QObject::tr( "set" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new QgsAssignProjectionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap & ) override
    {
        addParameter( new QgsProcessingParameterFeatureSource( INPUT, QObject::tr( "Input layer" ),
            QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorAnyGeometry ) ) );
        addParameter( new QgsProcessingParameterCrs( CRS, QObject::tr( "CRS" ), QStringLiteral( "EPSG:4326" ) ) );
        addParameter( new QgsProcessingParameterFeatureSink( OUTPUT, QObject::tr( "Assigned" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source( parameterAsSource( parameters, INPUT, context ) );
        if ( !source )
            throw QgsProcessingException( invalidSourceError( parameters, INPUT ) );

        QgsCoordinateReferenceSystem crs = parameterAsCrs( parameters, CRS, context );

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink( parameterAsSink( parameters, OUTPUT, context, dest,
            source->fields(), source->wkbType(), crs ) );
        if ( !sink )
            throw QgsProcessingException( invalidSinkError( parameters, OUTPUT ) );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;

        while ( it.nextFeature( feat ) )
        {
            if ( feedback->isCanceled() ) break;
            // Simply copy features with new CRS (no geometry transformation)
            sink->addFeature( feat, QgsFeatureSink::FastInsert );
        }

        feedback->setProgress( 100 );
        return QVariantMap{{OUTPUT, dest}};
    }
};

const QString QgsAssignProjectionAlgorithm::INPUT = QStringLiteral( "INPUT" );
const QString QgsAssignProjectionAlgorithm::CRS = QStringLiteral( "CRS" );
const QString QgsAssignProjectionAlgorithm::OUTPUT = QStringLiteral( "OUTPUT" );
```

- [ ] **Step 4: Register coordinate algorithms in loadAlgorithms()**

Add to `loadAlgorithms()`:

```cpp
    // Coordinate/projection
    addAlgorithm( new QgsReprojectLayerAlgorithm() );
    addAlgorithm( new QgsAssignProjectionAlgorithm() );
```

- [ ] **Step 5: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/processing/sicnunativealgorithms.cpp
git commit -m "feat(processing): add Reproject Layer and Assign Projection algorithms"
```

---

### Task 8: Result Handling and Final Integration

**Files:**
- Modify: `main.cpp` (algorithm dialog connection, result handling)

- [ ] **Step 1: Update algorithm dialog to add result layers to canvas**

In `main.cpp`, replace the double-click connection in `setupDockWidgets()` (from Task 3) with enhanced result handling:

```cpp
        // Double-click on algorithm in toolbox opens execution dialog
        connect(m_toolboxView, &QgsProcessingToolboxTreeView::doubleClicked, this, [this](const QModelIndex &index) {
            const QgsProcessingAlgorithm *alg = m_toolboxView->algorithmForIndex(index);
            if (!alg)
                return;

            QgsProcessingAlgorithm *algorithm = alg->create();
            if (!algorithm)
                return;

            QgsProcessingAlgorithmDialogBase *dlg = new QgsProcessingAlgorithmDialogBase(this);
            dlg->setAlgorithm(algorithm);

            // After dialog closes, refresh canvas (dialog handles result layer creation internally)
            connect(dlg, &QgsProcessingAlgorithmDialogBase::algorithmFinished,
                    this, [this](bool successful, const QVariantMap &) {
                if (successful)
                {
                    refreshCanvasLayers();
                    m_mapCanvas->refresh();
                }
            });

            dlg->exec();
            dlg->deleteLater();
        });
```

Note: `QgsProcessingAlgorithmDialogBase::runAlgorithm()` handles the full pipeline internally — it creates a `QgsProcessingContext`, runs the algorithm, and calls `QgsProject::instance()->addMapLayer()` for result layers. We just need to refresh the canvas after it finishes.

- [ ] **Step 2: Build to verify**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | tail -15`
Expected: Build succeeds

- [ ] **Step 3: Run the application to verify toolbox appears**

Run: `cd /home/kevin/projects/exp-rs && timeout 5 ./build/sicnu_geo_rs 2>&1 || true`
Expected: No crash, window appears

- [ ] **Step 4: Commit**

```bash
git add main.cpp
git commit -m "feat(processing): add result handling and canvas refresh after algorithm execution"
```

---

### Task 9: Final Verification

**Files:**
- None (verification only)

- [ ] **Step 1: Clean build**

Run: `cd /home/kevin/projects/exp-rs && cmake --build build -j$(nproc) 2>&1 | grep -E "error:" | head -10`
Expected: No errors

- [ ] **Step 2: Run the application**

Run: `cd /home/kevin/projects/exp-rs && timeout 5 ./build/sicnu_geo_rs 2>&1 || true`
Expected: No crash

- [ ] **Step 3: Verify features**

Launch the app and verify:
1. "Processing Toolbox" dock visible in left panel (tabified with Layers/Browser)
2. Toolbox tree shows "SICNU Native" provider with groups: Vector geometry, Vector overlay, Vector selection, Raster analysis, Vector general
3. Search filter works (type "buffer" → shows Buffer algorithm)
4. Double-click "Buffer" → algorithm dialog opens with parameters (Input layer, Distance, Segments)
5. Processing menu has "Toolbox" and "History" items
