// test_spectral_table.cpp — spectral table artifact + wavelength grid contract
//
// Covers:
//   - round-trip save/load with digest stamping (SpectralTable),
//   - digest stability (same content -> same digest; any cell change -> new
//     digest) and tamper refusal on load,
//   - structural validation: width mismatch, non-finite cells, grid rules,
//   - the measured-table provenance/license rule (machine-checkable),
//   - the cell-count bound refusal,
//   - wavelength unit normalization (nm/um/micro-sign, unknown refusal) and
//     grid rules (strict increase, FWHM pairing, disjoint-range detection)
//     (SpectralWavelength).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "processing/algorithms/spectral_table.h"
#include "processing/algorithms/spectral_wavelength.h"

#include <cmath>
#include <utility>
#include <vector>

using Catch::Approx;
using SpectralTable::Table;

namespace
{

Table makeTable( int count = 3, int bands = 4, bool synthetic = true )
{
    Table t;
    t.id = QStringLiteral( "test-table" );
    t.bandCount = bands;
    for ( int b = 0; b < bands; ++b )
        t.wavelengthsNm.push_back( 500.0f + 10.0f * b );
    for ( int r = 0; r < count; ++r )
    {
        std::vector<float> row;
        for ( int b = 0; b < bands; ++b )
            row.push_back( 0.1f + 0.05f * r + 0.01f * b );
        t.spectra.push_back( row );
        t.labels.append( QStringLiteral( "spec_%1" ).arg( r ) );
    }
    t.provenance.sourceOperator = QStringLiteral( "rs:test" );
    t.provenance.sourceInput = QStringLiteral( "synthetic" );
    t.provenance.synthetic = synthetic;
    if ( !synthetic )
    {
        t.license = QStringLiteral( "CC0-1.0" );
        t.citation = QStringLiteral( "Test citation" );
    }
    return t;
}

} // namespace

TEST_CASE( "SpectralTable round-trips through JSON with a stable digest", "[spectral_table]" )
{
    Table t = makeTable();
    const QString digest = SpectralTable::digestHex( t.spectra, t.bandCount );
    REQUIRE( digest.size() == 64 );

    QJsonObject json = SpectralTable::toJson( t );
    REQUIRE( json["digest"].toString() == digest );

    Table parsed;
    QString error;
    REQUIRE( SpectralTable::fromJson( json, &parsed, &error ) );
    REQUIRE( parsed.digestHex == digest );

    // Same content -> same digest; changed cell -> different digest.
    Table other = makeTable();
    REQUIRE( SpectralTable::digestHex( other.spectra, other.bandCount ) == digest );
    other.spectra[1][2] += 1e-3f;
    REQUIRE( SpectralTable::digestHex( other.spectra, other.bandCount ) != digest );

    // Round-trip through a file preserves values and validation.
    QTemporaryDir dir;
    const QString path = dir.filePath( "table.json" );
    QString saveError;
    REQUIRE( SpectralTable::save( t, path, &saveError ) );

    Table loaded;
    REQUIRE( SpectralTable::loadValidated( path, &loaded, &error ) );
    REQUIRE( loaded.bandCount == t.bandCount );
    REQUIRE( loaded.count() == t.count() );
    REQUIRE( loaded.spectra[2][3] == Approx( t.spectra[2][3] ) );
    REQUIRE( loaded.wavelengthsNm.back() == Approx( t.wavelengthsNm.back() ) );
    REQUIRE( loaded.provenance.sourceOperator == t.provenance.sourceOperator );
}

TEST_CASE( "SpectralTable refuses tampered content on load", "[spectral_table]" )
{
    Table t = makeTable();
    QJsonObject json = SpectralTable::toJson( t );
    QJsonArray spectra = json["spectra"].toArray();
    QJsonArray row = spectra.at( 0 ).toArray();
    row[1] = row.at( 1 ).toDouble() + 0.5;
    spectra[0] = row;
    json["spectra"] = spectra;

    Table parsed;
    QString error;
    REQUIRE_FALSE( SpectralTable::fromJson( json, &parsed, &error ) );
    REQUIRE( error.contains( "digest" ) );
}

