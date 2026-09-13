// src/processing/algorithms/spectral_library.cpp — spectral library domain
#include "spectral_library.h"

#include "spectral_classification.h"
#include "spectral_resampling.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace SpectralLibrary
{

namespace
{

/// Reads a JSON array of numbers into a float vector; false on non-array or
/// non-numeric content.
bool readFloatArray( const QJsonValue &value, std::vector<float> *out )
{
    if ( !value.isArray() )
        return false;
    const QJsonArray array = value.toArray();
    out->resize( static_cast<size_t>( array.size() ) );
    for ( int i = 0; i < array.size(); ++i )
    {
        if ( !array[i].isDouble() )
            return false;
        ( *out )[static_cast<size_t>( i )] = static_cast<float>( array[i].toDouble() );
    }
    return true;
}

QJsonArray toJsonArray( const std::vector<float> &values )
{
    QJsonArray array;
    for ( float v : values )
        array.append( v );
    return array;
}

} // namespace

QJsonObject Library::toJson() const
{
    QJsonObject root;
    root.insert( QStringLiteral( "format" ), QStringLiteral( "sicnu-spectral-library" ) );
    root.insert( QStringLiteral( "version" ), 2 );
    if ( !id.isEmpty() )
        root.insert( QStringLiteral( "id" ), id );

    const int bands = bandCount();
    if ( bands > 0 )
        root.insert( QStringLiteral( "bandCount" ), bands );

    // Shared grids are stored once at the root; when entries carry divergent
    // grids they are serialized per entry instead.
    const std::vector<float> wl = wavelengths();
    const bool sharedGrids = !wl.empty();
    if ( sharedGrids )
        root.insert( QStringLiteral( "wavelengths" ), toJsonArray( wl ) );
    const std::vector<float> fw = fwhm();
    if ( !fw.empty() )
        root.insert( QStringLiteral( "fwhm" ), toJsonArray( fw ) );

    QJsonArray entryArray;
    for ( const Entry &entry : entries )
    {
        QJsonObject e;
        if ( !entry.id.isEmpty() )
            e.insert( QStringLiteral( "id" ), entry.id );
        e.insert( QStringLiteral( "name" ), entry.name );
        if ( !entry.material.isEmpty() )
            e.insert( QStringLiteral( "material" ), entry.material );
        if ( !entry.subclass.isEmpty() )
            e.insert( QStringLiteral( "subclass" ), entry.subclass );
        if ( !entry.source.isEmpty() )
            e.insert( QStringLiteral( "source" ), entry.source );
        if ( !entry.license.isEmpty() )
            e.insert( QStringLiteral( "license" ), entry.license );
        if ( !entry.citation.isEmpty() )
            e.insert( QStringLiteral( "citation" ), entry.citation );
        if ( entry.synthetic )
            e.insert( QStringLiteral( "synthetic" ), true );
        if ( !entry.derivation.isEmpty() )
            e.insert( QStringLiteral( "derivation" ), entry.derivation );
        if ( !entry.tags.isEmpty() )
        {
            QJsonArray tags;
            for ( const QString &tag : entry.tags )
                tags.append( tag );
            e.insert( QStringLiteral( "tags" ), tags );
        }
        e.insert( QStringLiteral( "spectrum" ), toJsonArray( entry.spectrum ) );
        if ( !sharedGrids )
        {
            if ( !entry.wavelengths.empty() )
                e.insert( QStringLiteral( "wavelengths" ), toJsonArray( entry.wavelengths ) );
            if ( !entry.fwhm.empty() )
                e.insert( QStringLiteral( "fwhm" ), toJsonArray( entry.fwhm ) );
        }
        entryArray.append( e );
    }
    root.insert( QStringLiteral( "entries" ), entryArray );
    return root;
}

bool Library::fromJson( const QJsonObject &json, Library *out, QString *errorMessage )
{
    if ( !out )
        return false;

    Library library;
    library.id = json.value( QStringLiteral( "id" ) ).toString();
    const QJsonArray entryArray = json.value( QStringLiteral( "entries" ) ).toArray();
    int bandCount = -1;
    for ( const QJsonValue &value : entryArray )
    {
        const QJsonObject e = value.toObject();
        Entry entry;
        entry.id = e.value( QStringLiteral( "id" ) ).toString();
        entry.name = e.value( QStringLiteral( "name" ) ).toString();
        if ( entry.name.isEmpty() )
            entry.name = entry.id; // v2 files may rely on the id alone
        if ( entry.name.isEmpty() )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entry is missing a name" );
            return false;
        }
        entry.material = e.value( QStringLiteral( "material" ) ).toString();
        entry.subclass = e.value( QStringLiteral( "subclass" ) ).toString();
        entry.source = e.value( QStringLiteral( "source" ) ).toString();
        entry.license = e.value( QStringLiteral( "license" ) ).toString();
        entry.citation = e.value( QStringLiteral( "citation" ) ).toString();
        entry.synthetic = e.value( QStringLiteral( "synthetic" ) ).toBool( false );
        entry.derivation = e.value( QStringLiteral( "derivation" ) ).toString();
        const QJsonArray tagArray = e.value( QStringLiteral( "tags" ) ).toArray();
        for ( const QJsonValue &tag : tagArray )
        {
            if ( tag.isString() )
                entry.tags.append( tag.toString() );
        }

        // 'reflectance' is the canonical value array of curated libraries;
        // 'spectrum' is the v1 name kept as an alias. Carrying both is an
        // error unless they agree exactly.
        const bool hasSpectrum = e.contains( QStringLiteral( "spectrum" ) );
        const bool hasReflectance = e.contains( QStringLiteral( "reflectance" ) );
        if ( hasSpectrum && hasReflectance )
        {
            std::vector<float> spectrum, reflectance;
            if ( !readFloatArray( e.value( QStringLiteral( "spectrum" ) ), &spectrum )
                 || !readFloatArray( e.value( QStringLiteral( "reflectance" ) ), &reflectance )
                 || spectrum != reflectance )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Spectral library entry '%1' has conflicting "
                                                    "'spectrum' and 'reflectance' arrays" )
                                        .arg( entry.name );
                return false;
            }
            entry.spectrum = std::move( reflectance );
        }
        else if ( hasReflectance )
        {
            if ( !readFloatArray( e.value( QStringLiteral( "reflectance" ) ), &entry.spectrum )
                 || entry.spectrum.empty() )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Spectral library entry '%1' has an invalid spectrum" )
                                        .arg( entry.name );
                return false;
            }
        }
        else if ( !readFloatArray( e.value( QStringLiteral( "spectrum" ) ), &entry.spectrum )
                  || entry.spectrum.empty() )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entry '%1' has an invalid spectrum" )
                                    .arg( entry.name );
            return false;
        }

        if ( bandCount < 0 )
            bandCount = static_cast<int>( entry.spectrum.size() );
        else if ( static_cast<int>( entry.spectrum.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entries have inconsistent band counts" );
            return false;
        }

        // Per-entry wavelength / FWHM grids (v2). Malformed arrays and size
        // mismatches are hard errors — never silently dropped.
        static const QLatin1String gridKeys[] = { QLatin1String( "wavelengths" ), QLatin1String( "fwhm" ) };
        for ( const QLatin1String &key : gridKeys )
        {
            if ( !e.contains( key ) )
                continue;
            std::vector<float> grid;
            if ( !readFloatArray( e.value( key ), &grid ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Spectral library entry '%1' has an invalid '%2' array" )
                                        .arg( entry.name, QString( key ) );
                return false;
            }
            if ( static_cast<int>( grid.size() ) != bandCount )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Spectral library entry '%1' has a '%2' grid size "
                                                    "that does not match its band count" )
                                        .arg( entry.name, QString( key ) );
                return false;
            }
            if ( key == QLatin1String( "wavelengths" ) )
                entry.wavelengths = std::move( grid );
            else
                entry.fwhm = std::move( grid );
        }

        library.entries.append( std::move( entry ) );
    }

    // Optional shared wavelength grid: applied to entries without their own.
    // Malformed root arrays are rejected instead of being ignored.
    std::vector<float> wl;
    if ( json.contains( QStringLiteral( "wavelengths" ) ) )
    {
        if ( !readFloatArray( json.value( QStringLiteral( "wavelengths" ) ), &wl ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library has an invalid 'wavelengths' array" );
            return false;
        }
        if ( bandCount >= 0 && static_cast<int>( wl.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library wavelength grid size "
                                                "does not match the band count" );
            return false;
        }
        for ( Entry &entry : library.entries )
        {
            if ( entry.wavelengths.empty() )
                entry.wavelengths = wl;
        }
    }

    // Optional shared FWHM grid, same rules.
    std::vector<float> fw;
    if ( json.contains( QStringLiteral( "fwhm" ) ) )
    {
        if ( !readFloatArray( json.value( QStringLiteral( "fwhm" ) ), &fw ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library has an invalid 'fwhm' array" );
            return false;
        }
        if ( bandCount >= 0 && static_cast<int>( fw.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library FWHM grid size "
                                                "does not match the band count" );
            return false;
        }
        for ( Entry &entry : library.entries )
        {
            if ( entry.fwhm.empty() )
                entry.fwhm = fw;
        }
    }

    *out = std::move( library );
    return true;
}

bool Library::save( const QString &path, QString *errorMessage ) const
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot write spectral library: %1" ).arg( path );
        return false;
    }
    const QByteArray payload = QJsonDocument( toJson() ).toJson( QJsonDocument::Compact );
    if ( file.write( payload ) != payload.size() || !file.flush() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Failed writing spectral library %1: %2" )
                                .arg( path, file.errorString() );
        return false;
    }
    return true;
}

