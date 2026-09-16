/***************************************************************************
 * rs_sar_pair_network_operator.cpp — see the header
 * (Advanced InSAR 11.0, package E; DECISIONS D-005)
 ***************************************************************************/
#include "rs_sar_pair_network_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_baseline.h"
#include "processing/algorithms/sar/sar_orbit.h"
#include "processing/algorithms/sar/sar_pair_network.h"
#include "processing/algorithms/sar/sar_temporal_events.h"

#include <QFile>
#include <QTemporaryFile>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kMaxScenes = 512;

sicnu::sar::InSarSceneTruth parseSceneJson( const Json::Value &scene, int index )
{
    if ( !scene.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "scenes[" + std::to_string( index )
                                   + "] must be a JSON object" );

    sicnu::sar::InSarSceneTruth truth;
    truth.wavelengthUm = getDouble( scene, "wavelengthUm", 0.0 );
    const std::string orbitStates = getString( scene, "orbitStates", std::string() );

    // Acquisition UTC: ISO 8601 string (the SICNU_SAR_ACQUISITION_UTC
    // grammar) or absolute seconds — one of them is required.
    const std::string utcText = getString( scene, "acquisitionUtc", std::string() );
    const double utcSec = getDouble( scene, "acquisitionUtcSec", 0.0 );
    if ( !utcText.empty() )
    {
        QString error;
        if ( !sicnu::sar::parseAcquisitionUtc( QString::fromStdString( utcText ),
                                               &truth.acquisitionUtcSec, &error ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "scenes[" + std::to_string( index )
                                       + "].acquisitionUtc — " + error.toStdString() );
    }
    else if ( utcSec > 0.0 )
    {
        truth.acquisitionUtcSec = utcSec;
    }
    else
    {
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "scenes[" + std::to_string( index )
                                   + "] needs acquisitionUtc (ISO 8601 UTC) or "
                                     "acquisitionUtcSec" );
    }

    // Orbit states (required, SICNU_SAR_ORBIT_STATES grammar).
    QString orbitError;
    if ( !sicnu::sar::parseOrbitStates( QString::fromStdString( orbitStates ),
                                        &truth.orbit, &orbitError ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "scenes[" + std::to_string( index ) + "].orbitStates — "
                                   + orbitError.toStdString() );

    // Optional absolute anchor of the orbit time base.
    const std::string anchorText = getString( scene, "azimuthStartUtc", std::string() );
    const double anchorSec = getDouble( scene, "azimuthStartUtcSec", 0.0 );
    if ( !anchorText.empty() )
    {
        QString error;
        if ( !sicnu::sar::parseAcquisitionUtc( QString::fromStdString( anchorText ),
                                               &truth.azimuthStartUtcSec, &error ) )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "scenes[" + std::to_string( index )
                                       + "].azimuthStartUtc — " + error.toStdString() );
    }
    else if ( anchorSec > 0.0 )
    {
        truth.azimuthStartUtcSec = anchorSec;
    }
    return truth;
}

/// Atomic JSON sidecar (repo .tmp~ convention: same-directory temp +
/// rename; failures clean up after themselves).
void writeJsonSidecar( const QString &path, const std::string &content )
{
    const QString tmpPath = path + QStringLiteral( ".tmp~" );
    QFile tmp( tmpPath );
    if ( !tmp.open( QIODevice::WriteOnly | QIODevice::Truncate )
         || tmp.write( content.c_str(), static_cast<qint64>( content.size() ) )
                != static_cast<qint64>( content.size() ) )
    {
        tmp.close();
        QFile::remove( tmpPath );
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to write the network sidecar: "
                                   + path.toStdString() );
    }
    tmp.close();
    // Windows: QFile::rename refuses to replace an existing target —
    // remove-then-rename keeps re-publishing the same sidecar working.
    if ( ( !QFile::exists( path ) || QFile::remove( path ) )
         && !QFile::rename( tmpPath, path ) )
    {
        QFile::remove( tmpPath );
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to publish the network sidecar (rename): "
                                   + path.toStdString() );
    }
}

} // anonymous namespace

Json::Value RsSarPairNetworkOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["scenes"] = makeStringParam(
        "scenes", "JSON array of scene truths: {acquisitionUtc|acquisitionUtcSec, "
                  "wavelengthUm, orbitStates, [azimuthStartUtc|azimuthStartUtcSec]}" );
    props["strategy"] = makeEnumParam( "strategy", "Pairing strategy",
                                       { "all_pairs", "consecutive" }, "all_pairs" );
    props["maxTemporalDays"] = makeNumberParam( "maxTemporalDays",
                                                "Temporal constraint (|Δt| days; "
                                                "unset = unlimited)" );
    props["minPerpendicularM"] = makeNumberParam( "minPerpendicularM",
                                                  "Minimum |B⊥| screening bound (m)" );
    props["maxPerpendicularM"] = makeNumberParam( "maxPerpendicularM",
                                                  "Maximum |B⊥| screening bound (m)" );
    props["referenceIdx"] = makeNumberParam( "referenceIdx",
                                             "Reference scene index in the "
                                             "time-sorted list", 0.0 );
    props["allowDisconnected"] = makeBooleanParam(
        "allowDisconnected", "Return a disconnected graph with its component map "
                             "instead of refusing (explicit QA mode)", false );
    props["outputFile"] = makeOutputParam( "outputFile",
                                           "Optional JSON sidecar path for the pair "
                                           "table + component map", "json" );

    Json::Value outputs( Json::objectValue );
    outputs["outputFile"] = makeOutputParam( "outputFile", "Pair network JSON", "json" );
    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "scenes" } );
    return root;
}

