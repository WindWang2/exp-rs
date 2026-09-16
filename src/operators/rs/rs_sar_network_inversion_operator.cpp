/***************************************************************************
 * rs_sar_network_inversion_operator.cpp — see the header
 * (Advanced InSAR 11.0, package F; DECISIONS D-006)
 ***************************************************************************/
#include "rs_sar_network_inversion_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_baseline.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_network_inversion.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QCoreApplication>
#include <QString>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTile = 256;
constexpr int kMaxEpochs = 200;
constexpr int kMaxPairs = 64;

} // anonymous namespace

Json::Value RsSarNetworkInversionOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["displacementInputs"] = makeStringParam(
        "displacementInputs",
        "JSON array of pairCount raster paths: per-pair LOS displacement in metres "
        "(rs:sar_displacement products of the unwrapped network), same grid, pair "
        "order matches `pairs`" );
    props["pairs"] = makeStringParam(
        "pairs", "JSON array of [masterEpoch, slaveEpoch] index pairs (master > slave; "
                 "epochs index the acquisition table, 0 = reference)" );
    props["epochTemporalYears"] = makeStringParam(
        "epochTemporalYears",
        "Optional JSON array of epochCount ascending years since the reference epoch "
        "(velocity time base; missing = uniform one-year spacing)" );
    props["pairWeights"] = makeStringParam(
        "pairWeights", "Optional JSON array of per-pair weights > 0 (e.g. per-pair mean "
                       "coherence²; a stack property, not a per-pixel input)" );
    props["velocityOutput"] = makeOutputParam( "velocityOutput",
                                               "Output linear velocity raster (m/year)",
                                               "tif" );
    props["displacementOutput"] =
        makeOutputParam( "displacementOutput",
                         "Optional epochCount-band displacement stack (m; band 1 = "
                         "reference epoch, all zeros; NaN outside the reference "
                         "component)",
                         "tif" );
    props["rmsOutput"] = makeOutputParam(
        "rmsOutput", "Optional fit RMS residual raster (m; NaN where unsolved)", "tif" );
    props["maskStrategy"] = makeEnumParam( "maskStrategy",
                                           "Missing-data policy: perpixel (rows drop) | "
                                           "intersect (any NaN drops the pixel)",
                                           { "perpixel", "intersect" }, "perpixel" );
    props["maxPatterns"] = makeIntegerParam(
        "maxPatterns", "Distinct missing-data pattern cache bound (typed refusal beyond)",
        128 );

    Json::Value outputs( Json::objectValue );
    outputs["velocityOutput"] = makeRasterParam( "velocityOutput", "Velocity (m/year)" );
    outputs["displacementOutput"] = makeRasterParam( "displacementOutput",
                                                     "Per-epoch displacement stack" );
    outputs["rmsOutput"] = makeRasterParam( "rmsOutput", "Fit RMS residual (m)" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "displacementInputs", "pairs", "velocityOutput" } );
    return root;
}

Json::Value RsSarNetworkInversionOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Turn a connected network of unwrapped pair displacements into "
                      "per-epoch displacement and linear velocity with honest quality "
                      "accounts.";
    meta["prerequisites"].append( "Connected pair network (rs:sar_pair_network) and "
                                  "per-pair unwrapped displacement rasters on one grid." );
    meta["limitations"].append( "LINEAR small-baseline model: atmospheric phase stays "
                                "in the epoch displacements — NOT PSI (no PS selection, "
                                "no APS separation)." );
    meta["limitations"].append( "Bounded scale: 64 pairs (pattern-mask bound), 200 "
                                "epochs, 128 missing-data patterns (typed refusals "
                                "beyond)." );
    return meta;
}

Json::Value RsSarNetworkInversionOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTile;
    est["tileHeight"] = kTile;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        4ULL * ( kMaxPairs + kMaxEpochs + 4 ) * kTile * kTile );
    return est;
}

