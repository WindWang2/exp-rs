// test_r33_t04_change_mask_recovery.cpp — R33-T04 / WP09 (#2104):
//
//   The chunk journal's PUBLISHED marker is written by the MAGNITUDE phase,
//   but a makeMask run's final deliverable is the MASK at the output path.
//   When the mask write failed or was cancelled after the magnitude publish
//   (its temp is removed either way), the marker used to strand the run: a
//   retry hit alreadyPublished, failed to reopen the never-written mask and
//   demanded manual journal surgery. The marker claim is now bound to the
//   final deliverable via verifyPublishedOutput: a missing/incomplete mask
//   demotes the marker to a rebuild (tiles reused, mask re-derived).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/rs/rs_change_detection_operator.h"

#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <json/json.h>

#include <array>
#include <string>
#include <vector>

namespace
{

constexpr int W = 64;
constexpr int H = 64;

void makePair( const QString &dir, QString &beforePath, QString &afterPath )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    beforePath = dir + "/before.tif";
    afterPath = dir + "/after.tif";
    const auto make = [ &driver ]( const QString &path, bool withChange ) {
        std::vector<float> data( static_cast<std::size_t>( W ) * H, 10.0f );
        if ( withChange )
        {
            for ( int y = 8; y < 20; ++y )
                for ( int x = 8; x < 20; ++x )
                    data[static_cast<std::size_t>( y ) * W + x] = 80.0f;
        }
        GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1,
                                      GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                               data.data(), W, H, GDT_Float32, 0, 0 )
                 == CE_None );
        GDALClose( ds );
    };
    make( beforePath, false );
    make( afterPath, true );
}

Json::Value maskParams( const QString &before, const QString &after, const QString &output,
                        const std::string &stateBase )
{
    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = output.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "manual";
    params["threshold"] = 10.0;
    params["resumeStateBase"] = stateBase;
    return params;
}
} // namespace

TEST_CASE( "a lost mask self-heals from the magnitude journal instead of demanding surgery",
           "[operators][change][r33t04]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    QString before, after;
    makePair( tmp.path(), before, after );
    const QString output = tmp.path() + "/mask.tif";
    const std::string stateBase = ( tmp.path() + "/state" ).toStdString();

    sicnu::operators::rs::RsChangeDetectionOperator op;

    // First run completes the full deliverable.
    {
        sicnu::operators::RSOperatorContext ctx;
        const Json::Value res = op.run( maskParams( before, after, output, stateBase ), ctx );
        REQUIRE( res["changedPixels"].asDouble() > 0.0 );
        REQUIRE( res["already_published"].asBool() == false );
    }

    SECTION( "retry with the completed mask standing is the zero-work hit" )
    {
        sicnu::operators::RSOperatorContext ctx;
        const Json::Value res = op.run( maskParams( before, after, output, stateBase ), ctx );
        REQUIRE( res["already_published"].asBool() == true );
        // The read-back values are the persisted record.
        CHECK( res["changedPixels"].asDouble() > 0.0 );
    }

    SECTION( "retry after the mask vanished (the #2104 crash window)" )
    {
        REQUIRE( QFile::remove( output ) ); // mask write "failed": no final deliverable

        sicnu::operators::RSOperatorContext ctx;
        // Previously: typed error telling the user to delete the chunk
        // journal by hand. Now: the marker demotes to a rebuild, the mask is
        // re-derived and re-published.
        const Json::Value res = op.run( maskParams( before, after, output, stateBase ), ctx );
        REQUIRE( res["changedPixels"].asDouble() > 0.0 );
        CHECK( res["already_published"].asBool() == false ); // it was rebuilt

        // The re-published mask is a COMPLETED product: it carries the
        // method + threshold stamp the read-back path verifies.
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds != nullptr );
        const char *method = GDALGetMetadataItem( ds, "SICNU_CHANGE_METHOD", nullptr );
        const char *threshold = GDALGetMetadataItem( ds, "SICNU_CHANGE_THRESHOLD", nullptr );
        GDALClose( ds );
        CHECK( method != nullptr );
        CHECK( threshold != nullptr );

        // And a THIRD run now hits the zero-work path against the healed
        // deliverable.
        sicnu::operators::RSOperatorContext ctx2;
        const Json::Value res2 = op.run( maskParams( before, after, output, stateBase ), ctx2 );
        CHECK( res2["already_published"].asBool() == true );
        CHECK( res2["changedPixels"].asDouble() == res["changedPixels"].asDouble() );
    }
}