Json::Value RsSarPairNetworkOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Select which scene pairs form interferograms under baseline "
                      "constraints, before any heavy processing runs.";
    meta["prerequisites"].append( "Scene truth stack: orbit states + acquisition UTC "
                                  "+ one common wavelength." );
    meta["limitations"].append( "Screening B⊥ is evaluated at each master's orbit "
                                "mid-time nadir — a graph metric, not a per-pixel "
                                "baseline product." );
    meta["limitations"].append( "Bounded scale: 512 scenes, 65536 pairs (typed "
                                "refusals beyond)." );
    return meta;
}

Json::Value RsSarPairNetworkOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( 64ULL * 1024ULL * 1024ULL );
    return est;
}

Json::Value RsSarPairNetworkOperator::estimateExecution( const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarPairNetworkOperator::run( const Json::Value &params,
                                           RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const Json::Value &scenesJson = params["scenes"];
    if ( !scenesJson.isArray() || scenesJson.size() < 2 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "scenes must be a JSON array of at least 2 scene truths" );
    if ( scenesJson.size() > kMaxScenes )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "scenes exceed the network bound (" +
                                   std::to_string( kMaxScenes ) + ")" );

    sicnu::sar::PairNetworkParams netParams;
    const std::string strategy = getEnum( params, "strategy", { "all_pairs", "consecutive" },
                                          "all_pairs" );
    netParams.strategy = strategy == "consecutive" ? sicnu::sar::PairStrategy::Consecutive
                                                   : sicnu::sar::PairStrategy::AllPairs;
    if ( params.isMember( "maxTemporalDays" ) && params["maxTemporalDays"].isNumeric() )
        netParams.maxTemporalDays = getDouble( params, "maxTemporalDays", 0.0 );
    if ( params.isMember( "minPerpendicularM" ) && params["minPerpendicularM"].isNumeric() )
        netParams.minPerpendicularM = getDouble( params, "minPerpendicularM", 0.0 );
    if ( params.isMember( "maxPerpendicularM" ) && params["maxPerpendicularM"].isNumeric() )
        netParams.maxPerpendicularM = getDouble( params, "maxPerpendicularM", 0.0 );
    netParams.referenceIdx = getInt( params, "referenceIdx", 0 );
    netParams.allowDisconnected = getBool( params, "allowDisconnected", false );
    const std::string outputFile = getString( params, "outputFile", std::string() );

    // Parse + sort by acquisition UTC (the network pairs in calendar order).
    std::vector<sicnu::sar::InSarSceneTruth> scenes;
    scenes.reserve( scenesJson.size() );
    for ( Json::ArrayIndex i = 0; i < scenesJson.size(); ++i )
        scenes.push_back( parseSceneJson( scenesJson[i], static_cast<int>( i ) ) );
    std::vector<int> order( scenes.size() );
    std::iota( order.begin(), order.end(), 0 );
    std::stable_sort( order.begin(), order.end(), [ &scenes ]( int a, int b ) {
        return scenes[static_cast<size_t>( a )].acquisitionUtcSec
               < scenes[static_cast<size_t>( b )].acquisitionUtcSec;
    } );
    std::vector<sicnu::sar::InSarSceneTruth> sorted;
    sorted.reserve( scenes.size() );
    for ( const int idx : order )
        sorted.push_back( scenes[static_cast<size_t>( idx )] );
    // Map the caller's referenceIdx through the sort (stable → deterministic).
    int referenceIdx = netParams.referenceIdx;
    for ( size_t pos = 0; pos < order.size(); ++pos )
        if ( order[pos] == netParams.referenceIdx )
            referenceIdx = static_cast<int>( pos );
    netParams.referenceIdx = referenceIdx;

    sicnu::sar::PairNetworkResult network;
    QString error;
    context.throwIfCancelled();
    if ( !sicnu::sar::buildPairNetwork( sorted, netParams, &network, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData, error.toStdString() );

    // Result JSON (scene indices refer to the TIME-SORTED list; the sort
    // map is returned so callers can trace back).
    Json::Value json;
    json["connected"] = network.connected;
    json["componentCount"] = network.componentCount;
    json["referenceIdx"] = network.referenceIdx;
    json["maxPerpendicularSeenM"] = network.maxPerpendicularSeenM;
    json["maxTemporalSeenDays"] = network.maxTemporalSeenDays;
    json["truthVersion"] = sicnu::sar::kInSarTruthVersion;
    Json::Value sortMap( Json::arrayValue );
    for ( const int idx : order )
        sortMap.append( idx );
    json["timeSortedFrom"] = sortMap;

    Json::Value components( Json::arrayValue );
    for ( const int label : network.componentOfScene )
        components.append( label );
    json["componentOfScene"] = components;

    Json::Value pairsJson( Json::arrayValue );
    for ( const sicnu::sar::InSarNetworkPair &pair : network.pairs )
    {
        Json::Value p( Json::objectValue );
        p["master"] = pair.masterIdx;
        p["slave"] = pair.slaveIdx;
        p["temporalDays"] = pair.temporalDays;
        p["perpendicularM"] = pair.perpendicularM;
        p["parallelM"] = pair.parallelM;
        p["magnitudeM"] = pair.magnitudeM;
        pairsJson.append( p );
    }
    json["pairs"] = pairsJson;

    if ( !outputFile.empty() )
    {
        Json::StreamWriterBuilder builder;
        builder[ "indentation" ] = "  ";
        writeJsonSidecar( QString::fromStdString( outputFile ),
                          Json::writeString( builder, json ) );
        json["outputFile"] = outputFile;
    }

    context.reportProgress( 1.0, "Pair network complete" );
    return json;
}

} // namespace sicnu::operators::rs
