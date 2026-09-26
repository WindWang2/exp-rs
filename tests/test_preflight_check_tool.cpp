// tests/test_preflight_check_tool.cpp — Track 04: the preflight:check
// data-platform tool, dispatched through the real surface.
//
// Live oracles (nothing handwritten into the engine):
//   * capability parity — the tool consumes the harness CapabilityKnowledge:
//     a variant edit in the capability DOCUMENT (blue demanded, then not)
//     flips the runtime verdict with no code change;
//   * a blocked verdict survives an acknowledgement of the block code;
//   * temporal collections from the workspace catalog flow in as facts:
//     out-of-order dates block (SPF_TEMPORAL_ORDER_INVALID), observed basis;
//   * the same input is byte-stable; report/teaching/agent agree.

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <json/json.h>

#include "agent/data_platform_tools.h"
#include "agent/harness/capability_knowledge.h"
#include "data/data_manager.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_time.h"
#include "processing/algorithms/temporal/temporal_workspace.h"

#include <optional>
#include <string>

namespace {

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_preflight_check_tool";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
    if ( !QCoreApplication::instance() )
        new QCoreApplication( appArgc(), appArgv );
}

QString jsonToText( const Json::Value &value )
{
    return QString::fromStdString( Json::writeString( Json::StreamWriterBuilder(), value ) );
}

/// One spectral-index capability entry in the real document shape: variants
/// are the parameter-conditional requirements (blue for EVI only).
Json::Value capabilityDocument( bool eviDemandsBlue )
{
    Json::Value entry( Json::objectValue );
    entry["id"] = "rs:checkdemo";
    entry["family"] = "spectral_index";
    Json::Value modality( Json::arrayValue );
    modality.append( "optical" );
    entry["modality"] = modality;
    Json::Value radiometric( Json::objectValue );
    Json::Value acceptable( Json::arrayValue );
    acceptable.append( "surface_reflectance" );
    radiometric["acceptable"] = acceptable;
    entry["radiometric"] = radiometric;
    Json::Value variants( Json::arrayValue );
    for ( const auto &indexName : { "NDVI", "EVI" } )
    {
        Json::Value variant( Json::objectValue );
        Json::Value when( Json::objectValue );
        when["param"] = "index";
        Json::Value values( Json::arrayValue );
        values.append( indexName );
        when["values"] = values;
        variant["when"] = when;
        variant["band_roles"]["red"] = 1;
        variant["band_roles"]["nir"] = 1;
        if ( std::string( indexName ) == "EVI" && eviDemandsBlue )
            variant["band_roles"]["blue"] = 1;
        variants.append( variant );
    }
    entry["variants"] = variants;

    Json::Value document( Json::arrayValue );
    document.append( entry );
    return document;
}

/// Entry with a temporal policy on the NDVI variant (series demands).
Json::Value temporalCapabilityDocument()
{
    Json::Value entry( Json::objectValue );
    entry["id"] = "rs:checkdemo";
    entry["family"] = "spectral_index";
    Json::Value variants( Json::arrayValue );
    Json::Value variant( Json::objectValue );
    Json::Value when( Json::objectValue );
    when["param"] = "index";
    Json::Value values( Json::arrayValue );
    values.append( "NDVI" );
    when["values"] = values;
    variant["when"] = when;
    variant["band_roles"]["red"] = 1;
    variant["band_roles"]["nir"] = 1;
    Json::Value temporal( Json::objectValue );
    temporal["min_scenes"] = 2;
    temporal["requires_acquisition_time"] = true;
    temporal["max_gap_days"] = 40;
    variant["temporal"] = temporal;
    variants.append( variant );
    entry["variants"] = variants;
    Json::Value document( Json::arrayValue );
    document.append( entry );
    return document;
}

