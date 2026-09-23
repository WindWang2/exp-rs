// tests/test_verifier_provenance.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice D: provenance.complete,
// reproducibility.digest and cross.output.consistency families.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

class FakeArtifactProbe : public IArtifactProbe
{
  public:
    std::map<std::string, ArtifactInfo> artifacts;
    std::map<std::string, Json::Value> documents;

    std::optional<ArtifactInfo> probe( const std::string &path ) override
    {
        const auto found = artifacts.find( path );
        if ( found == artifacts.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> readJson( const std::string &path ) override
    {
        const auto found = documents.find( path );
        if ( found == documents.end() )
            return std::nullopt;
        return found->second;
    }
};

class FakeGridProbe : public IGridProbe
{
  public:
    std::map<std::string, GridInfo> grids;

    std::optional<GridInfo> grid( const std::string &path ) override
    {
        const auto found = grids.find( path );
        if ( found == grids.end() )
            return std::nullopt;
        return found->second;
    }
};

class FakeProvenance : public IProvenanceView
{
  public:
    std::map<std::string, Json::Value> byPath;
    std::map<std::string, Json::Value> byRun;

    std::optional<Json::Value> provenanceForPath( const std::string &path ) override
    {
        const auto found = byPath.find( path );
        if ( found == byPath.end() )
            return std::nullopt;
        return found->second;
    }
    std::optional<Json::Value> provenanceForRun( const std::string &runId ) override
    {
        const auto found = byRun.find( runId );
        if ( found == byRun.end() )
            return std::nullopt;
        return found->second;
    }
};

ArtifactInfo fileInfo( bool exists, const std::string &kind, const std::string &digest = "" )
{
    ArtifactInfo info;
    info.exists = exists;
    info.kind = kind;
    info.sizeBytes = 10;
    info.digest = digest;
    return info;
}

} // namespace

// ---------------------------------------------------------------------------
// provenance.complete
// ---------------------------------------------------------------------------

TEST_CASE( "provenance completeness judges fields and dimensions", "[verify][provenance][D]" )
{
    FakeProvenance provenance;
    Json::Value document( Json::objectValue );
    document["schema"] = "exp-rs-prov/1";
    document["tool"] = "ihs_fusion";
    Json::Value dimensions( Json::objectValue );
    dimensions["time"] = "2026-09-24";
    dimensions["region"] = "scene-17";
    document["dimensions"] = dimensions;
    provenance.byPath["out.tif"] = document;

    const auto provCheck = [ & ]( Json::Value params ) {
        VerificationCheckSpec check;
        check.checkId = "pv";
        check.kind = "provenance.complete";
        check.params = std::move( params );
        return evaluateCheck( check, VerificationContext{ nullptr, nullptr, nullptr, &provenance, nullptr } );
    };

    Json::Value params( Json::objectValue );
    params["path"] = "out.tif";
    Json::Value fields( Json::arrayValue );
    fields.append( "tool" );
    params["requiredFields"] = fields;
    Json::Value dims( Json::arrayValue );
    dims.append( "time" );
    params["requiredDimensions"] = dims;
    REQUIRE( provCheck( params ).status == VerificationStatus::Pass );

    SECTION( "missing field is a detected FAIL (the #1191 ruling)" )
    {
        fields.append( "operator" );
        params["requiredFields"] = fields;
        const VerificationCheckResult result = provCheck( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeProvenanceIncomplete );
    }
    SECTION( "missing dimension fails" )
    {
        dims.append("sensor");
        params["requiredDimensions"] = dims;
        REQUIRE( provCheck( params ).code == kCodeProvenanceIncomplete );
    }
    SECTION( "a missing DOCUMENT is a detected violation, not a capability gap" )
    {
        params["path"] = "other.tif";
        const VerificationCheckResult result = provCheck( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeProvenanceIncomplete );
    }
    SECTION( "an unreadable (non-object) document is indeterminate" )
    {
        provenance.byPath["out.tif"] = Json::Value( Json::arrayValue );
        const VerificationCheckResult result = provCheck( params );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeProvenanceMissing );
    }
    SECTION( "runId form" )
    {
        params.removeMember( "path" );
        params["runId"] = "run-9";
        params["requiredFields"] = fields;
        params["requiredDimensions"] = dims;
        const VerificationCheckResult missing = provCheck( params );
        REQUIRE( missing.status == VerificationStatus::Fail );
        provenance.byRun["run-9"] = document;
        REQUIRE( provCheck( params ).status == VerificationStatus::Pass );
    }
}