TEST_CASE( "SpectralTable validation catches structural violations", "[spectral_table]" )
{
    SECTION( "width mismatch" )
    {
        Table t = makeTable();
        t.spectra[1].pop_back();
        QStringList errors;
        REQUIRE_FALSE( SpectralTable::validate( t, &errors ) );
        REQUIRE( errors.join( ' ' ).contains( "spectra[1]" ) );
    }
    SECTION( "non-finite cell" )
    {
        Table t = makeTable();
        t.spectra[0][0] = std::numeric_limits<float>::quiet_NaN();
        QStringList errors;
        REQUIRE_FALSE( SpectralTable::validate( t, &errors ) );
        REQUIRE( errors.join( ' ' ).contains( "non-finite" ) );
    }
    SECTION( "non-monotonic wavelengths" )
    {
        Table t = makeTable();
        std::swap( t.wavelengthsNm[1], t.wavelengthsNm[2] );
        QStringList errors;
        REQUIRE_FALSE( SpectralTable::validate( t, &errors ) );
        REQUIRE( errors.join( ' ' ).contains( "wavelengths" ) );
    }
    SECTION( "label size mismatch" )
    {
        Table t = makeTable();
        t.labels.append( "extra" );
        QStringList errors;
        REQUIRE_FALSE( SpectralTable::validate( t, &errors ) );
        REQUIRE( errors.join( ' ' ).contains( "labels" ) );
    }
}

TEST_CASE( "Measured field tables require license and citation", "[spectral_table]" )
{
    Table measured = makeTable( 3, 4, /*synthetic=*/false );
    QStringList errors;
    REQUIRE( SpectralTable::validate( measured, &errors ) );

    measured.license.clear();
    REQUIRE_FALSE( SpectralTable::validate( measured, &errors ) );
    REQUIRE( errors.join( ' ' ).contains( "license" ) );

    Table synthetic = makeTable(); // synthetic: no license needed
    REQUIRE( SpectralTable::validate( synthetic, &errors ) );

    // The rule is enforced through the load path too: a measured table saved
    // without license cannot round-trip loadValidated.
    synthetic.provenance.synthetic = false;
    synthetic.license.clear();
    synthetic.citation.clear();
    QTemporaryDir dir;
    QString error;
    REQUIRE_FALSE( SpectralTable::save( synthetic, dir.filePath( "m.json" ), &error ) );
    REQUIRE( error.contains( "license" ) );

    // Derived artifacts (operator outputs over a recorded sourceInput) carry
    // the license story via the source, not their own license field.
    Table derived = makeTable( 3, 4, /*synthetic=*/false );
    derived.license.clear();
    derived.citation.clear();
    derived.provenance.derived = true;
    derived.provenance.sourceInput = QStringLiteral( "scene.tif" );
    REQUIRE( SpectralTable::validate( derived, &errors ) );
}

TEST_CASE( "SpectralTable refuses tables above the cell bound", "[spectral_table]" )
{
    // Probe the bound without materializing 4 Mi cells: 2100 x 2100 > 4 Mi.
    const long long cells = 2100LL * 2100LL;
    REQUIRE( cells > SpectralTable::kMaxCells );
    REQUIRE( cells < 6LL * 1000LL * 1000LL ); // probe stays small in memory

    Table t;
    t.id = QStringLiteral( "too-big" );
    t.bandCount = 2100;
    std::vector<float> row( 2100, 0.5f );
    for ( int r = 0; r < 2100; ++r )
        t.spectra.push_back( row );
    QStringList errors;
    REQUIRE_FALSE( SpectralTable::validate( t, &errors ) );
    REQUIRE( errors.join( ' ' ).contains( "bound" ) );
}

TEST_CASE( "SpectralTable rejects wrong kind and version", "[spectral_table]" )
{
    Table t = makeTable();
    QJsonObject json = SpectralTable::toJson( t );

    QJsonObject wrongKind = json;
    wrongKind["kind"] = "something-else";
    Table parsed;
    QString error;
    REQUIRE_FALSE( SpectralTable::fromJson( wrongKind, &parsed, &error ) );
    REQUIRE( error.contains( "kind" ) );

    QJsonObject wrongVersion = json;
    wrongVersion["version"] = 99;
    REQUIRE_FALSE( SpectralTable::fromJson( wrongVersion, &parsed, &error ) );
    REQUIRE( error.contains( "version" ) );
}