void writeCapabilityDoc( const QString &path, const Json::Value &document )
{
    QFile doc( path );
    REQUIRE( doc.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    doc.write( jsonToText( document ).toUtf8() );
    doc.close();
}

/// A canonical sicnu.asset_state.v1 passport for an S2-like red+nir scene.
QJsonObject passportObject( const std::string &assetId )
{
    Json::Value passport( Json::objectValue );
    passport["schema"] = "sicnu.asset_state.v1";
    Json::Value identity( Json::objectValue );
    identity["asset_id"] = assetId;
    identity["revision"] = "r1";
    identity["source_path"] = "/data/" + assetId + ".tif";
    identity["kind"] = "raster";
    identity["lifecycle"] = "ready";
    passport["identity"] = identity;
    Json::Value sensor( Json::objectValue );
    sensor["platform"] = "S2";
    sensor["instrument"] = "MSI";
    sensor["modality"] = "optical";
    passport["sensor"] = sensor;
    Json::Value acquisition( Json::objectValue );
    acquisition["time_iso"] = "2026-05-01T10:30:00Z";
    acquisition["valid"] = true;
    passport["acquisition"] = acquisition;
    Json::Value bands( Json::arrayValue );
    Json::Value red( Json::objectValue );
    red["index"] = 1;
    red["name"] = "B04";
    red["role"] = "red";
    red["data_type"] = "UInt16";
    red["wavelength_nm"] = 665.0;
    bands.append( red );
    Json::Value nir( Json::objectValue );
    nir["index"] = 2;
    nir["name"] = "B08";
    nir["role"] = "nir";
    nir["data_type"] = "UInt16";
    nir["wavelength_nm"] = 842.0;
    bands.append( nir );
    passport["bands"] = bands;
    Json::Value geometry( Json::objectValue );
    geometry["has_crs"] = true;
    geometry["crs_authid"] = "EPSG:32650";
    geometry["crs_projected"] = true;
    geometry["pixel_size_x"] = 10.0;
    geometry["pixel_size_y"] = 10.0;
    geometry["width"] = 10980;
    geometry["height"] = 10980;
    passport["geometry"] = geometry;
    Json::Value validity( Json::objectValue );
    validity["no_data_policy"] = "declared";
    passport["validity"] = validity;
    Json::Value radiometric( Json::objectValue );
    radiometric["unit"] = "surface_reflectance";
    passport["radiometric"] = radiometric;
    Json::Value provenance( Json::objectValue );
    provenance["is_derived"] = false;
    passport["provenance"] = provenance;
    const QString text = jsonToText( passport );
    return QJsonDocument::fromJson( text.toUtf8() ).object();
}

/// Tool arguments with one inline passport input; optional acks.
QVariantMap checkArgsVariant( const std::string &index, const QJsonObject &passport,
                              const QStringList &acks = {} )
{
    QVariantMap args;
    args.insert( QStringLiteral( "operator" ), QStringLiteral( "rs:checkdemo" ) );
    args.insert( QStringLiteral( "human_operator_id" ), QStringLiteral( "op-1" ) );
    QVariantMap params;
    params.insert( QStringLiteral( "index" ), QString::fromStdString( index ) );
    args.insert( QStringLiteral( "operator_params" ), params );

    QVariantList inputs;
    QVariantMap input;
    input.insert( QStringLiteral( "slot" ), QStringLiteral( "primary" ) );
    input.insert( QStringLiteral( "ref" ), QStringLiteral( "scene-a" ) );
    input.insert( QStringLiteral( "passport_json" ),
                  QString::fromUtf8( QJsonDocument( passport ).toJson( QJsonDocument::Compact ) ) );
    inputs.append( input );
    args.insert( QStringLiteral( "inputs" ), inputs );

    if ( !acks.isEmpty() )
    {
        QVariantList ackList;
        for ( const auto &code : acks )
            ackList.append( code );
        args.insert( QStringLiteral( "acknowledgements" ), ackList );
    }
    return args;
}

QVariantMap runCheck( const QVariantMap &args )
{
    return sicnu::agent::handleDataPlatformTool( QStringLiteral( "preflight:check" ), args );
}

/// Canonical bytes of a tool result (sorted keys, compact) — the byte
/// stability oracle.
std::string canonical( const QVariantMap &result )
{
    return QJsonDocument( QJsonObject::fromVariantMap( result ) )
        .toJson( QJsonDocument::Compact )
        .toStdString();
}

std::string jsonStr( const QVariantMap &result, const char *section, const char *key )
{
    return QJsonObject::fromVariantMap( result )
        .value( QLatin1String( section ) )
        .toObject()
        .value( QLatin1String( key ) )
        .toString()
        .toStdString();
}

QVariantList findingList( const QVariantMap &result )
{
    return result.value( QStringLiteral( "report" ) ).toMap()
        .value( QStringLiteral( "findings" ) )
        .toList();
}

/// Capability directory fixture: a temp dir holding the checkdemo document,
/// with the harness knowledge pointed at it and reloaded.
struct CapabilityFixture
{
    QTemporaryDir dir;
    QString docPath;

    CapabilityFixture()
    {
        const QString capabilitiesDir = dir.filePath( QStringLiteral( "capabilities" ) );
        REQUIRE( QDir( capabilitiesDir ).mkpath( QStringLiteral( "." ) ) );
        docPath = capabilitiesDir + QStringLiteral( "/checkdemo.json" );
        auto &knowledge = sicnu::agent::harness::CapabilityKnowledge::instance();
        knowledge.setDirectory( capabilitiesDir.toStdString() );
    }

    /// Rewrites the document and reloads the authority; returns entries loaded.
    int publish( const Json::Value &document )
    {
        writeCapabilityDoc( docPath, document );
        const int loaded = reload();
        REQUIRE( loaded == 1 );
        return loaded;
    }

    int reload()
    {
        return sicnu::agent::harness::CapabilityKnowledge::instance().reload();
    }
};

/// RAII guard for the process-wide workspace catalog: a REQUIRE failure
/// between set and restore must not leave the global pointing at a dead
/// stack DataManager for later tests in this binary.
struct CatalogGuard
{
    explicit CatalogGuard( sicnu::data::DataManager *dm ) { set( dm ); }
    ~CatalogGuard() { set( nullptr ); }
    CatalogGuard( const CatalogGuard & ) = delete;
    CatalogGuard &operator=( const CatalogGuard & ) = delete;

    void set( sicnu::data::DataManager *dm ) { sicnu::temporal::setWorkspaceCatalog( dm ); }
};

} // namespace