bool Library::load( const QString &path, Library *out, QString *errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open spectral library: %1" ).arg( path );
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    if ( !doc.isObject() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Spectral library file is not a JSON object: %1" ).arg( path );
        return false;
    }
    return fromJson( doc.object(), out, errorMessage );
}

int Library::bandCount() const
{
    if ( entries.isEmpty() )
        return 0;
    const int bands = static_cast<int>( entries.first().spectrum.size() );
    for ( const Entry &entry : entries )
    {
        if ( static_cast<int>( entry.spectrum.size() ) != bands )
            return 0;
    }
    return bands;
}

std::vector<float> Library::wavelengths() const
{
    std::vector<float> result;
    if ( entries.isEmpty() )
        return result;
    result = entries.first().wavelengths;
    for ( const Entry &entry : entries )
    {
        if ( entry.wavelengths != result )
            return {};
    }
    return result;
}

std::vector<float> Library::fwhm() const
{
    std::vector<float> result;
    if ( entries.isEmpty() )
        return result;
    result = entries.first().fwhm;
    for ( const Entry &entry : entries )
    {
        if ( entry.fwhm != result )
            return {};
    }
    return result;
}

bool Library::loadValidated( const QString &path, Library *out, QString *errorMessage )
{
    if ( !load( path, out, errorMessage ) )
        return false;
    QStringList issues;
    if ( !validateLibrary( *out, &issues ) )
    {
        if ( errorMessage )
            *errorMessage = issues.join( QLatin1String( "; " ) );
        return false;
    }
    return true;
}

