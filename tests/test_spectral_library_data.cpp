// test_spectral_library_data.cpp — D12 built-in spectral library data contract.
//
// Covers the shipped data/spectral/ deliverable end to end:
//   - strict validation rules (SpectralLibrary::validateLibrary / loadValidated),
//   - provenance completeness (source / license / citation / synthetic+derivation),
//   - LICENSES.md <-> library.json zero drift,
//   - material taxonomy coverage (key classes >= 3 entries),
//   - indexing + material-prior queries (byMaterial / byWavelengthRange /
//     priorsFor), including physics-level prior sanity (vegetation NIR >> red),
//   - sensor resampling (Landsat OLI / Sentinel-2 MSI / GF PMS) with the
//     resampled marking and class-preserving fidelity checks,
//   - SAM/SID smoke gate: clear-water and healthy-vegetation queries must rank
//     same-material entries first.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/spectral_library.h"

using SpectralLibrary::Entry;
using SpectralLibrary::Library;
using SpectralLibrary::SensorProfile;
using SpectralLibrary::SensorBand;

namespace
{

QString sourceDir()
{
#ifdef CMAKE_SOURCE_DIR
    return QString::fromUtf8( CMAKE_SOURCE_DIR );
#else
    return QStringLiteral( "." );
#endif
}

QString spectralPath( const QString &fileName )
{
    return QDir( sourceDir() ).filePath( QStringLiteral( "data/spectral/" ) + fileName );
}

bool writeFile( const QString &path, const QByteArray &payload )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return file.write( payload ) == payload.size();
}

/// Builds a minimal valid strict-mode entry; tests mutate individual fields.
Entry makeValidEntry( const QString &id = QStringLiteral( "test-entry" ) )
{
    Entry e;
    e.id = id;
    e.name = QStringLiteral( "Test entry" );
    e.material = QStringLiteral( "water" );
    e.source = QStringLiteral( "rs-studio synthetic model" );
    e.license = QStringLiteral( "CC0-1.0" );
    e.citation = QStringLiteral( "Synthetic spectrum, see derivation" );
    e.synthetic = true;
    e.derivation = QStringLiteral( "Parametric model" );
    e.spectrum = { 0.05f, 0.04f, 0.01f };
    e.wavelengths = { 500.0f, 600.0f, 700.0f };
    e.fwhm = { 5.0f, 5.0f, 5.0f };
    return e;
}

Library libraryOf( const Entry &entry )
{
    Library lib;
    lib.entries.append( entry );
    return lib;
}

/// Runs the strict validator; returns the joined error list.
QStringList validateErrors( const Library &library )
{
    QStringList errors;
    const bool ok = SpectralLibrary::validateLibrary( library, &errors );
    if ( ok )
        REQUIRE( errors.isEmpty() );
    else
        REQUIRE_FALSE( errors.isEmpty() );
    return errors;
}

const Entry &entryById( const Library &library, const QString &id )
{
    for ( const Entry &e : library.entries )
    {
        if ( e.id == id )
            return e;
    }
    FAIL( "entry not found: " << id.toStdString() );
    static const Entry dummy;
    return dummy;
}

/// Loads the shipped library.json through the strict loader.
Library loadBuiltin()
{
    Library lib;
    QString err;
    const bool ok = Library::loadValidated( spectralPath( QStringLiteral( "library.json" ) ), &lib, &err );
    INFO( "loadValidated error: " << err.toStdString() );
    REQUIRE( ok );
    return lib;
}

SensorProfile loadSensor( const QString &sensorId )
{
    SensorProfile profile;
    QString err;
    const bool ok = SensorProfile::loadSensor( spectralPath( QStringLiteral( "sensors.json" ) ),
                                               sensorId, &profile, &err );
    INFO( "sensor load error: " << err.toStdString() );
    REQUIRE( ok );
    return profile;
}

} // namespace

// ---------------------------------------------------------------------------
// Strict validation rules (WP B)
// ---------------------------------------------------------------------------

TEST_CASE( "validateLibrary accepts a well-formed entry", "[spectral_library_data]" )
{
    CHECK( validateErrors( libraryOf( makeValidEntry() ) ).isEmpty() );
}