TEST_CASE( "preflight:check consumes live CapabilityKnowledge (variant parity)",
           "[preflight][tool]" )
{
    ensureApp();
    CapabilityFixture fixture;

    // 1) EVI demands blue; the passport has red+nir only -> blocked.
    fixture.publish( capabilityDocument( true ) );
    const QVariantMap blocked =
        runCheck( checkArgsVariant( "EVI", passportObject( "asset-demo" ) ) );
    REQUIRE( jsonStr( blocked, "report", "verdict" ) == "blocked" );
    bool sawBandRoleMissing = false;
    for ( const auto &f : findingList( blocked ) )
        if ( f.toMap().value( QStringLiteral( "code" ) ).toString() ==
             QStringLiteral( "SPF_BAND_ROLE_MISSING" ) )
            sawBandRoleMissing = true;
    REQUIRE( sawBandRoleMissing );

    // 2) The AUTHORITY document changes (blue demand dropped); the runtime
    //    verdict flips with NO code or adapter change — parity by injection.
    fixture.publish( capabilityDocument( false ) );
    const QVariantMap ok = runCheck( checkArgsVariant( "EVI", passportObject( "asset-demo" ) ) );
    REQUIRE( jsonStr( ok, "report", "verdict" ) == "ok" );

    // 3) NDVI is unaffected throughout (first matching variant only).
    fixture.publish( capabilityDocument( true ) );
    const QVariantMap ndvi = runCheck( checkArgsVariant( "NDVI", passportObject( "asset-demo" ) ) );
    REQUIRE( jsonStr( ndvi, "report", "verdict" ) == "ok" );
}