QStringList Library::materials() const
{
    QSet<QString> unique;
    for ( const Entry &entry : entries )
    {
        if ( !entry.material.isEmpty() )
            unique.insert( entry.material );
    }
    QStringList result = unique.values();
    std::sort( result.begin(), result.end() );
    return result;
}

QVector<Entry> Library::byMaterial( const QString &material ) const
{
    QVector<Entry> result;
    for ( const Entry &entry : entries )
    {
        if ( entry.material == material )
            result.append( entry );
    }
    return result;
}

QVector<Entry> Library::byWavelengthRange( float minNm, float maxNm ) const
{
    QVector<Entry> result;
    if ( minNm > maxNm )
        return result;
    for ( const Entry &entry : entries )
    {
        if ( entry.wavelengths.empty() )
            continue; // cannot assess coverage without a grid
        if ( entry.wavelengths.front() <= maxNm && entry.wavelengths.back() >= minNm )
            result.append( entry );
    }
    return result;
}

namespace
{

/// Sensor-agnostic wavelength windows (nm) summarizing material priors.
/// These are generic band windows, not material knowledge — the priors
/// themselves are computed from the library data.
struct PriorWindow
{
    QLatin1String name;
    float minNm;
    float maxNm;
};

const PriorWindow kPriorWindows[] = {
    { QLatin1String( "blue" ), 450.0f, 520.0f },   { QLatin1String( "green" ), 520.0f, 600.0f },
    { QLatin1String( "red" ), 630.0f, 690.0f },    { QLatin1String( "redEdge" ), 700.0f, 740.0f },
    { QLatin1String( "nir" ), 760.0f, 900.0f },    { QLatin1String( "swir1" ), 1550.0f, 1750.0f },
    { QLatin1String( "swir2" ), 2080.0f, 2350.0f }
};

double round4( double value )
{
    return std::round( value * 10000.0 ) / 10000.0;
}

} // namespace