TEST_CASE( "validateLibrary rejects non-increasing wavelength grids", "[spectral_library_data]" )
{
    Entry e = makeValidEntry( QStringLiteral( "bad-grid" ) );
    e.wavelengths = { 500.0f, 600.0f, 600.0f }; // duplicate wavelength
    const QStringList errors = validateErrors( libraryOf( e ) );
    CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
    { return s.contains( QStringLiteral( "bad-grid" ) ); } ) );

    Entry e2 = makeValidEntry( QStringLiteral( "bad-grid-2" ) );
    e2.wavelengths = { 600.0f, 500.0f, 700.0f }; // decreasing
    CHECK_FALSE( validateErrors( libraryOf( e2 ) ).isEmpty() );
}

TEST_CASE( "validateLibrary rejects reflectance outside [0,1] and non-finite values", "[spectral_library_data]" )
{
    SECTION( "Above 1" )
    {
        Entry e = makeValidEntry( QStringLiteral( "too-bright" ) );
        e.spectrum = { 0.1f, 1.2f, 0.2f };
        const QStringList errors = validateErrors( libraryOf( e ) );
        CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
        { return s.contains( QStringLiteral( "too-bright" ) ); } ) );
    }
    SECTION( "Below 0" )
    {
        Entry e = makeValidEntry( QStringLiteral( "negative" ) );
        e.spectrum = { 0.1f, -0.01f, 0.2f };
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
    SECTION( "NaN" )
    {
        Entry e = makeValidEntry( QStringLiteral( "nan" ) );
        e.spectrum = { 0.1f, std::numeric_limits<float>::quiet_NaN(), 0.2f };
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
    SECTION( "Inf (e.g. double overflow through the JSON float cast)" )
    {
        Entry e = makeValidEntry( QStringLiteral( "inf" ) );
        e.spectrum = { 0.1f, std::numeric_limits<float>::infinity(), 0.2f };
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
}

TEST_CASE( "validateLibrary rejects non-positive FWHM", "[spectral_library_data]" )
{
    Entry e = makeValidEntry( QStringLiteral( "flat-fwhm" ) );
    e.fwhm = { 5.0f, 0.0f, 5.0f };
    const QStringList errors = validateErrors( libraryOf( e ) );
    CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
    { return s.contains( QStringLiteral( "flat-fwhm" ) ); } ) );
}

TEST_CASE( "validateLibrary requires provenance fields", "[spectral_library_data]" )
{
    for ( const QString field : { QStringLiteral( "source" ), QStringLiteral( "license" ),
                                  QStringLiteral( "citation" ) } )
    {
        Entry e = makeValidEntry( QStringLiteral( "no-provenance" ) );
        if ( field == QStringLiteral( "source" ) )
            e.source.clear();
        else if ( field == QStringLiteral( "license" ) )
            e.license.clear();
        else
            e.citation.clear();
        const QStringList errors = validateErrors( libraryOf( e ) );
        CHECK_FALSE( errors.isEmpty() );
    }
}

TEST_CASE( "validateLibrary requires a derivation for synthetic entries", "[spectral_library_data]" )
{
    Entry e = makeValidEntry( QStringLiteral( "no-derivation" ) );
    e.derivation.clear();
    const QStringList errors = validateErrors( libraryOf( e ) );
    CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
    { return s.contains( QStringLiteral( "no-derivation" ) ); } ) );
}

