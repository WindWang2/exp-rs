// tests/test_r26_capability_family_drift.cpp
//
// R26-harness: capability family classification drift oracles.
//
// The authored familyMap claims to be "complete over the rs: operators",
// but the planner's live slotTable and the contracts registry gained
// operators the map never learned: canonicalFamily then falls back to the
// group table, and for groups the table does not know (e.g. "geometric")
// the operator is silently classified "io". Misclassified families corrupt
// every family-keyed retrieval surface (byFamily, capability pages, tool
// shortlists).
//
//   F1 — explicit pins: every slotTable operator missing from familyMap
//        resolves to its science family (register/stack-register are
//        raster_spatial like rs:align, rs:change is change, …).
//   F2 — drift guard: no production capability sidecar may fall through to
//        the unknown-group "io" default (an operator whose group the table
//        does not know must be pinned in familyMap instead).

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <json/json.h>
#include <json/reader.h>

#include "agent/harness/capability_catalog.h"

#include <string>
#include <vector>

using namespace sicnu::agent::harness;

namespace
{

struct FamilyPin
{
    const char *operatorId;
    const char *group;   // the sidecar's derived operator_group
    const char *family;  // the science family the map must return
};

/// Every planner slotTable operator the authored familyMap was missing.
/// Groups/families mirror data/processing/algorithm_meta/capability sidecars.
const std::vector<FamilyPin> kPins = {
    { "rs:register_images", "geometric", "raster_spatial" },
    { "rs:stack_register", "geometric", "raster_spatial" },
    { "rs:change", "ml", "change" },
    { "rs:classify", "ml", "classification" },
    { "rs:local_rx_anomaly", "spectral", "hyperspectral" },
    { "rs:sar_coregister", "sar", "sar" },
    { "rs:sar_coregister_local", "sar", "sar" },
    { "rs:temporal_harmonic_breaks", "temporal", "temporal" },
    { "rs:temporal_region_features", "temporal", "temporal" },
    { "rs:temporal_regularize", "temporal", "temporal" },
};

} // namespace

TEST_CASE( "F1: slotTable operators resolve to their science families",
           "[harness][r26][family]" )
{
    for ( const FamilyPin &pin : kPins )
    {
        INFO( "operator: " << pin.operatorId << " group: " << pin.group );
        CHECK( canonicalFamily( pin.operatorId, pin.group ) == pin.family );
    }
}

TEST_CASE( "F2: no capability sidecar falls through to the unknown-group io default",
           "[harness][r26][family][drift]" )
{
    const QDir dir( QString::fromStdString( std::string( CMAKE_SOURCE_DIR )
                                             + "/data/processing/algorithm_meta/capability" ) );
    REQUIRE( dir.exists() );
    const auto entries = dir.entryList( { "*.json" }, QDir::Files );

    std::vector<std::string> silentlyIo;
    for ( const QString &entry : entries )
    {
        QFile file( dir.filePath( entry ) );
        if ( !file.open( QIODevice::ReadOnly ) )
            continue;
        const QByteArray raw = file.readAll();
        Json::Value doc;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &doc, &errors ) )
            continue;

        const std::string id = doc.get( "id", "" ).asString();
        const std::string group =
          doc.get( "capability", Json::Value() ).get( "operator_group", "" ).asString();
        if ( id.empty() || group.empty() )
            continue;
        // "io" is a legitimate resolution only for the data-formats group;
        // every other "io" is the unknown-group default — silent
        // misclassification.
        if ( group != "data-formats" && canonicalFamily( id, group ) == "io" )
            silentlyIo.push_back( id + " (group " + group + ")" );
    }

    INFO( "silently-io operators: "
          << [&] {
                   std::string joined;
                   for ( const std::string &id : silentlyIo )
                       joined += id + "; ";
                   return joined;
               }() );
    CHECK( silentlyIo.empty() );
}