QJsonObject Library::priorsFor( const QString &material ) const
{
    QJsonObject out;
    out.insert( QStringLiteral( "material" ), material );

    const QVector<Entry> selected = byMaterial( material );
    out.insert( QStringLiteral( "entryCount" ), selected.size() );
    if ( selected.isEmpty() )
    {
        out.insert( QStringLiteral( "subclasses" ), QJsonArray() );
        out.insert( QStringLiteral( "entries" ), QJsonArray() );
        out.insert( QStringLiteral( "bands" ), QJsonObject() );
        out.insert( QStringLiteral( "syntheticOnly" ), false );
        return out;
    }

    QSet<QString> subclasses;
    bool allSynthetic = true;
    float minWl = std::numeric_limits<float>::infinity();
    float maxWl = -std::numeric_limits<float>::infinity();
    bool anyGrid = false;
    struct Accumulator
    {
        float min = std::numeric_limits<float>::infinity();
        float max = -std::numeric_limits<float>::infinity();
        double sum = 0.0;
        int count = 0;
    };
    Accumulator windows[sizeof( kPriorWindows ) / sizeof( kPriorWindows[0] )];

    QJsonArray entryIds;
    for ( const Entry &entry : selected )
    {
        entryIds.append( entry.id.isEmpty() ? entry.name : entry.id );
        if ( !entry.subclass.isEmpty() )
            subclasses.insert( entry.subclass );
        if ( !entry.synthetic )
            allSynthetic = false;

        if ( entry.wavelengths.size() == entry.spectrum.size() && !entry.wavelengths.empty() )
        {
            anyGrid = true;
            minWl = std::min( minWl, entry.wavelengths.front() );
            maxWl = std::max( maxWl, entry.wavelengths.back() );
            for ( size_t i = 0; i < entry.spectrum.size(); ++i )
            {
                const float wl = entry.wavelengths[i];
                const float v = entry.spectrum[i];
                if ( !std::isfinite( wl ) || !std::isfinite( v ) )
                    continue;
                for ( size_t w = 0; w < sizeof( kPriorWindows ) / sizeof( kPriorWindows[0] ); ++w )
                {
                    const PriorWindow &window = kPriorWindows[w];
                    if ( wl >= window.minNm && wl <= window.maxNm )
                    {
                        Accumulator &acc = windows[w];
                        acc.min = std::min( acc.min, v );
                        acc.max = std::max( acc.max, v );
                        acc.sum += v;
                        acc.count += 1;
                    }
                }
            }
        }
    }
    out.insert( QStringLiteral( "syntheticOnly" ), allSynthetic );

    QJsonArray subclassArray;
    for ( const QString &subclass : subclasses.values() )
        subclassArray.append( subclass );
    out.insert( QStringLiteral( "subclasses" ), subclassArray );
    out.insert( QStringLiteral( "entries" ), entryIds );

    if ( anyGrid )
    {
        QJsonObject range;
        range.insert( QStringLiteral( "min" ), round4( minWl ) );
        range.insert( QStringLiteral( "max" ), round4( maxWl ) );
        out.insert( QStringLiteral( "wavelengthRangeNm" ), range );
    }

    QJsonObject bands;
    for ( size_t w = 0; w < sizeof( kPriorWindows ) / sizeof( kPriorWindows[0] ); ++w )
    {
        const Accumulator &acc = windows[w];
        if ( acc.count == 0 )
            continue;
        QJsonObject stats;
        QJsonArray windowNm;
        windowNm.append( kPriorWindows[w].minNm );
        windowNm.append( kPriorWindows[w].maxNm );
        stats.insert( QStringLiteral( "windowNm" ), windowNm );
        stats.insert( QStringLiteral( "min" ), round4( acc.min ) );
        stats.insert( QStringLiteral( "mean" ), round4( acc.sum / acc.count ) );
        stats.insert( QStringLiteral( "max" ), round4( acc.max ) );
        bands.insert( kPriorWindows[w].name, stats );
    }
    out.insert( QStringLiteral( "bands" ), bands );
    return out;
}