TEST_CASE( "validateLibrary requires unique non-empty slug ids in strict mode", "[spectral_library_data]" )
{
    SECTION( "Duplicate ids" )
    {
        Library lib;
        lib.entries.append( makeValidEntry( QStringLiteral( "dup" ) ) );
        lib.entries.append( makeValidEntry( QStringLiteral( "dup" ) ) );
        const QStringList errors = validateErrors( lib );
        CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
        { return s.contains( QStringLiteral( "dup" ) ); } ) );
    }
    SECTION( "Empty id" )
    {
        Entry e = makeValidEntry();
        e.id.clear();
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
    SECTION( "Uppercase / whitespace ids are not slugs" )
    {
        Entry e = makeValidEntry( QStringLiteral( "Not A Slug" ) );
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
}

TEST_CASE( "validateLibrary flags array size mismatches", "[spectral_library_data]" )
{
    SECTION( "Wavelengths shorter than spectrum" )
    {
        Entry e = makeValidEntry( QStringLiteral( "size-mismatch" ) );
        e.wavelengths = { 500.0f, 600.0f };
        const QStringList errors = validateErrors( libraryOf( e ) );
        CHECK( std::any_of( errors.cbegin(), errors.cend(), []( const QString &s )
        { return s.contains( QStringLiteral( "size-mismatch" ) ); } ) );
    }
    SECTION( "FWHM longer than spectrum" )
    {
        Entry e = makeValidEntry( QStringLiteral( "fwhm-mismatch" ) );
        e.fwhm = { 5.0f, 5.0f, 5.0f, 5.0f };
        CHECK_FALSE( validateErrors( libraryOf( e ) ).isEmpty() );
    }
}

TEST_CASE( "loadValidated reports the offending entry id and never drops entries silently",
           "[spectral_library_data]" )
{
    const QByteArray json = R"({
        "id": "unit-test-library",
        "entries": [
            {"id": "good-entry", "name": "Good", "material": "water",
             "reflectance": [0.01, 0.02, 0.03], "wavelengths": [500.0, 600.0, 700.0],
             "fwhm": [5.0, 5.0, 5.0],
             "source": "synthetic", "license": "CC0-1.0", "citation": "unit test",
             "synthetic": true, "derivation": "constant"},
            {"id": "bad-entry", "name": "Bad", "material": "vegetation",
             "reflectance": [0.1, 2.0, 0.3], "wavelengths": [500.0, 600.0, 700.0],
             "fwhm": [5.0, 5.0, 5.0],
             "source": "synthetic", "license": "CC0-1.0", "citation": "unit test",
             "synthetic": true, "derivation": "constant"}
        ]
    })";
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "library.json" ) );
    REQUIRE( writeFile( path, json ) );

    Library lib;
    QString err;
    CHECK_FALSE( Library::loadValidated( path, &lib, &err ) );
    CHECK( err.contains( QStringLiteral( "bad-entry" ) ) );
    CHECK_FALSE( err.contains( QStringLiteral( "good-entry" ) ) );
}

TEST_CASE( "fromJson stays backward compatible with the v1 library format",
           "[spectral_library_data]" )
{
    // v1 files carry no id / license / per-entry grids; they load through the
    // lenient path (the workbench must keep working) but fail strict
    // validation because provenance is missing.
    const QByteArray json = R"({
        "format": "sicnu-spectral-library", "version": 1,
        "wavelengths": [500.0, 600.0, 700.0],
        "entries": [{"name": "Legacy", "material": "water", "spectrum": [0.05, 0.04, 0.01]}]
    })";
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "v1.json" ) );
    REQUIRE( writeFile( path, json ) );

    Library lib;
    QString err;
    REQUIRE( Library::load( path, &lib, &err ) );
    REQUIRE( lib.entries.size() == 1 );
    CHECK( lib.entries.first().wavelengths == std::vector<float>( { 500.0f, 600.0f, 700.0f } ) );
    CHECK( lib.entries.first().id.isEmpty() ); // v1 has no ids

    CHECK_FALSE( Library::loadValidated( path, &lib, &err ) );
    CHECK( err.contains( QStringLiteral( "provenance" ) ) );
}

TEST_CASE( "fromJson accepts 'reflectance' as the canonical value array and per-entry grids",
           "[spectral_library_data]" )
{
    const QByteArray json = R"({
        "id": "alias-check",
        "entries": [
            {"id": "a", "name": "A", "reflectance": [0.1, 0.2],
             "wavelengths": [400.0, 500.0], "fwhm": [10.0, 10.0],
             "source": "s", "license": "CC0-1.0", "citation": "c"},
            {"id": "b", "name": "B", "reflectance": [0.3, 0.4],
             "wavelengths": [401.0, 501.0], "fwhm": [11.0, 11.0],
             "source": "s", "license": "CC0-1.0", "citation": "c"}
        ]
    })";
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "alias.json" ) );
    REQUIRE( writeFile( path, json ) );

    Library lib;
    QString err;
    REQUIRE( Library::load( path, &lib, &err ) );
    REQUIRE( lib.entries.size() == 2 );
    CHECK( lib.entries[0].spectrum == std::vector<float>( { 0.1f, 0.2f } ) );
    CHECK( lib.entries[0].wavelengths == std::vector<float>( { 400.0f, 500.0f } ) );
    CHECK( lib.entries[1].wavelengths == std::vector<float>( { 401.0f, 501.0f } ) );
    // Mixed grids -> no shared grid, but band counts stay equal.
    CHECK( lib.bandCount() == 2 );
    CHECK( lib.wavelengths().empty() );
}