TEST_CASE( "preflight:check: a block survives its own acknowledgement; the three "
           "projections agree; repeats are byte-stable",
           "[preflight][tool]" )
{
    ensureApp();
    CapabilityFixture fixture;
    fixture.publish( capabilityDocument( true ) );

    const QVariantMap blocked =
        runCheck( checkArgsVariant( "EVI", passportObject( "asset-demo" ),
                                    { QStringLiteral( "SPF_BAND_ROLE_MISSING" ) } ) );
    // Acknowledging the block code cannot flip the verdict.
    REQUIRE( jsonStr( blocked, "report", "verdict" ) == "blocked" );

    // Three projections of the ONE report.
    const std::string verdict = jsonStr( blocked, "report", "verdict" );
    REQUIRE( jsonStr( blocked, "teaching", "verdict" ) == verdict );
    REQUIRE( jsonStr( blocked, "agent", "verdict" ) == verdict );
    const std::string digest = jsonStr( blocked, "report", "request_digest" );
    REQUIRE( jsonStr( blocked, "teaching", "request_digest" ) == digest );
    REQUIRE( jsonStr( blocked, "agent", "request_digest" ) == digest );

    // Byte stability across repeated invocations.
    const QVariantMap again =
        runCheck( checkArgsVariant( "EVI", passportObject( "asset-demo" ),
                                    { QStringLiteral( "SPF_BAND_ROLE_MISSING" ) } ) );
    REQUIRE( canonical( blocked ) == canonical( again ) );

    // The teaching items and agent judgments carry the same finding multiset.
    const QVariantList findings = findingList( blocked );
    const QVariantList items = blocked.value( QStringLiteral( "teaching" ) ).toMap()
                                   .value( QStringLiteral( "items" ) )
                                   .toList();
    const QVariantList judgments = blocked.value( QStringLiteral( "agent" ) ).toMap()
                                       .value( QStringLiteral( "judgments" ) )
                                       .toList();
    REQUIRE( findings.size() == items.size() );
    REQUIRE( findings.size() == judgments.size() );
    REQUIRE( findings.size() > 0 );
}

