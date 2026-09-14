// src/processing/algorithms/spectral_table.cpp — typed spectral table artifact
#include "spectral_table.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>
#include <string>

namespace SpectralTable
{
namespace
{

QString formatCell( float v )
{
    // %.9g round-trips IEEE float exactly; locale-independent via snprintf.
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%.9g", static_cast<double>( v ) );
    return QString::fromLatin1( buf );
}

bool sizeInt( const QJsonValue &v, int *out )
{
    if ( !v.isDouble() )
        return false;
    const double d = v.toDouble();
    if ( d < 0.0 || d > 2147483647.0 || d != static_cast<double>( static_cast<int>( d ) ) )
        return false;
    *out = static_cast<int>( d );
    return true;
}

} // namespace

QString digestHex( const std::vector<std::vector<float>> &spectra, int bandCount )
{
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    const QString header = QStringLiteral( "%1/v%2/%3x%4\n" )
                             .arg( kKind,
                                   QString::number( kFormatVersion ),
                                   QString::number( spectra.size() ),
                                   QString::number( bandCount ) );
    hash.addData( header.toUtf8() );
    for ( const auto &row : spectra )
    {
        for ( int b = 0; b < bandCount; ++b )
        {
            const float v = row[static_cast<size_t>( b )];
            hash.addData( formatCell( v ).toUtf8() );
            hash.addData( "\n", 1 );
        }
    }
    return QString::fromLatin1( hash.result().toHex() );
}

bool validate( const Table &table, QStringList *errors )
{
    QStringList local;
    auto fail = [&local]( const QString &issue ) { local.append( issue ); };

    if ( table.id.trimmed().isEmpty() )
        fail( QStringLiteral( "id: empty" ) );
    if ( table.bandCount <= 0 )
        fail( QStringLiteral( "bandCount: must be positive, got %1" ).arg( table.bandCount ) );
    if ( table.spectra.empty() )
        fail( QStringLiteral( "spectra: must contain at least one row" ) );

    long long cells = 0;
    bool widthsConsistent = true;
    for ( int r = 0; r < static_cast<int>( table.spectra.size() ); ++r )
    {
        const auto &row = table.spectra[static_cast<size_t>( r )];
        if ( static_cast<int>( row.size() ) != table.bandCount )
        {
            fail( QStringLiteral( "spectra[%1]: width %2 != bandCount %3" )
                      .arg( r ).arg( row.size() ).arg( table.bandCount ) );
            widthsConsistent = false;
            continue;
        }
        cells += static_cast<long long>( row.size() );
        for ( int b = 0; b < table.bandCount; ++b )
        {
            if ( !std::isfinite( row[static_cast<size_t>( b )] ) )
            {
                fail( QStringLiteral( "spectra[%1][%2]: non-finite value" ).arg( r ).arg( b ) );
                break; // one issue per row is enough; widths stay verified
            }
        }
    }
    if ( cells > kMaxCells )
        fail( QStringLiteral( "size: %1 cells exceed the %2-cell bound" )
                  .arg( cells ).arg( kMaxCells ) );

    if ( !table.wavelengthsNm.empty()
         && static_cast<int>( table.wavelengthsNm.size() ) != table.bandCount )
        fail( QStringLiteral( "wavelengths: size %1 != bandCount %2" )
                  .arg( table.wavelengthsNm.size() ).arg( table.bandCount ) );
    bool wlOk = true;
    for ( size_t i = 0; i < table.wavelengthsNm.size(); ++i )
    {
        const float w = table.wavelengthsNm[i];
        if ( !std::isfinite( w ) || w <= 0.0f
             || ( i > 0 && w <= table.wavelengthsNm[i - 1] ) )
        {
            fail( QStringLiteral( "wavelengths[%1]: must be finite, positive and "
                                  "strictly increasing, got %2" )
                      .arg( i ).arg( formatCell( w ) ) );
            wlOk = false;
            break;
        }
    }
    if ( !table.fwhmNm.empty() )
    {
        if ( static_cast<int>( table.fwhmNm.size() ) != table.bandCount )
            fail( QStringLiteral( "fwhm: size %1 != bandCount %2" )
                      .arg( table.fwhmNm.size() ).arg( table.bandCount ) );
        else if ( wlOk )
            for ( size_t i = 0; i < table.fwhmNm.size(); ++i )
                if ( !std::isfinite( table.fwhmNm[i] ) || table.fwhmNm[i] <= 0.0f )
                {
                    fail( QStringLiteral( "fwhm[%1]: must be finite and positive, got %2" )
                              .arg( i ).arg( formatCell( table.fwhmNm[i] ) ) );
                    break;
                }
    }

    if ( !table.labels.isEmpty()
         && table.labels.size() != static_cast<int>( table.spectra.size() ) )
        fail( QStringLiteral( "labels: size %1 != spectrum count %2" )
                  .arg( table.labels.size() ).arg( table.spectra.size() ) );
    if ( !table.materials.isEmpty()
         && table.materials.size() != static_cast<int>( table.spectra.size() ) )
        fail( QStringLiteral( "materials: size %1 != spectrum count %2" )
                  .arg( table.materials.size() ).arg( table.spectra.size() ) );

    if ( !table.provenance.synthetic && !table.provenance.derived
         && ( table.license.trimmed().isEmpty() || table.citation.trimmed().isEmpty() ) )
        fail( QStringLiteral( "provenance: measured field tables (synthetic=false, "
                              "derived=false) require non-empty license and citation" ) );

    // The digest walk indexes row[b] for b in [0, bandCount): only safe once
    // every row width is verified — width violations are already reported.
    if ( !table.digestHex.isEmpty() && widthsConsistent )
    {
        const QString computed = digestHex( table.spectra, table.bandCount );
        if ( computed != table.digestHex )
            fail( QStringLiteral( "digest: stored %1 != computed %2 (content changed "
                                  "or corrupt)" )
                      .arg( table.digestHex, computed ) );
    }

    if ( errors )
        errors->append( local );
    return local.isEmpty();
}

QJsonObject toJson( const Table &table )
{
    QJsonObject root;
    root[QStringLiteral( "kind" )] = kKind;
    root[QStringLiteral( "version" )] = kFormatVersion;
    root[QStringLiteral( "id" )] = table.id;
    root[QStringLiteral( "bandCount" )] = table.bandCount;

    QJsonArray spectra;
    for ( const auto &row : table.spectra )
    {
        QJsonArray r;
        for ( int b = 0; b < table.bandCount; ++b )
            r.append( row[static_cast<size_t>( b )] );
        spectra.append( r );
    }
    root[QStringLiteral( "spectra" )] = spectra;

    if ( !table.wavelengthsNm.empty() )
    {
        QJsonArray wl;
        for ( float w : table.wavelengthsNm )
            wl.append( w );
        root[QStringLiteral( "wavelengthsNm" )] = wl;
    }
    if ( !table.fwhmNm.empty() )
    {
        QJsonArray fw;
        for ( float f : table.fwhmNm )
            fw.append( f );
        root[QStringLiteral( "fwhmNm" )] = fw;
    }
    if ( !table.labels.isEmpty() )
    {
        QJsonArray l;
        for ( const QString &s : table.labels )
            l.append( s );
        root[QStringLiteral( "labels" )] = l;
    }
    if ( !table.materials.isEmpty() )
    {
        QJsonArray m;
        for ( const QString &s : table.materials )
            m.append( s );
        root[QStringLiteral( "materials" )] = m;
    }

    QJsonObject prov;
    prov[QStringLiteral( "sourceOperator" )] = table.provenance.sourceOperator;
    prov[QStringLiteral( "sourceInput" )] = table.provenance.sourceInput;
    prov[QStringLiteral( "parameters" )] = table.provenance.parameters;
    prov[QStringLiteral( "createdAtMs" )] = static_cast<qint64>( table.provenance.createdAtMs );
    prov[QStringLiteral( "synthetic" )] = table.provenance.synthetic;
    prov[QStringLiteral( "derived" )] = table.provenance.derived;
    root[QStringLiteral( "provenance" )] = prov;

    if ( !table.license.isEmpty() )
        root[QStringLiteral( "license" )] = table.license;
    if ( !table.citation.isEmpty() )
        root[QStringLiteral( "citation" )] = table.citation;

    root[QStringLiteral( "digest" )] = digestHex( table.spectra, table.bandCount );
    return root;
}

bool fromJson( const QJsonObject &json, Table *out, QString *errorMessage )
{
    auto fail = [errorMessage]( const QString &msg ) {
        if ( errorMessage )
            *errorMessage = msg;
        return false;
    };

    if ( json[QStringLiteral( "kind" )].toString() != kKind )
        return fail( QStringLiteral( "kind: expected %1, got %2" )
                         .arg( kKind, json[QStringLiteral( "kind" )].toString( QStringLiteral( "<missing>" ) ) ) );
    if ( json[QStringLiteral( "version" )].toInt( 0 ) != kFormatVersion )
        return fail( QStringLiteral( "version: expected %1, got %2" )
                         .arg( kFormatVersion ).arg( json[QStringLiteral( "version" )].toInt( 0 ) ) );

    Table t;
    t.id = json[QStringLiteral( "id" )].toString();
    if ( t.id.trimmed().isEmpty() )
        return fail( QStringLiteral( "id: missing or empty" ) );
    if ( !sizeInt( json[QStringLiteral( "bandCount" )], &t.bandCount ) || t.bandCount <= 0 )
        return fail( QStringLiteral( "bandCount: missing or invalid" ) );

    const QJsonArray spectra = json[QStringLiteral( "spectra" )].toArray();
    if ( spectra.isEmpty() )
        return fail( QStringLiteral( "spectra: missing or empty" ) );
    const long long cells = static_cast<long long>( spectra.size() ) * t.bandCount;
    if ( cells > kMaxCells )
        return fail( QStringLiteral( "size: %1 cells exceed the %2-cell bound" )
                         .arg( cells ).arg( kMaxCells ) );
    t.spectra.reserve( static_cast<size_t>( spectra.size() ) );
    for ( int r = 0; r < spectra.size(); ++r )
    {
        const QJsonArray row = spectra.at( r ).toArray();
        if ( row.size() != t.bandCount )
            return fail( QStringLiteral( "spectra[%1]: width %2 != bandCount %3" )
                             .arg( r ).arg( row.size() ).arg( t.bandCount ) );
        std::vector<float> values( static_cast<size_t>( t.bandCount ) );
        for ( int b = 0; b < t.bandCount; ++b )
        {
            const QJsonValue cell = row.at( b );
            if ( !cell.isDouble() )
                return fail( QStringLiteral( "spectra[%1][%2]: not a number" ).arg( r ).arg( b ) );
            const float v = static_cast<float>( cell.toDouble() );
            if ( !std::isfinite( v ) )
                return fail( QStringLiteral( "spectra[%1][%2]: non-finite value" ).arg( r ).arg( b ) );
            values[static_cast<size_t>( b )] = v;
        }
        t.spectra.push_back( std::move( values ) );
    }

    auto readFloatArray = [&json, &t, &fail]( const char *key, std::vector<float> *dst ) -> bool {
        const QJsonArray arr = json[QLatin1String( key )].toArray();
        if ( arr.isEmpty() )
            return true;
        if ( arr.size() != t.bandCount )
            return fail( QStringLiteral( "%1: size %2 != bandCount %3" )
                             .arg( key ).arg( arr.size() ).arg( t.bandCount ) );
        dst->resize( static_cast<size_t>( t.bandCount ) );
        for ( int b = 0; b < t.bandCount; ++b )
        {
            if ( !arr.at( b ).isDouble() )
                return fail( QStringLiteral( "%1[%2]: not a number" ).arg( key ).arg( b ) );
            ( *dst )[static_cast<size_t>( b )] = static_cast<float>( arr.at( b ).toDouble() );
        }
        return true;
    };
    if ( !readFloatArray( "wavelengthsNm", &t.wavelengthsNm ) )
        return false;
    if ( !readFloatArray( "fwhmNm", &t.fwhmNm ) )
        return false;

    auto readStringArray = [&json]( const char *key ) {
        QStringList out;
        const QJsonArray arr = json[QLatin1String( key )].toArray();
        for ( const auto &v : arr )
            out.append( v.toString() );
        return out;
    };
    t.labels = readStringArray( "labels" );
    t.materials = readStringArray( "materials" );

    const QJsonObject prov = json[QStringLiteral( "provenance" )].toObject();
    t.provenance.sourceOperator = prov[QStringLiteral( "sourceOperator" )].toString();
    t.provenance.sourceInput = prov[QStringLiteral( "sourceInput" )].toString();
    t.provenance.parameters = prov[QStringLiteral( "parameters" )].toString();
    t.provenance.createdAtMs = static_cast<qint64>( prov[QStringLiteral( "createdAtMs" )].toDouble( 0.0 ) );
    t.provenance.synthetic = prov[QStringLiteral( "synthetic" )].toBool( false );
    t.provenance.derived = prov[QStringLiteral( "derived" )].toBool( false );

    t.license = json[QStringLiteral( "license" )].toString();
    t.citation = json[QStringLiteral( "citation" )].toString();
    t.digestHex = json[QStringLiteral( "digest" )].toString();

    if ( !t.digestHex.isEmpty() )
    {
        const QString computed = digestHex( t.spectra, t.bandCount );
        if ( computed != t.digestHex )
            return fail( QStringLiteral( "digest: stored %1 != computed %2 (content "
                                         "changed or corrupt)" )
                             .arg( t.digestHex, computed ) );
    }

    *out = std::move( t );
    return true;
}

bool save( const Table &table, const QString &path, QString *errorMessage )
{
    QStringList errors;
    if ( !validate( table, &errors ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Refusing to save an invalid table: %1" )
                                .arg( errors.join( QStringLiteral( "; " ) ) );
        return false;
    }
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open %1 for writing" ).arg( path );
        return false;
    }
    const QJsonDocument doc( toJson( table ) );
    const QByteArray bytes = doc.toJson( QJsonDocument::Compact );
    if ( file.write( bytes ) != bytes.size() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Short write to %1" ).arg( path );
        return false;
    }
    return true;
}

bool load( const QString &path, Table *out, QString *errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open table file: %1" ).arg( path );
        return false;
    }
    // Read-nothing precheck: the kMaxCells bound caps the legal JSON size far
    // below this (worst-case ~20 bytes per cell); refuse absurd files without
    // materializing them.
    constexpr qint64 kMaxTableFileBytes = 256LL * 1024LL * 1024LL;
    if ( file.size() > kMaxTableFileBytes )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Table file is %1 bytes, above the %2-byte "
                                            "pre-read bound" )
                                .arg( file.size() )
                                .arg( kMaxTableFileBytes );
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid JSON in %1: %2" )
                                .arg( path, parseError.errorString() );
        return false;
    }
    return fromJson( doc.object(), out, errorMessage );
}

bool loadValidated( const QString &path, Table *out, QString *errorMessage )
{
    Table t;
    if ( !load( path, &t, errorMessage ) )
        return false;
    QStringList errors;
    if ( !validate( t, &errors ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid spectral table %1: %2" )
                                .arg( path, errors.join( QStringLiteral( "; " ) ) );
        return false;
    }
    *out = std::move( t );
    return true;
}

} // namespace SpectralTable
