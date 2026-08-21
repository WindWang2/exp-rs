// test_otb_new_algorithms_params.cpp — Processing Toolbox Phase 1 Task 4
//
// Parameter definition and buildArgs tests for new OTB wrappers:
// GLCM, LocalStatisticExtraction, MAD, SVM classification.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include "processing/providers/otb_tools/algorithms/otb_gray_level_cooccurrence_matrix.h"
#include "processing/providers/otb_tools/algorithms/otb_local_statistic_extraction.h"
#include "processing/providers/otb_tools/algorithms/otb_multivariate_alteration_detector.h"
#include "processing/providers/otb_tools/algorithms/otb_svm_classification.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>

namespace
{
template<typename AlgoT>
class TestableOtbAlgorithm : public AlgoT
{
  public:
    QStringList testBuildArgs( const QVariantMap &parameters )
    {
        QgsProcessingContext context;
        return this->buildArgs( parameters, context, nullptr );
    }
};
} // namespace

TEST_CASE( "OTB GLCM: algorithm metadata", "[otb][processing]" )
{
    OtbGrayLevelCooccurrenceMatrixAlgorithm algo;

    REQUIRE( algo.name() == "otb_gray_level_cooccurrence_matrix" );
    REQUIRE( algo.displayName() == "Gray Level Co-occurrence Matrix" );
    REQUIRE( algo.group() == "Feature" );
    // 651bf2167c corrected the application to the real OTB 8.x name (#296)
    REQUIRE( algo.applicationName() == "HaralickTextureExtraction" );
    REQUIRE( !algo.tags().isEmpty() );
}

TEST_CASE( "OTB GLCM: parameter definitions", "[otb][processing]" )
{
    OtbGrayLevelCooccurrenceMatrixAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QStringList names;
    for ( auto *p : params )
        names << p->name();

    REQUIRE( names.contains( "INPUT" ) );
    REQUIRE( names.contains( "OUTPUT" ) );
    REQUIRE( names.contains( "CHANNEL" ) );
    REQUIRE( names.contains( "XRAD" ) );
    REQUIRE( names.contains( "YRAD" ) );
    REQUIRE( names.contains( "NBBIN" ) );
}

TEST_CASE( "OTB GLCM: buildArgs", "[otb][processing]" )
{
    TestableOtbAlgorithm<OtbGrayLevelCooccurrenceMatrixAlgorithm> algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["CHANNEL"] = 2;
    params["XRAD"] = 3;
    params["YRAD"] = 4;
    params["XOFF"] = 1;
    params["YOFF"] = 2;
    params["MIN"] = 0.0;
    params["MAX"] = 255.0;
    params["NBBIN"] = 16;
    params["OUTPUT"] = "/tmp/glcm.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in" ) + 1] == "/data/input.tif" );
    REQUIRE( args.indexOf( "-channel" ) >= 0 );
    REQUIRE( args[args.indexOf( "-channel" ) + 1] == "2" );
    REQUIRE( args.indexOf( "-parameters.xrad" ) >= 0 );
    REQUIRE( args[args.indexOf( "-parameters.xrad" ) + 1] == "3" );
    REQUIRE( args.indexOf( "-parameters.nbbin" ) >= 0 );
    REQUIRE( args[args.indexOf( "-parameters.nbbin" ) + 1] == "16" );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/glcm.tif" );
}

TEST_CASE( "OTB LocalStatistic: parameter definitions", "[otb][processing]" )
{
    OtbLocalStatisticExtractionAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QStringList names;
    for ( auto *p : params )
        names << p->name();

    REQUIRE( names.contains( "INPUT" ) );
    REQUIRE( names.contains( "OUTPUT" ) );
    REQUIRE( names.contains( "CHANNEL" ) );
    REQUIRE( names.contains( "RADIUS" ) );
}

TEST_CASE( "OTB LocalStatistic: buildArgs", "[otb][processing]" )
{
    TestableOtbAlgorithm<OtbLocalStatisticExtractionAlgorithm> algo;

    QVariantMap params;
    params["INPUT"] = "/data/input.tif";
    params["CHANNEL"] = 1;
    params["RADIUS"] = 5;
    params["OUTPUT"] = "/tmp/stats.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in" ) + 1] == "/data/input.tif" );
    REQUIRE( args.indexOf( "-channel" ) >= 0 );
    REQUIRE( args[args.indexOf( "-channel" ) + 1] == "1" );
    REQUIRE( args.indexOf( "-radius" ) >= 0 );
    REQUIRE( args[args.indexOf( "-radius" ) + 1] == "5" );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/stats.tif" );
}