// ---------------------------------------------------------------------------
// reproducibility.digest
// ---------------------------------------------------------------------------

TEST_CASE( "reproducibility digest matches pinned and pair forms", "[verify][provenance][D]" )
{
    const std::string digestA( 64, 'a' );
    const std::string digestB( 64, 'b' );
    FakeArtifactProbe artifacts;
    artifacts.artifacts["run1.tif"] = fileInfo( true, "raster", digestA );
    artifacts.artifacts["run2.tif"] = fileInfo( true, "raster", digestA );
    artifacts.artifacts["run3.tif"] = fileInfo( true, "raster", digestB );
    artifacts.artifacts["nodigest.tif"] = fileInfo( true, "raster", "" );
    artifacts.artifacts["ghost.tif"] = fileInfo( false, "raster" );

    const auto repro = [ & ]( Json::Value params ) {
        VerificationCheckSpec check;
        check.checkId = "rp";
        check.kind = "reproducibility.digest";
        check.params = std::move( params );
        return evaluateCheck( check, VerificationContext{ &artifacts, nullptr, nullptr, nullptr, nullptr } );
    };

    SECTION( "pinned expectation" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "run1.tif";
        params["expectedDigest"] = digestA;
        REQUIRE( repro( params ).status == VerificationStatus::Pass );

        params["expectedDigest"] = digestB;
        const VerificationCheckResult result = repro( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeDigestMismatch );
    }
    SECTION( "pair comparison" )
    {
        Json::Value params( Json::objectValue );
        params["leftPath"] = "run1.tif";
        params["rightPath"] = "run2.tif";
        REQUIRE( repro( params ).status == VerificationStatus::Pass );
        params["rightPath"] = "run3.tif";
        REQUIRE( repro( params ).code == kCodeDigestMismatch );
    }
    SECTION( "honest indeterminacy beats a guess" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "nodigest.tif";
        params["expectedDigest"] = digestA;
        const VerificationCheckResult noDigest = repro( params );
        REQUIRE( noDigest.status == VerificationStatus::Indeterminate );
        REQUIRE( noDigest.code == kCodeDigestUnavailable );

        Json::Value pair( Json::objectValue );
        pair["leftPath"] = "run1.tif";
        pair["rightPath"] = "nodigest.tif";
        REQUIRE( repro( pair ).code == kCodeDigestUnavailable );
    }
    SECTION( "missing artifact in either form fails" )
    {
        Json::Value params( Json::objectValue );
        params["path"] = "ghost.tif";
        params["expectedDigest"] = digestA;
        REQUIRE( repro( params ).code == kCodeArtifactMissing );

        Json::Value pair( Json::objectValue );
        pair["leftPath"] = "run1.tif";
        pair["rightPath"] = "ghost.tif";
        REQUIRE( repro( pair ).code == kCodeArtifactMissing );
    }
}

// ---------------------------------------------------------------------------
// cross.output.consistency
// ---------------------------------------------------------------------------