bool Library::resampleTo( const SensorProfile &sensor, Library *out, QString *errorMessage ) const
{
    if ( !out )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "resampleTo requires a non-null output library" );
        return false;
    }
    if ( !sensor.isValid() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot resample onto invalid sensor profile '%1'" )
                                .arg( sensor.id );
        return false;
    }

    Library result;
    result.id = id;
    const int dstBands = sensor.bands.size();
    std::vector<float> dstWl( static_cast<size_t>( dstBands ) );
    std::vector<float> dstFwhm( static_cast<size_t>( dstBands ) );
    for ( int b = 0; b < dstBands; ++b )
    {
        dstWl[static_cast<size_t>( b )] = sensor.bands.at( b ).wavelengthNm;
        dstFwhm[static_cast<size_t>( b )] = sensor.bands.at( b ).fwhmNm;
    }

    for ( const Entry &entry : entries )
    {
        if ( entry.wavelengths.empty()
             || entry.wavelengths.size() != entry.spectrum.size() )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Entry '%1' cannot be resampled: "
                                                "it carries no wavelength grid" )
                                    .arg( entry.id.isEmpty() ? entry.name : entry.id );
            return false;
        }

        std::vector<float> resampled( static_cast<size_t>( dstBands ),
                                      std::numeric_limits<float>::quiet_NaN() );
        const bool ok = ( entry.fwhm.size() == entry.spectrum.size() )
            ? SpectralResampling::resampleSpectrumGaussian(
                  entry.spectrum.data(), entry.wavelengths.data(),
                  static_cast<int>( entry.spectrum.size() ), dstWl.data(), dstFwhm.data(),
                  dstBands, resampled.data() )
            : SpectralResampling::resampleSpectrum(
                  entry.spectrum.data(), entry.wavelengths.data(),
                  static_cast<int>( entry.spectrum.size() ), dstWl.data(), dstBands,
                  resampled.data() );
        if ( !ok )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Entry '%1' could not be resampled onto '%2'" )
                                    .arg( entry.id.isEmpty() ? entry.name : entry.id, sensor.id );
            return false;
        }
        for ( int b = 0; b < dstBands; ++b )
        {
            if ( !std::isfinite( resampled[static_cast<size_t>( b )] ) )
            {
                if ( errorMessage )
                    *errorMessage = QStringLiteral( "Entry '%1': sensor band '%2' (%3 nm) lies "
                                                    "outside the source coverage [%4, %5] nm" )
                                        .arg( entry.id.isEmpty() ? entry.name : entry.id,
                                              sensor.bands.at( b ).name,
                                              QString::number( sensor.bands.at( b ).wavelengthNm ),
                                              QString::number( entry.wavelengths.front() ),
                                              QString::number( entry.wavelengths.back() ) );
                return false;
            }
        }

        Entry resampledEntry = entry; // identity + provenance carried over
        resampledEntry.spectrum = std::move( resampled );
        resampledEntry.wavelengths = dstWl;
        resampledEntry.fwhm = dstFwhm;
        if ( !resampledEntry.tags.contains( QStringLiteral( "resampled" ) ) )
            resampledEntry.tags.append( QStringLiteral( "resampled" ) );
        const QString sensorTag = QStringLiteral( "sensor:" ) + sensor.id;
        if ( !resampledEntry.tags.contains( sensorTag ) )
            resampledEntry.tags.append( sensorTag );
        result.entries.append( std::move( resampledEntry ) );
    }

    *out = std::move( result );
    return true;
}