TEST_CASE( "preflight:check resolves temporal collections from the workspace "
           "catalog into typed series findings",
           "[preflight][tool][temporal]" )
{
    ensureApp();
    CapabilityFixture fixture;
    fixture.publish( temporalCapabilityDocument() );

    // The runtime temporal authority normalizes scene order at parse time
    // (TemporalCollection::fromJson sorts), so the adversarial series that
    // CAN reach the engine through this surface are oversized gaps and
    // scenes without a parseable time. Unsorted declared lists are covered
    // by the Qt-free TemporalFactsProvider oracle.
    auto makeCollection = []( sicnu::data::DataManager &dm, const QString &name,
                              std::vector<std::pair<const char *, bool>> scenes ) {
        sicnu::temporal::TemporalCollection collection;
        for ( const auto &[ date, valid ] : scenes )
        {
            sicnu::temporal::TemporalSceneRef scene;
            scene.path = QStringLiteral( "/data/scene.tif" );
            scene.time = valid ? sicnu::temporal::parseAcquisitionTime( QLatin1String( date ) )
                               : sicnu::temporal::AcquisitionTime{};
            scene.originalIndex = static_cast<int>( collection.scenes().size() );
            collection.scenes().append( scene );
        }
        const std::string descriptorText =
            Json::writeString( Json::StreamWriterBuilder(), collection.toJson() );
        sicnu::data::TemporalCollectionCreateRequest request;
        request.displayName = name;
        request.descriptor = QString::fromStdString( descriptorText );
        const auto created = dm.createTemporalCollection( request );
        REQUIRE( created.diagnostics.isEmpty() );
        return created.collectionId.toString();
    };

    // Series with parseable, ordered dates but a 59-day gap against a
    // 40-day budget: SPF_TEMPORAL_GAP_EXCEEDED (require_ack, observed).
    QJsonObject gappedPassport = passportObject( "asset-series" );
    QJsonObject invalidPassport = passportObject( "asset-series-bad" );
    {
        sicnu::data::DataManager dm;
        const CatalogGuard catalogGuard( &dm );
        const QString gappedId = makeCollection(
            dm, QStringLiteral( "checkdemo-gap" ),
            { { "2026-01-01", true }, { "2026-01-15", true }, { "2026-03-15", true } } );
        const QString invalidId = makeCollection(
            dm, QStringLiteral( "checkdemo-invalid" ),
            { { "2026-01-01", true }, { "not-a-date", false }, { "2026-02-01", true } } );

        QJsonArray refs;
        QJsonObject ref;
        ref.insert( QLatin1String( "collection_id" ), gappedId );
        ref.insert( QLatin1String( "role" ), QLatin1String( "series" ) );
        refs.append( ref );
        QJsonObject temporalObj;
        temporalObj.insert( QLatin1String( "present" ), true );
        temporalObj.insert( QLatin1String( "refs" ), refs );
        gappedPassport.insert( QLatin1String( "temporal" ), temporalObj );

        QJsonArray refs2;
        QJsonObject ref2;
        ref2.insert( QLatin1String( "collection_id" ), invalidId );
        ref2.insert( QLatin1String( "role" ), QLatin1String( "series" ) );
        refs2.append( ref2 );
        QJsonObject temporalObj2;
        temporalObj2.insert( QLatin1String( "present" ), true );
        temporalObj2.insert( QLatin1String( "refs" ), refs2 );
        invalidPassport.insert( QLatin1String( "temporal" ), temporalObj2 );

        const QVariantMap gapRun = runCheck( checkArgsVariant( "NDVI", gappedPassport ) );
        REQUIRE( jsonStr( gapRun, "report", "verdict" ) == "requires_ack" );
        bool sawGap = false;
        for ( const auto &f : findingList( gapRun ) )
        {
            const QVariantMap fm = f.toMap();
            if ( fm.value( QStringLiteral( "code" ) ).toString() ==
                 QStringLiteral( "SPF_TEMPORAL_GAP_EXCEEDED" ) )
            {
                sawGap = true;
                REQUIRE( fm.value( QStringLiteral( "basis" ) ).toString() ==
                         QLatin1String( "observed" ) );
                REQUIRE( fm.value( QStringLiteral( "evidence" ) ).toMap()
                             .value( QStringLiteral( "max_gap_days_found" ) )
                             .toInt() == 59 );
            }
        }
        REQUIRE( sawGap );

        const QVariantMap invalidRun = runCheck( checkArgsVariant( "NDVI", invalidPassport ) );
        REQUIRE( jsonStr( invalidRun, "report", "verdict" ) == "requires_ack" );
        bool sawIncomplete = false;
        for ( const auto &f : findingList( invalidRun ) )
        {
            const QVariantMap fm = f.toMap();
            if ( fm.value( QStringLiteral( "code" ) ).toString() ==
                 QStringLiteral( "SPF_TEMPORAL_TIME_INCOMPLETE" ) )
            {
                sawIncomplete = true;
                REQUIRE( fm.value( QStringLiteral( "evidence" ) ).toMap()
                             .value( QStringLiteral( "invalid_time_scenes" ) )
                             .toInt() == 1 );
                REQUIRE( fm.value( QStringLiteral( "evidence" ) ).toMap()
                             .value( QStringLiteral( "declared_scenes" ) )
                             .toInt() == 3 );
            }
        }
        REQUIRE( sawIncomplete );
    }
}

TEST_CASE( "preflight:check rejects malformed arguments with typed failures",
           "[preflight][tool]" )
{
    ensureApp();
    QVariantMap noOperator;
    QVariantList inputs;
    QVariantMap input;
    input.insert( QStringLiteral( "ref" ), QStringLiteral( "scene-a" ) );
    inputs.append( input );
    noOperator.insert( QStringLiteral( "inputs" ), inputs );
    REQUIRE_THROWS_AS( runCheck( noOperator ), std::runtime_error );
}