TEST_CASE( "fromJson rejects conflicting 'spectrum' and 'reflectance' arrays",
           "[spectral_library_data]" )
{
    const QByteArray json = R"({"entries":[{"name":"a","spectrum":[0.1],"reflectance":[0.2]}]})";
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "conflict.json" ) );
    REQUIRE( writeFile( path, json ) );

    Library lib;
    QString err;
    CHECK_FALSE( Library::load( path, &lib, &err ) );
}

// ---------------------------------------------------------------------------
// Shipped built-in library contract (WP C/D/E data + schema)
// ---------------------------------------------------------------------------

TEST_CASE( "Built-in spectral library loads through the strict loader", "[spectral_library_data][builtin]" )
{
    const Library lib = loadBuiltin();
    CHECK( lib.entries.size() >= 20 );
    CHECK_FALSE( lib.libraryId().isEmpty() );
    CHECK( lib.bandCount() >= 200 ); // 400-2500 nm at fine sampling
}

TEST_CASE( "Built-in library entries satisfy every numeric and provenance constraint",
           "[spectral_library_data][builtin]" )
{
    const Library lib = loadBuiltin();

    QStringList ids;
    for ( const Entry &e : lib.entries )
    {
        INFO( "entry: " << e.id.toStdString() );
        CHECK_FALSE( e.id.isEmpty() );
        CHECK_FALSE( ids.contains( e.id ) );
        ids.append( e.id );

        CHECK( e.spectrum.size() == e.wavelengths.size() );
        CHECK( e.spectrum.size() == e.fwhm.size() );
        CHECK( e.wavelengths.size() >= 2 );
        CHECK( e.wavelengths.front() >= 400.0f );
        CHECK( e.wavelengths.back() <= 2500.0f );
        for ( size_t i = 1; i < e.wavelengths.size(); ++i )
            CHECK( e.wavelengths[i] > e.wavelengths[i - 1] );
        for ( float v : e.spectrum )
        {
            CHECK( std::isfinite( v ) );
            CHECK( v >= 0.0f );
            CHECK( v <= 1.0f );
        }
        for ( float f : e.fwhm )
        {
            CHECK( std::isfinite( f ) );
            CHECK( f > 0.0f );
        }
        CHECK_FALSE( e.source.isEmpty() );
        CHECK( e.license == QStringLiteral( "CC0-1.0" ) );
        CHECK_FALSE( e.citation.isEmpty() );
        CHECK( e.synthetic );
        CHECK_FALSE( e.derivation.isEmpty() );
        CHECK( SpectralLibrary::kKnownMaterials.contains( e.material ) );
    }
}

TEST_CASE( "Built-in library covers the fixed material taxonomy with intra-class variation",
           "[spectral_library_data][builtin]" )
{
    const Library lib = loadBuiltin();

    // Key classes need >= 3 entries showing within-class variability.
    for ( const QString key : { QStringLiteral( "water" ), QStringLiteral( "vegetation" ),
                                QStringLiteral( "soil" ), QStringLiteral( "impervious_surface" ) } )
    {
        CHECK( lib.byMaterial( key ).size() >= 3 );
    }
    // Every taxonomy class is present at least once.
    for ( const QString material : SpectralLibrary::kKnownMaterials )
        CHECK( lib.byMaterial( material ).size() >= 1 );

    // Within-class variation: entries of a key class must not be identical.
    for ( const QString key : { QStringLiteral( "water" ), QStringLiteral( "vegetation" ),
                                QStringLiteral( "soil" ), QStringLiteral( "impervious_surface" ) } )
    {
        const QVector<Entry> same = lib.byMaterial( key );
        for ( int i = 1; i < same.size(); ++i )
            CHECK_FALSE( same[i].spectrum == same[0].spectrum );
    }
}