TEST_CASE( "Wavelength grids normalize units and refuse bad input", "[spectral_wavelength]" )
{
    using namespace SpectralWavelength;

    float nm = 0.0f;
    REQUIRE( normalizeToNm( 0.65, "nm", &nm ) );
    REQUIRE( nm == Approx( 650.0f ) );
    REQUIRE( normalizeToNm( 0.65, "µm", &nm ) );
    REQUIRE( nm == Approx( 650.0f ) );
    REQUIRE( normalizeToNm( 0.65, "um", &nm ) );
    REQUIRE( nm == Approx( 650.0f ) );
    REQUIRE( normalizeToNm( 0.65, "Micrometers", &nm ) );
    REQUIRE( nm == Approx( 650.0f ) );
    REQUIRE( normalizeToNm( 0.65, "", &nm ) ); // empty = repo default nm
    REQUIRE( nm == Approx( 0.65f ).epsilon( 1e-6 ) );
    REQUIRE_FALSE( normalizeToNm( 0.65, "furlongs", &nm ) );
    REQUIRE_FALSE( normalizeToNm( std::nan( "" ), "nm", &nm ) );
}

TEST_CASE( "Grid building validates monotonicity, FWHM pairing and coverage", "[spectral_wavelength]" )
{
    using namespace SpectralWavelength;

    SECTION( "ok with matching FWHM" )
    {
        std::vector<std::pair<double, std::string>> wl{ { 0.5, "um" }, { 0.6, "um" }, { 0.7, "um" } };
        std::vector<std::pair<double, std::string>> fw{ { 20, "nm" }, { 30, "nm" }, { 40, "nm" } };
        Grid grid;
        REQUIRE( gridFromBandValues( wl, fw, &grid ) == Status::Ok );
        REQUIRE( grid.size() == 3 );
        REQUIRE( grid.centersNm[1] == Approx( 600.0f ) );
        REQUIRE( grid.hasFwhm() );
        REQUIRE( grid.fwhmNm[2] == Approx( 40.0f ) );
    }
    SECTION( "non-monotonic" )
    {
        std::vector<std::pair<double, std::string>> wl{ { 650, "nm" }, { 550, "nm" } };
        Grid grid;
        REQUIRE( gridFromBandValues( wl, {}, &grid ) == Status::NonMonotonic );
    }
    SECTION( "unknown units" )
    {
        std::vector<std::pair<double, std::string>> wl{ { 650, "nm" }, { 750, "cm" } };
        Grid grid;
        REQUIRE( gridFromBandValues( wl, {}, &grid ) == Status::UnknownUnits );
    }
    SECTION( "fwhm size mismatch" )
    {
        std::vector<std::pair<double, std::string>> wl{ { 650, "nm" }, { 750, "nm" } };
        std::vector<std::pair<double, std::string>> fw{ { 20, "nm" } };
        Grid grid;
        REQUIRE( gridFromBandValues( wl, fw, &grid ) == Status::SizeMismatch );
    }
    SECTION( "empty input" )
    {
        Grid grid;
        REQUIRE( gridFromBandValues( {}, {}, &grid ) == Status::Empty );
    }
}

TEST_CASE( "Disjoint grid ranges refuse to resample", "[spectral_wavelength]" )
{
    using namespace SpectralWavelength;

    Grid a;
    a.centersNm = { 400.0f, 500.0f, 600.0f };
    Grid b;
    b.centersNm = { 1500.0f, 1600.0f };
    std::string reason;
    REQUIRE_FALSE( rangesOverlap( a, b, &reason ) );
    REQUIRE( reason.find( "disjoint" ) != std::string::npos );

    Grid c;
    c.centersNm = { 550.0f, 650.0f };
    REQUIRE( rangesOverlap( a, c ) );
}