Json::Value RsSarNetworkInversionOperator::estimateExecution(
    const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarNetworkInversionOperator::run( const Json::Value &params,
                                                RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const Json::Value &inputsJson = params["displacementInputs"];
    const Json::Value &pairsJson = params["pairs"];
    if ( !inputsJson.isArray() || inputsJson.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "displacementInputs must be a non-empty JSON array of "
                               "raster paths" );
    if ( !pairsJson.isArray() || pairsJson.empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "pairs must be a non-empty JSON array of "
                               "[masterEpoch, slaveEpoch]" );

    const int pairCount = static_cast<int>( inputsJson.size() );
    const std::string velocityPath = requireString( params, "velocityOutput" );
    const std::string stackPath = getString( params, "displacementOutput", std::string() );
    const std::string rmsPath = getString( params, "rmsOutput", std::string() );
    const std::string maskStrategy = getEnum( params, "maskStrategy",
                                              { "perpixel", "intersect" }, "perpixel" );
    const int maxPatterns = getInt( params, "maxPatterns", 128 );

    if ( pairCount > kMaxPairs )
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "NETWORK_INVERSION_PAIR_LIMIT: " + std::to_string( pairCount )
                + " pairs exceed the " + std::to_string( kMaxPairs )
                + "-pair pattern-mask bound — split the network" );

    // Pair graph from JSON (epoch indices; validation in the problem).
    sicnu::sar::NetworkInversionProblem problem;
    problem.pairCount = pairCount;
    int epochCount = 0;
    for ( Json::ArrayIndex i = 0; i < pairsJson.size(); ++i )
    {
        const Json::Value &pair = pairsJson[i];
        if ( !pair.isArray() || pair.size() != 2 || !pair[0].isIntegral()
             || !pair[1].isIntegral() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "pairs[" + std::to_string( i )
                                       + "] must be [masterEpoch, slaveEpoch] integers" );
        const int master = pair[0].asInt();
        const int slave = pair[1].asInt();
        if ( master < 0 || slave < 0 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "pairs[" + std::to_string( i )
                                       + "] epoch indices must be >= 0" );
        problem.pairMasterEpoch.push_back( master );
        problem.pairSlaveEpoch.push_back( slave );
        problem.pairTemporalYears.push_back( 0.0 ); // filled below (QA field)
        epochCount = std::max( epochCount, master + 1 );
        epochCount = std::max( epochCount, slave + 1 );
    }
    problem.epochCount = epochCount;
    if ( epochCount > kMaxEpochs )
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "NETWORK_INVERSION_EPOCH_LIMIT: " + std::to_string( epochCount )
                + " epochs exceed the " + std::to_string( kMaxEpochs ) + "-epoch bound" );

    // Optional velocity time base (ascending years since the reference).
    std::vector<double> epochTemporalYears;
    if ( params.isMember( "epochTemporalYears" ) && params["epochTemporalYears"].isArray() )
    {
        const Json::Value &temporalJson = params["epochTemporalYears"];
        if ( static_cast<int>( temporalJson.size() ) != epochCount )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "epochTemporalYears must hold exactly " +
                                       std::to_string( epochCount ) + " entries" );
        for ( Json::ArrayIndex e = 0; e < temporalJson.size(); ++e )
        {
            if ( !temporalJson[e].isNumeric() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "epochTemporalYears[" + std::to_string( e )
                                           + "] must be a number" );
            epochTemporalYears.push_back( temporalJson[e].asDouble() );
        }
        for ( size_t e = 1; e < epochTemporalYears.size(); ++e )
            if ( !( epochTemporalYears[e] > epochTemporalYears[e - 1] ) )
                throw RSOperatorError(
                    ErrorCode::InvalidParameter,
                    "epochTemporalYears must be strictly ascending (years since the "
                    "reference epoch)" );
    }

    // Optional per-pair weights (stack property; QA temporal years scale).
    if ( params.isMember( "pairWeights" ) && params["pairWeights"].isArray() )
    {
        const Json::Value &weightsJson = params["pairWeights"];
        if ( static_cast<int>( weightsJson.size() ) != pairCount )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "pairWeights must hold exactly " +
                                       std::to_string( pairCount ) + " entries" );
        for ( Json::ArrayIndex i = 0; i < weightsJson.size(); ++i )
        {
            if ( !weightsJson[i].isNumeric() )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "pairWeights[" + std::to_string( i )
                                           + "] must be a number" );
            problem.pairWeights.push_back( weightsJson[i].asDouble() );
        }
    }
    // QA-only temporal years per pair from the epoch table.
    {
        std::vector<double> temporal = epochTemporalYears;
        if ( temporal.empty() )
            temporal.resize( static_cast<size_t>( epochCount ), 0.0 );
        for ( int i = 0; i < pairCount; ++i )
        {
            const int m = problem.pairMasterEpoch[static_cast<size_t>( i )];
            const int s = problem.pairSlaveEpoch[static_cast<size_t>( i )];
            problem.pairTemporalYears[static_cast<size_t>( i )] =
                std::abs( temporal[static_cast<size_t>( m )]
                          - temporal[static_cast<size_t>( s )] );
        }
    }
    if ( !problem.isValid() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "NETWORK_INVERSION_RANK_DEFICIENT: the pair/epoch contract "
                               "is invalid (masters must precede slaves; counts consistent; "
                               "weights > 0)" );

    // --- Displacement rasters: same grid, readable band 1 ---------------
    ensureGdalInit();
    std::vector<std::unique_ptr<GdalDatasetWrapper>> datasets;
    datasets.reserve( pairCount );
    for ( int i = 0; i < pairCount; ++i )
    {
        const std::string path = inputsJson[static_cast<Json::ArrayIndex>( i )].asString();
        auto ds = std::make_unique<GdalDatasetWrapper>();
        if ( !ds->open( QString::fromStdString( path ) ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to open displacement raster " + path );
        if ( ds->width() <= 0 || ds->height() <= 0 )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Displacement raster is empty: " + path );
        if ( i > 0 )
        {
            const sicnu::data::GridCompatReport report = sicnu::data::compareGrids(
                sicnu::processing::gridFromDataset( *datasets.front() ),
                sicnu::processing::gridFromDataset( *ds ) );
            if ( !report.compatible() )
            {
                std::string detail;
                for ( const sicnu::data::GridCompatIssue &issue : report.issues )
                    detail += issue.message.toStdString() + "; ";
                throw RSOperatorError( ErrorCode::InvalidInputData,
                                       "GRID_MISMATCH: displacement raster " + path
                                           + " off the reference grid (" + detail + ")" );
            }
        }
        datasets.push_back( std::move( ds ) );
    }

    const int width = datasets.front()->width();
    const int height = datasets.front()->height();

    sicnu::sar::NetworkInversionSolver solver( problem, maxPatterns );

    // --- Outputs ---------------------------------------------------------
    const auto &reference = *datasets.front();
    GdalStreamingOutput velocityOut( QString::fromStdString( velocityPath ), width, height,
                                     1, GDT_Float32, reference.geoTransform(),
                                     reference.projection() );
    if ( !velocityOut.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create velocity raster: " + velocityPath );
    velocityOut.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    velocityOut.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "network_velocity" );
    velocityOut.setMetadataItem( "SICNU_SAR_INSAR_TRUTH_VERSION",
                                 std::to_string( sicnu::sar::kInSarTruthVersion ).c_str() );
    velocityOut.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );

    std::unique_ptr<GdalStreamingOutput> stackOut;
    if ( !stackPath.empty() )
    {
        stackOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString( stackPath ), width, height, epochCount, GDT_Float32,
            reference.geoTransform(), reference.projection() );
        if ( !stackOut->isOpen() )
        {
            velocityOut.abandon();
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create displacement stack: " + stackPath );
        }
        stackOut->setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        stackOut->setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "network_epoch_displacement" );
        for ( int band = 1; band <= epochCount; ++band )
            stackOut->setBandNoDataValue( band, std::numeric_limits<double>::quiet_NaN() );
    }
    std::unique_ptr<GdalStreamingOutput> rmsOut;
    if ( !rmsPath.empty() )
    {
        rmsOut = std::make_unique<GdalStreamingOutput>(
            QString::fromStdString( rmsPath ), width, height, 1, GDT_Float32,
            reference.geoTransform(), reference.projection() );
        if ( !rmsOut->isOpen() )
        {
            velocityOut.abandon();
            if ( !stackPath.empty() )
                stackOut->abandon();
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create RMS raster: " + rmsPath );
        }
        rmsOut->setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        rmsOut->setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "network_fit_rms" );
        rmsOut->setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );
    }

    // --- Streaming tiles (single solve pass per pixel) -------------------
    const int tilesX = ( width + kTile - 1 ) / kTile;
    const int tilesY = ( height + kTile - 1 ) / kTile;
    const int totalTiles = tilesX * tilesY;
    std::vector<std::vector<float>> pairTiles( static_cast<size_t>( pairCount ) );
    std::vector<float> velocityTile( static_cast<size_t>( kTile ) * kTile );
    std::vector<float> rmsTile( static_cast<size_t>( kTile ) * kTile );
    std::vector<double> displacementRow( static_cast<size_t>( pairCount ) );
    std::vector<double> epochDisplacement( static_cast<size_t>( epochCount ) );
    const double kNan = std::numeric_limits<double>::quiet_NaN();
    const double *temporalPtr =
        epochTemporalYears.empty() ? nullptr : epochTemporalYears.data();

    long long solvedPixels = 0;
    long long intersectDropped = 0;
    int maxDroppedPairs = 0;

    int tileIndex = 0;
    for ( int ty = 0; ty < tilesY; ++ty )
    {
        for ( int tx = 0; tx < tilesX; ++tx, ++tileIndex )
        {
            context.throwIfCancelled();
            const int x0 = tx * kTile;
            const int y0 = ty * kTile;
            const int tw = std::min( kTile, width - x0 );
            const int th = std::min( kTile, height - y0 );
            const size_t n = static_cast<size_t>( tw ) * th;

            for ( int i = 0; i < pairCount; ++i )
            {
                pairTiles[static_cast<size_t>( i )].resize( n );
                if ( !datasets[static_cast<size_t>( i )]->readBandWindow(
                         1, x0, y0, tw, th, pairTiles[static_cast<size_t>( i )].data() ) )
                {
                    velocityOut.abandon();
                    if ( !stackPath.empty() )
                        stackOut->abandon();
                    if ( !rmsPath.empty() )
                        rmsOut->abandon();
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read displacement raster tile "
                                               + std::to_string( i ) + " at (" +
                                               std::to_string( tx ) + ", "
                                               + std::to_string( ty ) + ")" );
                }
            }

            // Per-epoch planes for this tile (only when the stack output is
            // requested — the band write happens after the pixel loop).
            std::vector<std::vector<float>> epochPlanes;
            if ( !stackPath.empty() )
                epochPlanes.assign( static_cast<size_t>( epochCount ),
                                    std::vector<float>( n, std::numeric_limits<float>::quiet_NaN() ) );

            for ( int y = 0; y < th; ++y )
            {
                for ( int x = 0; x < tw; ++x )
                {
                    const size_t idx = static_cast<size_t>( y ) * tw + x;
                    velocityTile[idx] = std::numeric_limits<float>::quiet_NaN();
                    rmsTile[idx] = std::numeric_limits<float>::quiet_NaN();

                    bool allValid = true;
                    for ( int i = 0; i < pairCount; ++i )
                    {
                        const float v = pairTiles[static_cast<size_t>( i )][idx];
                        displacementRow[static_cast<size_t>( i )] = v;
                        if ( !std::isfinite( v ) )
                            allValid = false;
                    }
                    if ( maskStrategy == "intersect" && !allValid )
                    {
                        ++intersectDropped;
                        continue;
                    }

                    int solvedEpochs = 0;
                    int droppedPairs = 0;
                    double velocity = kNan;
                    double rms = kNan;
                    QString error;
                    if ( !solver.solvePixel( displacementRow.data(), temporalPtr,
                                             epochDisplacement.data(), &velocity, &rms,
                                             &solvedEpochs, &droppedPairs, &error ) )
                    {
                        velocityOut.abandon();
                        if ( !stackPath.empty() )
                            stackOut->abandon();
                        if ( !rmsPath.empty() )
                            rmsOut->abandon();
                        throw RSOperatorError( ErrorCode::InvalidInputData,
                                               error.toStdString() );
                    }
                    maxDroppedPairs = std::max( maxDroppedPairs, droppedPairs );
                    if ( solvedEpochs > 0 )
                    {
                        velocityTile[idx] = static_cast<float>( velocity );
                        rmsTile[idx] = static_cast<float>( rms );
                        ++solvedPixels;
                        for ( int e = 0; e < epochCount; ++e )
                            epochPlanes[static_cast<size_t>( e )][idx] =
                                static_cast<float>(
                                    epochDisplacement[static_cast<size_t>( e )] );
                    }
                }
            }

            const GdalBlockStream::Tile tile{ x0, y0, tw, th, 0, tw, th, 0, 1, tw, th };
            if ( !velocityOut.writeTile( 1, tile, velocityTile.data() ) )
            {
                velocityOut.abandon();
                if ( !stackPath.empty() )
                    stackOut->abandon();
                if ( !rmsPath.empty() )
                    rmsOut->abandon();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write the velocity tile at (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }
            if ( !rmsPath.empty() && !rmsOut->writeTile( 1, tile, rmsTile.data() ) )
            {
                velocityOut.abandon();
                rmsOut->abandon();
                if ( !stackPath.empty() )
                    stackOut->abandon();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write the RMS tile at (" +
                                           std::to_string( tx ) + ", "
                                           + std::to_string( ty ) + ")" );
            }
            if ( !stackPath.empty() )
            {
                for ( int e = 0; e < epochCount; ++e )
                {
                    if ( !stackOut->writeTile(
                             e + 1, tile, epochPlanes[static_cast<size_t>( e )].data() ) )
                    {
                        velocityOut.abandon();
                        stackOut->abandon();
                        if ( !rmsPath.empty() )
                            rmsOut->abandon();
                        throw RSOperatorError(
                            ErrorCode::GdalError,
                            "Failed to write displacement stack band "
                                + std::to_string( e + 1 ) );
                    }
                }
            }

            context.reportProgress(
                0.95 * static_cast<double>( tileIndex + 1 ) / totalTiles,
                "Inverted tile " + std::to_string( tileIndex + 1 ) + "/"
                    + std::to_string( totalTiles ) );
        }
    }


    if ( !velocityOut.closeWithError()
         || ( !stackPath.empty() && !stackOut->closeWithError() )
         || ( !rmsPath.empty() && !rmsOut->closeWithError() ) )
    {
        velocityOut.abandon();
        if ( !stackPath.empty() )
            stackOut->abandon();
        if ( !rmsPath.empty() )
            rmsOut->abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize the inversion outputs" );
    }

    Json::Value json;
    json["velocityOutput"] = velocityPath;
    if ( !stackPath.empty() )
        json["displacementOutput"] = stackPath;
    if ( !rmsPath.empty() )
        json["rmsOutput"] = rmsPath;
    json["epochCount"] = epochCount;
    json["pairCount"] = pairCount;
    json["solvedPixels"] = Json::Value::Int64( solvedPixels );
    json["intersectDroppedPixels"] = Json::Value::Int64( intersectDropped );
    json["maxDroppedPairs"] = maxDroppedPairs;
    json["distinctPatterns"] = Json::Value::Int64( solver.distinctPatterns() );
    json["maskStrategy"] = maskStrategy;
    context.reportProgress( 1.0, "Network inversion complete" );
    return json;
}

} // namespace sicnu::operators::rs