TEST_CASE( "Built-in library stays inside the 5 MB uncompressed budget", "[spectral_library_data][builtin]" )
{
    const QFileInfo info( spectralPath( QStringLiteral( "library.json" ) ) );
    REQUIRE( info.exists() );
    const qint64 bytes = info.size();
    INFO( "library.json bytes: " << bytes );
    CHECK( bytes <= 5 * 1024 * 1024 );
}

TEST_CASE( "Library schema file is present and constrains the shipped entries",
           "[spectral_library_data][builtin]" )
{
    QFile schemaFile( spectralPath( QStringLiteral( "library.schema.json" ) ) );
    REQUIRE( schemaFile.open( QIODevice::ReadOnly ) );
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( schemaFile.readAll(), &parseError );
    REQUIRE( parseError.error == QJsonParseError::NoError );
    const QJsonObject schema = doc.object();
    CHECK( schema.value( QStringLiteral( "$schema" ) ).toString().contains( QStringLiteral( "json-schema.org" ) ) );

    const QJsonObject entrySchema = schema.value( QStringLiteral( "properties" ) ).toObject()
                                        .value( QStringLiteral( "entries" ) ).toObject()
                                        .value( QStringLiteral( "items" ) ).toObject();
    const QJsonArray required = entrySchema.value( QStringLiteral( "required" ) ).toArray();
    QStringList requiredNames;
    for ( const QJsonValue &v : required )
        requiredNames.append( v.toString() );
    for ( const QString field : { QStringLiteral( "id" ), QStringLiteral( "material" ),
                                  QStringLiteral( "wavelengths" ), QStringLiteral( "reflectance" ),
                                  QStringLiteral( "source" ), QStringLiteral( "license" ),
                                  QStringLiteral( "citation" ) } )
    {
        CHECK( requiredNames.contains( field ) );
    }
}

TEST_CASE( "LICENSES.md stays in sync with the shipped library", "[spectral_library_data][builtin]" )
{
    const Library lib = loadBuiltin();
    QFile licensesFile( spectralPath( QStringLiteral( "LICENSES.md" ) ) );
    REQUIRE( licensesFile.open( QIODevice::ReadOnly ) );
    const QString licenses = QString::fromUtf8( licensesFile.readAll() );

    CHECK( licenses.contains( QStringLiteral( "CC0-1.0" ) ) );
    CHECK_FALSE( licenses.contains( QStringLiteral( "TBD" ) ) );
    CHECK_FALSE( licenses.contains( QStringLiteral( "TODO" ) ) );

    // Zero drift: every shipped entry id must be listed in LICENSES.md.
    for ( const Entry &e : lib.entries )
    {
        INFO( "entry id missing from LICENSES.md: " << e.id.toStdString() );
        CHECK( licenses.contains( e.id ) );
    }

    // And every entry table row in LICENSES.md must reference a real entry id.
    const QRegularExpression entryRow( QStringLiteral( "^\\| `([a-z0-9][a-z0-9._-]*)` " ),
                                       QRegularExpression::MultilineOption );
    QRegularExpressionIterator it( entryRow );
    while ( it.hasNext() )
    {
        const QString id = it.next().captured( 1 );
        INFO( "LICENSES.md row not in library: " << id.toStdString() );
        CHECK( std::any_of( lib.entries.cbegin(), lib.entries.cend(),
                            [&]( const Entry &e ) { return e.id == id; } ) );
    }
}

// ---------------------------------------------------------------------------
// Indexing + material priors (WP B / WP G)
// ---------------------------------------------------------------------------