bool SensorProfile::isValid() const
{
    if ( id.isEmpty() || name.isEmpty() || bands.isEmpty() )
        return false;
    for ( const SensorBand &band : bands )
    {
        if ( band.name.isEmpty() || !( band.wavelengthNm > 0.0f )
             || !std::isfinite( band.wavelengthNm ) || !( band.fwhmNm > 0.0f )
             || !std::isfinite( band.fwhmNm ) )
        {
            return false;
        }
    }
    return true;
}

bool SensorProfile::fromJson( const QJsonObject &json, SensorProfile *out, QString *errorMessage )
{
    if ( !out )
        return false;
    SensorProfile profile;
    profile.id = json.value( QStringLiteral( "id" ) ).toString();
    profile.name = json.value( QStringLiteral( "name" ) ).toString();
    const QJsonArray bandArray = json.value( QStringLiteral( "bands" ) ).toArray();
    if ( profile.id.isEmpty() || bandArray.isEmpty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Sensor profile is missing an id or has no bands" );
        return false;
    }
    for ( const QJsonValue &value : bandArray )
    {
        const QJsonObject b = value.toObject();
        SensorBand band;
        band.name = b.value( QStringLiteral( "name" ) ).toString();
        band.wavelengthNm = static_cast<float>( b.value( QStringLiteral( "wavelengthNm" ) ).toDouble() );
        band.fwhmNm = static_cast<float>( b.value( QStringLiteral( "fwhmNm" ) ).toDouble() );
        if ( band.name.isEmpty() || !( band.wavelengthNm > 0.0f ) || !( band.fwhmNm > 0.0f ) )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Sensor profile '%1' has an invalid band '%2'" )
                                    .arg( profile.id, band.name );
            return false;
        }
        profile.bands.append( band );
    }
    *out = std::move( profile );
    return true;
}

bool SensorProfile::loadSensors( const QString &path, QVector<SensorProfile> *out,
                                 QString *errorMessage )
{
    if ( !out )
        return false;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open sensor registry: %1" ).arg( path );
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    const QJsonArray sensorArray = doc.object().value( QStringLiteral( "sensors" ) ).toArray();
    if ( sensorArray.isEmpty() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Sensor registry %1 has no sensors" ).arg( path );
        return false;
    }
    QVector<SensorProfile> profiles;
    for ( const QJsonValue &value : sensorArray )
    {
        SensorProfile profile;
        if ( !SensorProfile::fromJson( value.toObject(), &profile, errorMessage ) )
            return false;
        profiles.append( std::move( profile ) );
    }
    *out = std::move( profiles );
    return true;
}