TEST_CASE( "cross-output consistency judges grid equality across outputs",
           "[verify][provenance][D]" )
{
    FakeArtifactProbe artifacts;
    FakeGridProbe grids;
    const auto addOutput = [ & ]( const std::string &path, int width, int height, const std::string &crs,
                                  int bands, const std::string &digest = std::string( 64, 'c' ) ) {
        artifacts.artifacts[path] = fileInfo( true, "raster", digest );
        GridInfo grid;
        grid.width = width;
        grid.height = height;
        grid.bandCount = bands;
        grid.crs = crs;
        grids.grids[path] = grid;
    };
    addOutput( "ndvi.tif", 512, 256, "EPSG:32650", 1 );
    addOutput( "water.tif", 512, 256, "EPSG:32650", 1 );
    // A definitive non-existence answer (the adapter can stat the file):
    // this is a detected violation, unlike an unprobeable path.
    artifacts.artifacts["ghost.tif"] = fileInfo( false, "raster" );

    const auto cross = [ & ]( Json::Value params ) {
        VerificationCheckSpec check;
        check.checkId = "co";
        check.kind = "cross.output.consistency";
        check.params = std::move( params );
        return evaluateCheck( check, VerificationContext{ &artifacts, &grids, nullptr, nullptr, nullptr } );
    };

    Json::Value params( Json::objectValue );
    Json::Value outputs( Json::arrayValue );
    Json::Value o1( Json::objectValue );
    o1["path"] = "ndvi.tif";
    Json::Value o2( Json::objectValue );
    o2["path"] = "water.tif";
    outputs.append( o1 );
    outputs.append( o2 );
    params["outputs"] = outputs;
    params["sameGrid"] = true;
    params["sameCrs"] = true;
    params["bandCount"] = 1;
    REQUIRE( cross( params ).status == VerificationStatus::Pass );

    SECTION( "grid divergence" )
    {
        addOutput( "water.tif", 256, 256, "EPSG:32650", 1 );
        const VerificationCheckResult result = cross( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeCrossOutputInconsistent );
    }
    SECTION( "crs divergence" )
    {
        addOutput( "water.tif", 512, 256, "EPSG:4326", 1 );
        REQUIRE( cross( params ).code == kCodeCrossOutputInconsistent );
    }
    SECTION( "band divergence" )
    {
        addOutput( "water.tif", 512, 256, "EPSG:32650", 3 );
        REQUIRE( cross( params ).code == kCodeCrossOutputInconsistent );
    }
    SECTION( "divergence in ANY pair is caught, not only adjacent ones" )
    {
        addOutput( "third.tif", 512, 256, "EPSG:32650", 1 );
        Json::Value o3( Json::objectValue );
        o3["path"] = "third.tif";
        outputs.append( o3 );
        params["outputs"] = outputs;
        params.removeMember( "bandCount" );
        REQUIRE( cross( params ).status == VerificationStatus::Pass );
        addOutput( "third.tif", 999, 256, "EPSG:32650", 1 );
        const VerificationCheckResult result = cross( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeCrossOutputInconsistent );
    }
    SECTION( "a missing output is a detected violation" )
    {
        outputs[1] = [ & ] {
            Json::Value o( Json::objectValue );
            o["path"] = "ghost.tif";
            return o;
        }();
        params["outputs"] = outputs;
        params.removeMember( "bandCount" );
        const VerificationCheckResult result = cross( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeArtifactMissing );
    }
    SECTION( "an unprobeable output cannot pass the consistency claim" )
    {
        outputs[1] = [ & ] {
            Json::Value o( Json::objectValue );
            o["path"] = "locked.tif";
            return o;
        }();
        params["outputs"] = outputs;
        params.removeMember( "bandCount" );
        const VerificationCheckResult result = cross( params );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeArtifactUnreadable );
    }
    SECTION( "sameCrs refuses a CRS-less output set" )
    {
        addOutput( "ndvi.tif", 512, 256, "", 1 );
        addOutput( "water.tif", 512, 256, "", 1 );
        params.removeMember( "bandCount" );
        const VerificationCheckResult result = cross( params );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeCrossOutputInconsistent );
    }
}