TEST_CASE( "byMaterial and byWavelengthRange index the library", "[spectral_library_data][priors]" )
{
    const Library lib = loadBuiltin();

    const QStringList materials = lib.materials();
    CHECK( materials.size() == SpectralLibrary::kKnownMaterials.size() );
    CHECK( materials == lib.materials() ); // stable order
    CHECK( std::is_sorted( materials.cbegin(), materials.cend() ) );

    const QVector<Entry> water = lib.byMaterial( QStringLiteral( "water" ) );
    REQUIRE_FALSE( water.isEmpty() );
    for ( const Entry &e : water )
        CHECK( e.material == QStringLiteral( "water" ) );
    CHECK( lib.byMaterial( QStringLiteral( "no-such-material" ) ).isEmpty() );

    const QVector<Entry> visible = lib.byWavelengthRange( 450.0f, 520.0f );
    CHECK( visible.size() == lib.entries.size() ); // all shipped entries cover 400-2500
    CHECK( lib.byWavelengthRange( 3000.0f, 3100.0f ).isEmpty() );
    CHECK( lib.byWavelengthRange( 100.0f, 200.0f ).isEmpty() );
}

TEST_CASE( "priorsFor returns stable, physics-consistent JSON per material",
           "[spectral_library_data][priors]" )
{
    const Library lib = loadBuiltin();

    const QJsonObject vegetation = lib.priorsFor( QStringLiteral( "vegetation" ) );
    CHECK( vegetation.value( QStringLiteral( "material" ) ).toString() == QStringLiteral( "vegetation" ) );
    CHECK( vegetation.value( QStringLiteral( "entryCount" ) ).toInt()
           == lib.byMaterial( QStringLiteral( "vegetation" ) ).size() );

    const QJsonObject bands = vegetation.value( QStringLiteral( "bands" ) ).toObject();
    CHECK( bands.size() == 7 );
    const auto meanOf = [&]( const char *band )
    {
        return bands.value( QLatin1String( band ) ).toObject().value( QStringLiteral( "mean" ) ).toDouble();
    };
    // The core vegetation prior, straight from the shipped data:
    CHECK( meanOf( "nir" ) > meanOf( "red" ) + 0.15 );
    CHECK( meanOf( "redEdge" ) > meanOf( "red" ) );

    // Water prior: NIR is essentially dark.
    const QJsonObject water = lib.priorsFor( QStringLiteral( "water" ) );
    const QJsonObject waterBands = water.value( QStringLiteral( "bands" ) ).toObject();
    CHECK( waterBands.value( QStringLiteral( "nir" ) ).toObject().value( QStringLiteral( "max" ) ).toDouble() < 0.10 );

    // Stability: repeated calls serialize to identical bytes.
    const QJsonDocument doc( vegetation );
    CHECK( doc.toJson( QJsonDocument::Compact ) == QJsonDocument( lib.priorsFor( QStringLiteral( "vegetation" ) ) ).toJson( QJsonDocument::Compact ) );

    // Unknown material: well-formed empty prior, never a crash.
    const QJsonObject missing = lib.priorsFor( QStringLiteral( "no-such-material" ) );
    CHECK( missing.value( QStringLiteral( "material" ) ).toString() == QStringLiteral( "no-such-material" ) );
    CHECK( missing.value( QStringLiteral( "entryCount" ) ).toInt() == 0 );
}

// ---------------------------------------------------------------------------
// Sensor profiles + resampling (WP F)
// ---------------------------------------------------------------------------

TEST_CASE( "Sensor profile registry ships Landsat OLI, Sentinel-2 MSI and GF PMS",
           "[spectral_library_data][sensors]" )
{
    const SensorProfile oli = loadSensor( QStringLiteral( "landsat-oli" ) );
    CHECK( oli.bands.size() == 9 );
    const SensorProfile s2 = loadSensor( QStringLiteral( "sentinel-2-msi" ) );
    CHECK( s2.bands.size() == 13 );
    const SensorProfile gf = loadSensor( QStringLiteral( "gf-pms" ) );
    CHECK( gf.bands.size() == 4 );

    for ( const SensorProfile *profile : { &oli, &s2, &gf } )
    {
        CHECK_FALSE( profile->name.isEmpty() );
        REQUIRE( profile->bands.size() >= 4 );
        for ( const SensorBand &b : profile->bands )
        {
            CHECK( b.wavelengthNm > 400.0f );
            CHECK( b.wavelengthNm < 2500.0f );
            CHECK( b.fwhmNm > 0.0f );
            CHECK_FALSE( b.name.isEmpty() );
        }
    }
    SensorProfile missing;
    QString missingErr;
    CHECK_FALSE( SensorProfile::loadSensor( spectralPath( QStringLiteral( "sensors.json" ) ),
                                            QStringLiteral( "no-such-sensor" ), &missing, &missingErr ) );
    CHECK( missing.bands.isEmpty() );
    CHECK( missingErr.contains( QStringLiteral( "no-such-sensor" ) ) );
}