TEST_CASE( "OTB MAD: parameter definitions", "[otb][processing]" )
{
    OtbMultivariateAlterationDetectorAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QStringList names;
    for ( auto *p : params )
        names << p->name();

    REQUIRE( names.contains( "INPUT1" ) );
    REQUIRE( names.contains( "INPUT2" ) );
    REQUIRE( names.contains( "OUTPUT" ) );
}

TEST_CASE( "OTB MAD: buildArgs", "[otb][processing]" )
{
    TestableOtbAlgorithm<OtbMultivariateAlterationDetectorAlgorithm> algo;

    QVariantMap params;
    params["INPUT1"] = "/data/before.tif";
    params["INPUT2"] = "/data/after.tif";
    params["OUTPUT"] = "/tmp/mad.tif";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-in1" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in1" ) + 1] == "/data/before.tif" );
    REQUIRE( args.indexOf( "-in2" ) >= 0 );
    REQUIRE( args[args.indexOf( "-in2" ) + 1] == "/data/after.tif" );
    REQUIRE( args.indexOf( "-out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-out" ) + 1] == "/tmp/mad.tif" );
}

TEST_CASE( "OTB SVM: parameter definitions", "[otb][processing]" )
{
    OtbSvmClassificationAlgorithm algo;
    algo.initAlgorithm();

    auto params = algo.parameterDefinitions();
    QStringList names;
    for ( auto *p : params )
        names << p->name();

    REQUIRE( names.contains( "INPUT" ) );
    REQUIRE( names.contains( "VECTOR" ) );
    REQUIRE( names.contains( "OUTPUT" ) );
    REQUIRE( names.contains( "KERNEL" ) );
    REQUIRE( names.contains( "C" ) );
}

TEST_CASE( "OTB SVM: buildArgs", "[otb][processing]" )
{
    TestableOtbAlgorithm<OtbSvmClassificationAlgorithm> algo;

    QVariantMap params;
    params["INPUT"] = "/data/image.tif";
    params["VECTOR"] = "/data/training.shp";
    params["LABEL_FIELD"] = "class_id";
    params["KERNEL"] = 1;
    params["C"] = 2.5;
    params["OUTPUT"] = "/tmp/svm_model.txt";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-io.il" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.il" ) + 1] == "/data/image.tif" );
    REQUIRE( args.indexOf( "-io.vd" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.vd" ) + 1] == "/data/training.shp" );
    REQUIRE( args.indexOf( "-sample.vfn" ) >= 0 );
    REQUIRE( args[args.indexOf( "-sample.vfn" ) + 1] == "class_id" );
    REQUIRE( args.indexOf( "-classifier" ) >= 0 );
    REQUIRE( args[args.indexOf( "-classifier" ) + 1] == "libsvm" );
    REQUIRE( args.indexOf( "-classifier.libsvm.k" ) >= 0 );
    REQUIRE( args[args.indexOf( "-classifier.libsvm.k" ) + 1] == "rbf" );
    REQUIRE( args.indexOf( "-classifier.libsvm.c" ) >= 0 );
    REQUIRE( args[args.indexOf( "-classifier.libsvm.c" ) + 1] == "2.5000" );
    REQUIRE( args.indexOf( "-io.out" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.out" ) + 1] == "/tmp/svm_model.txt" );
    REQUIRE( !args.contains( "-io.imstat" ) );
}

TEST_CASE( "OTB SVM: buildArgs with statistics file", "[otb][processing]" )
{
    TestableOtbAlgorithm<OtbSvmClassificationAlgorithm> algo;

    QVariantMap params;
    params["INPUT"] = "/data/image.tif";
    params["VECTOR"] = "/data/training.shp";
    params["STATS"] = "/data/stats.xml";
    params["LABEL_FIELD"] = "Class";
    params["KERNEL"] = 0;
    params["C"] = 1.0;
    params["OUTPUT"] = "/tmp/svm_model.txt";

    const QStringList args = algo.testBuildArgs( params );

    REQUIRE( args.indexOf( "-io.imstat" ) >= 0 );
    REQUIRE( args[args.indexOf( "-io.imstat" ) + 1] == "/data/stats.xml" );
    REQUIRE( args[args.indexOf( "-classifier.libsvm.k" ) + 1] == "linear" );
}