bool SensorProfile::loadSensor( const QString &path, const QString &sensorId,
                                SensorProfile *out, QString *errorMessage )
{
    QVector<SensorProfile> profiles;
    if ( !loadSensors( path, &profiles, errorMessage ) )
        return false;
    for ( const SensorProfile &profile : profiles )
    {
        if ( profile.id == sensorId )
        {
            *out = profile;
            return true;
        }
    }
    if ( errorMessage )
        *errorMessage = QStringLiteral( "Sensor '%1' not found in %2" ).arg( sensorId, path );
    return false;
}

bool validateLibrary( const Library &library, QStringList *errors )
{
    QStringList issues;
    const auto report = [ &issues ]( const QString &issue ) { issues.append( issue ); };

    if ( library.entries.isEmpty() )
        report( QStringLiteral( "library has no entries" ) );

    static const QRegularExpression slugPattern( QStringLiteral( "^[a-z0-9][a-z0-9._-]*$" ) );
    QSet<QString> seenIds;
    int index = 0;
    for ( const Entry &entry : library.entries )
    {
        const QString label = entry.id.isEmpty()
            ? QStringLiteral( "#%1" ).arg( index )
            : entry.id;
        const auto issue = [ &report, &label ]( const QString &detail )
        { report( QStringLiteral( "entry '%1': %2" ).arg( label, detail ) ); };

        if ( entry.id.isEmpty() )
            issue( QStringLiteral( "missing id (curated libraries require a stable slug id)" ) );
        else if ( !slugPattern.match( entry.id ).hasMatch() )
            issue( QStringLiteral( "invalid id '%1' (expected a lowercase slug)" ).arg( entry.id ) );
        else if ( seenIds.contains( entry.id ) )
            issue( QStringLiteral( "duplicate entry id" ) );
        seenIds.insert( entry.id );

        if ( entry.name.isEmpty() )
            issue( QStringLiteral( "missing name" ) );
        if ( entry.spectrum.empty() )
            issue( QStringLiteral( "empty spectrum" ) );

        for ( size_t i = 0; i < entry.spectrum.size(); ++i )
        {
            const float v = entry.spectrum[i];
            if ( !std::isfinite( v ) )
                issue( QStringLiteral( "reflectance[%1] is not finite" ).arg( i ) );
            else if ( v < 0.0f || v > 1.0f )
                issue( QStringLiteral( "reflectance[%1] = %2 outside [0, 1]" )
                           .arg( i ).arg( QString::number( v ) ) );
        }

        if ( entry.wavelengths.size() != entry.spectrum.size() )
        {
            issue( QStringLiteral( "wavelength grid size %1 does not match band count %2" )
                       .arg( entry.wavelengths.size() )
                       .arg( entry.spectrum.size() ) );
        }
        else
        {
            for ( size_t i = 0; i < entry.wavelengths.size(); ++i )
            {
                const float wl = entry.wavelengths[i];
                if ( !std::isfinite( wl ) )
                    issue( QStringLiteral( "wavelength[%1] is not finite" ).arg( i ) );
                else if ( i > 0 && !( wl > entry.wavelengths[i - 1] ) )
                    issue( QStringLiteral( "wavelength[%1] = %2 is not strictly increasing" )
                               .arg( i ).arg( QString::number( wl ) ) );
            }
        }

        if ( entry.fwhm.size() != entry.spectrum.size() )
        {
            issue( QStringLiteral( "fwhm grid size %1 does not match band count %2" )
                       .arg( entry.fwhm.size() )
                       .arg( entry.spectrum.size() ) );
        }
        else
        {
            for ( size_t i = 0; i < entry.fwhm.size(); ++i )
            {
                const float f = entry.fwhm[i];
                if ( !std::isfinite( f ) )
                    issue( QStringLiteral( "fwhm[%1] is not finite" ).arg( i ) );
                else if ( !( f > 0.0f ) )
                    issue( QStringLiteral( "fwhm[%1] = %2 is not positive" )
                               .arg( i ).arg( QString::number( f ) ) );
            }
        }

        for ( const QLatin1String field :
              { QLatin1String( "source" ), QLatin1String( "license" ), QLatin1String( "citation" ) } )
        {
            const QString value = field == QLatin1String( "source" ) ? entry.source
                : field == QLatin1String( "license" ) ? entry.license : entry.citation;
            if ( value.isEmpty() )
                issue( QStringLiteral( "missing provenance field '%1'" ).arg( QString( field ) ) );
        }

        if ( entry.synthetic && entry.derivation.isEmpty() )
            issue( QStringLiteral( "synthetic entry requires a non-empty 'derivation'" ) );

        ++index;
    }

    if ( errors )
        *errors = issues;
    return issues.isEmpty();
}

} // namespace SpectralLibrary