TEST_CASE( "resampleTo aligns the library onto sensor grids and marks entries resampled",
           "[spectral_library_data][sensors]" )
{
    const Library lib = loadBuiltin();
    const SensorProfile s2 = loadSensor( QStringLiteral( "sentinel-2-msi" ) );

    Library resampled;
    QString err;
    REQUIRE( lib.resampleTo( s2, &resampled, &err ) );
    REQUIRE( resampled.entries.size() == lib.entries.size() );
    CHECK( resampled.bandCount() == 13 );
    CHECK( resampled.wavelengths().size() == 13 );

    for ( int i = 0; i < resampled.entries.size(); ++i )
    {
        const Entry &src = lib.entries[i];
        const Entry &dst = resampled.entries[i];
        INFO( "entry: " << dst.id.toStdString() );
        CHECK( dst.id == src.id );                 // identity preserved
        CHECK( dst.material == src.material );     // class preserved
        CHECK( dst.license == src.license );       // provenance preserved
        CHECK( dst.tags.contains( QStringLiteral( "resampled" ) ) );
        CHECK( dst.tags.contains( QStringLiteral( "sensor:sentinel-2-msi" ) ) );
        CHECK( dst.spectrum.size() == 13 );
        for ( float v : dst.spectrum )
        {
            CHECK( std::isfinite( v ) );
            CHECK( v >= 0.0f );
            CHECK( v <= 1.0f );
        }
    }
}

TEST_CASE( "Resampling to sensor grids preserves material physics", "[spectral_library_data][sensors]" )
{
    const Library lib = loadBuiltin();
    const SensorProfile s2 = loadSensor( QStringLiteral( "sentinel-2-msi" ) );

    Library resampled;
    QString err;
    REQUIRE( lib.resampleTo( s2, &resampled, &err ) );

    const auto reflectanceAt = []( const Entry &e, int index )
    { return e.spectrum[static_cast<size_t>( index )]; };

    const Entry &water = entryById( resampled, QStringLiteral( "water-clear-deep" ) );
    // S2 B4 (red, ~664.5 nm) is index 3: clear deep water stays dark in red.
    CHECK( reflectanceAt( water, 3 ) < 0.05 );

    const Entry &snow = entryById( resampled, QStringLiteral( "snow-fresh" ) );
    // S2 B2 (blue, ~490 nm) is index 1: fresh snow stays bright.
    CHECK( reflectanceAt( snow, 1 ) > 0.80 );

    const Entry &veg = entryById( resampled, QStringLiteral( "vegetation-healthy-canopy" ) );
    // NIR-red contrast survives band integration.
    CHECK( reflectanceAt( veg, 7 ) - reflectanceAt( veg, 3 ) > 0.25 ); // B8 vs B4

    // Fidelity: a sensor band value tracks the source spectrum near the band
    // centre (Gaussian SRF over a dense source grid ~ interpolation).
    const Entry &srcVeg = entryById( lib, QStringLiteral( "vegetation-healthy-canopy" ) );
    CHECK( reflectanceAt( veg, 3 ) == Catch::Approx( 0.05 ).margin( 0.06 ) ); // red ~665nm
    CHECK( reflectanceAt( veg, 7 ) == Catch::Approx( 0.44 ).margin( 0.08 ) ); // NIR plateau ~843nm
    CHECK( srcVeg.spectrum.size() == 421 );                                   // 400-2500 nm @ 5 nm
}

