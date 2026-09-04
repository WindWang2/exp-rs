// test_obia_architecture.cpp — #663 architectural guardrail.
//
// The OBIA GUI is a client of the rs:obia_* operators (ADR 0126): it may
// hold label-map DATA structures (RsSegmentMap, SegmentStat,
// RsObjectHierarchy fixtures, accuracy results) but must not execute
// analysis kernels or construct classifier backends. This test scans the
// shipped sources so a future edit cannot quietly reintroduce a
// GUI → kernel execution path for the migrated flows.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace
{

// Headers whose inclusion from src/app/obia would re-open a kernel execution
// path (algorithm classes). The data structures the adapter rehydrates —
// rs_segment_map.h / rs_segment_features.h / rs_object_hierarchy.h /
// rs_accuracy_assessment.h — are allowed as data contracts.
// RsHierarchyClassConsolidator stays interactive-only (documented deferred
// debt in ADR 0126) and is therefore NOT in the forbidden list; the comment
// in the test below tracks the exemption.

} // namespace

TEST_CASE( "OBIA GUI sources contain no analysis-kernel execution includes", "[obia][architecture]" )
{
    const QDir obiaDir( QStringLiteral( CMAKE_SOURCE_DIR ) + QStringLiteral( "/src/app/obia" ) );
    REQUIRE( obiaDir.exists() );

    // RsHierarchyClassConsolidator is the one remaining direct kernel use
    // (interactive label-map consolidation, documented as deferred debt in
    // ADR 0126). It is excluded from the forbidden list but tracked here so
    // the debt stays visible; remove the exemption when an operator exists.
    const QStringList files = obiaDir.entryList( { "*.cpp", "*.h" }, QDir::Files );
    REQUIRE( files.size() >= 4 ); // adapter + window + dock + select tool

    QStringList violations;
    for (const QString &file : files)
    {
        QFile f( obiaDir.filePath( file ) );
        if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
        {
            violations << QStringLiteral( "%1 (unreadable)" ).arg( file );
            continue;
        }
        QTextStream in( &f );
        const QString source = in.readAll();
        for ( const char *header : { "rs_simple_segmenter.h", "rs_otb_segmenter.h",
                                     "rs_object_classify.h", "rs_classifier_backend_factory.h",
                                     "rs_classifier_backend.h", "rs_classifier_svm.h",
                                     "rs_classifier_normalbayes.h", "rs_classifier_kmeans.h",
                                     "rs_classifier_random_forest.h", "rs_classifier_mlp.h",
                                     "rs_roi_labeler.h", "rs_class_raster.h",
                                     "rs_hierarchy_features.h", "rs_parent_link.h",
                                     "rs_segmenter_port.h" } )
        {
            if ( source.contains( QLatin1String( header ) ) )
                violations << QStringLiteral( "%1 includes %2" ).arg( file, QLatin1String( header ) );
        }
    }
    INFO( violations.join( QStringLiteral( "; " ) ).toStdString() );
    REQUIRE( violations.isEmpty() );
}

TEST_CASE( "OBIA GUI sources dispatch operator ids, not module lambdas", "[obia][architecture]" )
{
    const QDir obiaDir( QStringLiteral( CMAKE_SOURCE_DIR ) + QStringLiteral( "/src/app/obia" ) );
    REQUIRE( obiaDir.exists() );

    const QStringList files = obiaDir.entryList( { "*.cpp", "*.h" }, QDir::Files );
    QString windowSource;
    {
        QFile f( obiaDir.filePath( QStringLiteral( "rs_obia_main_window.cpp" ) ) );
        REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
        QTextStream in( &f );
        windowSource = in.readAll();
    }

    // The retired pseudo-id seam must not come back.
    REQUIRE_FALSE( windowSource.contains( QStringLiteral( "module:obia:" ) ) );

    // The five flows dispatch the registered operator contracts.
    for ( const char *op : { "rs:obia_segment", "rs:obia_features", "rs:obia_label",
                             "rs:obia_classify", "rs:obia_hierarchy", "gdal:polygonize" } )
    {
        INFO( op );
        REQUIRE( windowSource.contains( QLatin1String( op ) ) );
    }
}

TEST_CASE( "mosaic panel dispatches rs:mosaic, not a callable lambda", "[architecture][mosaic]" )
{
    const QString path = QStringLiteral( CMAKE_SOURCE_DIR ) +
                         QStringLiteral( "/src/app/panels/mosaic_panel.cpp" );
    QFile f( path );
    if ( !f.exists() )
        SUCCEED( "panel not present in this build configuration" );
    else
    {
        REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
        QTextStream in( &f );
        const QString source = in.readAll();
        REQUIRE_FALSE( source.contains( QStringLiteral( "callable:mosaic_panel" ) ) );
        REQUIRE( source.contains( QStringLiteral( "rs:mosaic" ) ) );
    }
}