namespace SpectralLibrary
{

std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                       const Library &library,
                                       float nodata )
{
    return matchSpectrum( spectrum, {}, library, nodata );
}

std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                       const std::vector<float> &spectrumWavelengths,
                                       const Library &library,
                                       float nodata )
{
    std::vector<MatchScore> scores;
    if ( spectrum.empty() )
        return scores;

    const int bands = static_cast<int>( spectrum.size() );
    for ( int i = 0; i < library.entries.size(); ++i )
    {
        const Entry &entry = library.entries.at( i );
        const std::vector<float> &entryRef = entry.spectrum;
        // Test spectrum used for scoring: the original, or a wavelength-
        // resampled copy when band counts differ and both sides carry grids.
        const float *testPtr = spectrum.data();
        int testBands = bands;
        std::vector<float> resampledSpectrum;
        bool resampled = false;
        if ( static_cast<int>( entryRef.size() ) != bands )
        {
            if ( spectrumWavelengths.size() == spectrum.size()
                 && entry.wavelengths.size() == entryRef.size() )
            {
                resampledSpectrum.resize( entryRef.size() );
                const bool okResampling = (!entry.fwhm.empty() && entry.fwhm.size() == entryRef.size())
                    ? SpectralResampling::resampleSpectrumGaussian(
                           spectrum.data(), spectrumWavelengths.data(), bands,
                           entry.wavelengths.data(), entry.fwhm.data(), static_cast<int>( entryRef.size() ),
                           resampledSpectrum.data() )
                    : SpectralResampling::resampleSpectrum(
                           spectrum.data(), spectrumWavelengths.data(), bands,
                           entry.wavelengths.data(), static_cast<int>( entryRef.size() ),
                           resampledSpectrum.data() );

                if ( okResampling )
                {
                    bool hasOutOfRange = false;
                    for ( float v : resampledSpectrum )
                    {
                        if ( std::isnan( v ) )
                        {
                            hasOutOfRange = true;
                            break;
                        }
                    }
                    if ( !hasOutOfRange )
                    {
                        testPtr = resampledSpectrum.data();
                        testBands = static_cast<int>( entryRef.size() );
                        resampled = true;
                    }
                }
            }
            if ( !resampled )
                continue; // cannot compare (band mismatch, no wavelengths)
        }

        MatchScore score;
        score.entryIndex = i;
        score.name = entry.name;
        score.material = entry.material;
        score.resampled = resampled;
        score.angleDegrees =
            SpectralClassification::spectralAngle( testPtr, entryRef.data(),
                                                   testBands, nodata )
            * ( 180.0 / std::acos( -1.0 ) );
        score.divergence =
            SpectralClassification::spectralDivergence( testPtr, entryRef.data(),
                                                        testBands, nodata );
        scores.push_back( std::move( score ) );
    }

    // Ascending SAM angle; undefined (NaN) angles sort last.
    std::stable_sort( scores.begin(), scores.end(),
                      []( const MatchScore &a, const MatchScore &b )
    {
        const auto key = []( double v )
        {
            return std::isnan( v ) ? std::numeric_limits<double>::infinity() : v;
        };
        return key( a.angleDegrees ) < key( b.angleDegrees );
    } );
    return scores;
}

} // namespace SpectralLibrary