TEST_CASE( "resampleTo rejects sensors with bands outside the library coverage",
           "[spectral_library_data][sensors]" )
{
    const Library lib = loadBuiltin();

    SensorProfile outOfRange;
    outOfRange.id = QStringLiteral( "x-ray-cam" );
    outOfRange.name = QStringLiteral( "Out of range" );
    outOfRange.bands = { SensorBand{ QStringLiteral( "b1" ), 480.0f, 20.0f },
                         SensorBand{ QStringLiteral( "b2" ), 3000.0f, 50.0f } };

    Library resampled;
    QString err;
    CHECK_FALSE( lib.resampleTo( outOfRange, &resampled, &err ) );
    CHECK( err.contains( QStringLiteral( "3000" ) ) );
}

TEST_CASE( "Landsat-OLI resampled water matches native-grid water through matchSpectrum",
           "[spectral_library_data][sensors]" )
{
    // End-to-end gate: water on the Landsat grid (9 bands, resampled marker)
    // matched against the native library must rank water entries first, and
    // the wavelength-aware matcher must flag the matches as resampled.
    const Library lib = loadBuiltin();
    const SensorProfile oli = loadSensor( QStringLiteral( "landsat-oli" ) );

    Library onOli;
    QString err;
    REQUIRE( lib.resampleTo( oli, &onOli, &err ) );
    const Entry waterOnOli = entryById( onOli, QStringLiteral( "water-clear-deep" ) );
    REQUIRE( waterOnOli.wavelengths.size() == waterOnOli.spectrum.size() );

    const auto scores = SpectralLibrary::matchSpectrum(
        waterOnOli.spectrum, waterOnOli.wavelengths, lib,
        SpectralClassification::kNoDataSentinel );
    REQUIRE( scores.size() == lib.entries.size() );
    CHECK( scores.front().resampled );
    CHECK( scores.front().material == QStringLiteral( "water" ) );
    CHECK( scores.at( 1 ).material == QStringLiteral( "water" ) );
}

// ---------------------------------------------------------------------------
// SAM/SID smoke gate (completion gate: class-preserving best match)
// ---------------------------------------------------------------------------

TEST_CASE( "Clear-water query ranks water entries first on the native grid",
           "[spectral_library_data][matching]" )
{
    const Library lib = loadBuiltin();
    const Entry query = entryById( lib, QStringLiteral( "water-clear-deep" ) );

    const auto scores = SpectralLibrary::matchSpectrum( query.spectrum, lib,
                                                        SpectralClassification::kNoDataSentinel );
    REQUIRE( scores.size() == lib.entries.size() );
    // The trivial best match is the query itself; the first non-self matches
    // must stay inside the water class (intra-class cohesion gate).
    CHECK( scores[0].name == query.name );
    CHECK( scores[0].angleDegrees == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( scores[1].material == QStringLiteral( "water" ) );
    CHECK( scores[2].material == QStringLiteral( "water" ) );
    CHECK( scores[0].divergence == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    CHECK( std::isfinite( scores[1].divergence ) );
}

TEST_CASE( "Healthy-vegetation query ranks vegetation entries first on the native grid",
           "[spectral_library_data][matching]" )
{
    const Library lib = loadBuiltin();
    const Entry query = entryById( lib, QStringLiteral( "vegetation-healthy-canopy" ) );

    const auto scores = SpectralLibrary::matchSpectrum( query.spectrum, lib );
    REQUIRE( scores.size() == lib.entries.size() );
    CHECK( scores[0].name == query.name );
    CHECK( scores[1].material == QStringLiteral( "vegetation" ) );
    CHECK( scores[2].material == QStringLiteral( "vegetation" ) );
}

TEST_CASE( "Cross-class queries stay separable: water and vegetation do not confuse",
           "[spectral_library_data][matching]" )
{
    const Library lib = loadBuiltin();
    const Entry water = entryById( lib, QStringLiteral( "water-turbid-sediment" ) );
    const Entry veg = entryById( lib, QStringLiteral( "vegetation-stressed-canopy" ) );

    const auto waterScores = SpectralLibrary::matchSpectrum( water.spectrum, lib );
    REQUIRE_FALSE( waterScores.empty() );
    CHECK( waterScores.front().material == QStringLiteral( "water" ) );

    const auto vegScores = SpectralLibrary::matchSpectrum( veg.spectrum, lib );
    REQUIRE_FALSE( vegScores.empty() );
    CHECK( vegScores.front().material == QStringLiteral( "vegetation" ) );
}

