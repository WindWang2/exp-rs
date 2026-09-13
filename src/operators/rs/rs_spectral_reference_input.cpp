// src/operators/rs/rs_spectral_reference_input.cpp — shared spectral reference seam
#include "rs_spectral_reference_input.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/spectral_library.h"
#include "processing/algorithms/spectral_resampling.h"
#include "processing/algorithms/spectral_table.h"
#include "processing/algorithms/spectral_wavelength.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <set>
#include <string>

namespace sicnu::operators::rs {

using namespace params;

namespace {

[[noreturn]] void refuse( ErrorCode code, const std::string &message )
{
    throw RSOperatorError( code, message );
}

QString gridExtentNm( const std::vector<float> &centers )
{
    if ( centers.empty() )
        return QStringLiteral( "without wavelengths" );
    return QStringLiteral( "[%1, %2] nm" )
        .arg( static_cast<double>( centers.front() ), 0, 'f', 1 )
        .arg( static_cast<double>( centers.back() ), 0, 'f', 1 );
}

SpectralWavelength::Grid gridOf( const std::vector<float> &centers,
                                 const std::vector<float> &fwhm )
{
    SpectralWavelength::Grid grid;
    grid.centersNm = centers;
    grid.fwhmNm = fwhm;
    return grid;
}

struct LoadedRows
{
    std::vector<std::vector<float>> spectra;
    std::vector<float> wavelengthsNm; ///< optional shared grid
    std::vector<float> fwhmNm;        ///< optional shared grid
    QStringList labels;
    QStringList materials;
    QString description;
    QString license;
    QString citation;
    bool synthetic = false;
};

/// Parse a JSON file into either a spectral table or a library. Returns
/// false with a typed message when the content matches neither contract.
bool loadRowsFromPath( const QString &path, const QString &refKey, LoadedRows *out,
                       QString *error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        *error = QStringLiteral( "%1: cannot open spectral reference file: %2" )
                     .arg( refKey, path );
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        *error = QStringLiteral( "%1: not a valid JSON spectral reference: %2 (%3)" )
                     .arg( refKey, path, parseError.errorString() );
        return false;
    }
    const QJsonObject root = doc.object();

    const QString kind = root[QStringLiteral( "kind" )].toString();
    if ( kind == SpectralTable::kKind )
    {
        SpectralTable::Table table;
        QString tableError;
        if ( !SpectralTable::loadValidated( path, &table, &tableError ) )
        {
            *error = QStringLiteral( "%1: %2" ).arg( refKey, tableError );
            return false;
        }
        out->spectra = table.spectra;
        out->wavelengthsNm = table.wavelengthsNm;
        out->fwhmNm = table.fwhmNm;
        out->labels = table.labels;
        out->materials = table.materials;
        out->description = QStringLiteral( "spectral table '%1' (%2 spectra x %3 bands)" )
                               .arg( table.id )
                               .arg( table.count() )
                               .arg( table.bandCount );
        if ( !table.labels.isEmpty() )
            out->description += QStringLiteral( ", labels [%1]" )
                                    .arg( table.labels.join( ", " ) );
        out->license = table.license;
        out->citation = table.citation;
        out->synthetic = table.provenance.synthetic;
        return true;
    }

    // Library shape: root "entries" array. loadValidated applies the curation
    // rules; v1 libraries without an id still load.
    if ( root.contains( QStringLiteral( "entries" ) ) )
    {
        SpectralLibrary::Library library;
        QString libraryError;
        if ( !SpectralLibrary::Library::loadValidated( path, &library, &libraryError ) )
        {
            *error = QStringLiteral( "%1: %2" ).arg( refKey, libraryError );
            return false;
        }
        out->spectra.reserve( static_cast<size_t>( library.entries.size() ) );
        QStringList materials;
        for ( const auto &entry : library.entries )
        {
            out->spectra.push_back( entry.spectrum );
            materials.append( entry.material );
            out->labels.append( entry.name );
        }
        out->materials = materials;
        out->wavelengthsNm = library.wavelengths();
        out->fwhmNm = library.fwhm();
        // Table-level claims stay conservative: a library is "synthetic" only
        // when every entry is, and the license echoes only when uniform.
        bool allSynthetic = !library.entries.isEmpty();
        bool uniformLicense = !library.entries.isEmpty();
        bool anyLicense = false;
        QString firstLicense;
        for ( const auto &entry : library.entries )
        {
            allSynthetic = allSynthetic && entry.synthetic;
            if ( firstLicense.isEmpty() && !entry.license.isEmpty() )
                firstLicense = entry.license;
            anyLicense = anyLicense || !entry.license.isEmpty();
            if ( !entry.license.isEmpty() && entry.license != firstLicense )
                uniformLicense = false;
        }
        out->synthetic = allSynthetic;
        out->license = !anyLicense ? QString()
                       : ( uniformLicense ? firstLicense : QStringLiteral( "mixed" ) );
        out->description = QStringLiteral( "spectral library '%1' (%2 entries)" )
                               .arg( library.libraryId().isEmpty()
                                         ? QStringLiteral( "<unnamed>" )
                                         : library.libraryId() )
                               .arg( library.entries.size() );
        return true;
    }

    *error = QStringLiteral( "%1: unrecognized spectral reference (expected kind "
                             "\"%2\" or a spectral library): %3" )
                 .arg( refKey, SpectralTable::kKind, path );
    return false;
}

/// Apply a material filter to loaded library rows (no-op for an empty filter).
void applyMaterialFilter( const QStringList &filter, LoadedRows *rows )
{
    if ( filter.isEmpty() )
        return;
    const std::set<QString> wanted( filter.cbegin(), filter.cend() );

    std::vector<std::vector<float>> spectra;
    QStringList labels;
    QStringList materials;
    for ( int i = 0; i < rows->materials.size() && i < static_cast<int>( rows->spectra.size() );
          ++i )
    {
        if ( !wanted.count( rows->materials.at( i ) ) )
            continue;
        spectra.push_back( rows->spectra[static_cast<size_t>( i )] );
        labels.append( rows->labels.value( i ) );
        materials.append( rows->materials.at( i ) );
    }
    if ( spectra.empty() )
        refuse( ErrorCode::InvalidParameter,
                "libraryMaterials filter matched no entries; requested [" +
                    filter.join( ", " ).toStdString() + "] but the library offers [" +
                    rows->materials.join( ", " ).toStdString() + "]" );
    rows->spectra = std::move( spectra );
    rows->labels = labels;
    rows->materials = materials;
    rows->description += QStringLiteral( ", material filter [%1]" ).arg( filter.join( ", " ) );
}

/// Reconcile reference rows onto the input band grid; fills out->flat/width.
void reconcileWidths( const LoadedRows &rows, const RasterWavelengthGrid &inputGrid,
                      int inputBandCount, const char *refKey, const QString &path,
                      ResolvedSpectralReference *out )
{
    const int sourceWidth =
        rows.spectra.empty() ? 0 : static_cast<int>( rows.spectra.front().size() );
    const bool sourceHasGrid = !rows.wavelengthsNm.empty();

    if ( inputGrid.present && sourceHasGrid )
    {
        std::string reason;
        if ( !SpectralWavelength::rangesOverlap( inputGrid.grid,
                                                 gridOf( rows.wavelengthsNm, rows.fwhmNm ),
                                                 &reason ) )
        {
            refuse( ErrorCode::InvalidInputData,
                    std::string( refKey ) + ": " + reason + " — input " +
                        gridExtentNm( inputGrid.grid.centersNm ).toStdString() +
                        ", reference " + gridExtentNm( rows.wavelengthsNm ).toStdString() +
                        " (" + path.toStdString() + ")" );
        }

        const bool useGaussian = inputGrid.grid.hasFwhm() && !rows.fwhmNm.empty();
        out->flat.reserve( rows.spectra.size() * static_cast<size_t>( inputBandCount ) );
        std::vector<float> resampled( static_cast<size_t>( inputBandCount ), 0.0f );
        for ( size_t r = 0; r < rows.spectra.size(); ++r )
        {
            const auto &src = rows.spectra[r];
            const int srcBands = static_cast<int>( src.size() );
            if ( srcBands < 2 || static_cast<int>( rows.wavelengthsNm.size() ) != srcBands )
                refuse( ErrorCode::InvalidInputData,
                        std::string( refKey ) + ": spectrum " + std::to_string( r ) +
                            " lacks a usable wavelength grid for resampling (" +
                            path.toStdString() + ")" );
            const bool ok =
                useGaussian
                    ? SpectralResampling::resampleSpectrumGaussian(
                          src.data(), rows.wavelengthsNm.data(), srcBands,
                          inputGrid.grid.centersNm.data(), inputGrid.grid.fwhmNm.data(),
                          inputBandCount, resampled.data() )
                    : SpectralResampling::resampleSpectrum(
                          src.data(), rows.wavelengthsNm.data(), srcBands,
                          inputGrid.grid.centersNm.data(), inputBandCount, resampled.data() );
            if ( !ok )
                refuse( ErrorCode::InvalidInputData,
                        std::string( refKey ) + ": failed to resample spectrum " +
                            std::to_string( r ) + " onto the input band grid (" +
                            path.toStdString() + ")" );
            for ( int b = 0; b < inputBandCount; ++b )
                if ( !std::isfinite( resampled[static_cast<size_t>( b )] ) )
                    refuse( ErrorCode::InvalidInputData,
                            std::string( refKey ) + ": input band " +
                                std::to_string( b + 1 ) + " (" +
                                std::to_string( inputGrid.grid.centersNm[static_cast<size_t>( b )] ) +
                                " nm) lies outside the reference wavelength coverage " +
                                gridExtentNm( rows.wavelengthsNm ).toStdString() +
                                "; select input bands inside the coverage (" +
                                path.toStdString() + ")" );
            out->flat.insert( out->flat.end(), resampled.begin(), resampled.end() );
        }
        out->width = inputBandCount;
        out->count = static_cast<int>( rows.spectra.size() );
        out->resampled = true;
        return;
    }

    // No wavelength reconciliation possible: exact width equality required.
    if ( sourceWidth != inputBandCount )
    {
        const char *missingSide = inputGrid.present
                                      ? "the reference lacks wavelength metadata"
                                      : "the input raster lacks WAVELENGTH band metadata";
        refuse( ErrorCode::InvalidParameter,
                std::string( refKey ) + ": reference width " +
                    std::to_string( sourceWidth ) + " != input band count " +
                    std::to_string( inputBandCount ) +
                    "; resampling requires wavelength metadata on both sides, but " +
                    missingSide + " (" + path.toStdString() + ")" );
    }
    for ( const auto &src : rows.spectra )
        out->flat.insert( out->flat.end(), src.begin(), src.end() );
    out->width = inputBandCount;
    out->count = static_cast<int>( rows.spectra.size() );
}

} // namespace

RasterWavelengthGrid RasterWavelengthGrid::read( const GdalDatasetWrapper &ds,
                                                 const std::vector<int> &bands,
                                                 QString *error )
{
    RasterWavelengthGrid result;
    std::vector<std::pair<double, std::string>> wavelengths;
    std::vector<std::pair<double, std::string>> fwhm;
    int withWavelength = 0;

    for ( int band : bands )
    {
        const QString wl = ds.bandMetadataItem( band, "WAVELENGTH" );
        if ( wl.isEmpty() )
        {
            wavelengths.emplace_back( 0.0, std::string() );
            fwhm.emplace_back( 0.0, std::string() );
            continue;
        }
        ++withWavelength;
        bool ok = false;
        const double value = wl.toDouble( &ok );
        if ( !ok )
        {
            if ( error )
                *error = QStringLiteral( "band %1 WAVELENGTH is not numeric: %2" )
                             .arg( band )
                             .arg( wl );
            return result;
        }
        const QString units = ds.bandMetadataItem( band, "WAVELENGTH_UNITS" );
        wavelengths.emplace_back( value, units.toStdString() );

        const QString fw = ds.bandMetadataItem( band, "FWHM" );
        if ( fw.isEmpty() )
        {
            fwhm.emplace_back( 0.0, std::string() );
        }
        else
        {
            const double fValue = fw.toDouble( &ok );
            if ( !ok )
            {
                if ( error )
                    *error = QStringLiteral( "band %1 FWHM is not numeric: %2" )
                                 .arg( band )
                                 .arg( fw );
                return result;
            }
            const QString fUnits = ds.bandMetadataItem( band, "FWHM_UNITS" );
            fwhm.emplace_back( fValue, fUnits.toStdString() );
        }
    }

    if ( withWavelength == 0 )
        return result; // absent: a legal state, consumers demand width equality
    if ( withWavelength != static_cast<int>( bands.size() ) )
    {
        if ( error )
            *error = QStringLiteral( "partial wavelength metadata: %1 of %2 selected bands "
                                     "carry WAVELENGTH" )
                         .arg( withWavelength )
                         .arg( static_cast<int>( bands.size() ) );
        return result;
    }

    SpectralWavelength::Grid grid;
    const SpectralWavelength::Status status =
        SpectralWavelength::gridFromBandValues( wavelengths, fwhm, &grid );
    if ( status != SpectralWavelength::Status::Ok )
    {
        if ( error )
            *error = QStringLiteral( "invalid input wavelength grid: %1" )
                         .arg( SpectralWavelength::statusText( status ) );
        return result;
    }
    result.grid = std::move( grid );
    result.present = true;
    return result;
}

const float *ResolvedSpectralReference::single() const
{
    if ( count != 1 || flat.empty() )
        return nullptr;
    return flat.data();
}

ResolvedSpectralReference resolveSpectralReference(
    const Json::Value &params, const char *inlineKey, const char *refKey,
    const GdalDatasetWrapper &input, const std::vector<int> &bands,
    const RasterWavelengthGrid &inputGrid )
{
    Q_UNUSED( input ); // band metadata enters through inputGrid; kept for call-site clarity

    const int inputBandCount = static_cast<int>( bands.size() );

    const bool hasInline = params.isMember( inlineKey ) && params[inlineKey].isArray()
                           && !params[inlineKey].empty();
    const bool hasRef = params.isMember( refKey ) && params[refKey].isString()
                        && !params[refKey].asString().empty();
    const bool hasLibrary = params.isMember( "libraryPath" ) && params["libraryPath"].isString()
                            && !params["libraryPath"].asString().empty();

    int sources = 0;
    if ( hasInline )
        ++sources;
    if ( hasRef )
        ++sources;
    if ( hasLibrary )
        ++sources;
    if ( sources == 0 )
        refuse( ErrorCode::InvalidParameter,
                std::string( "one of '" ) + inlineKey + "', '" + refKey +
                    "' or 'libraryPath' is required" );
    if ( sources > 1 )
        refuse( ErrorCode::InvalidParameter,
                std::string( "ambiguous spectral reference: supply only one of '" ) +
                    inlineKey + "', '" + refKey + "', 'libraryPath'" );

    ResolvedSpectralReference out;

    if ( hasInline )
    {
        // Legacy contract: exact width match, no wavelength reconciliation.
        const Json::Value &arr = params[inlineKey];
        for ( Json::ArrayIndex r = 0; r < arr.size(); ++r )
        {
            const Json::Value &row = arr[r];
            if ( !row.isArray() || static_cast<int>( row.size() ) != inputBandCount )
                refuse( ErrorCode::InvalidParameter,
                        std::string( inlineKey ) + " spectrum " + std::to_string( r ) +
                            " must be an array of " + std::to_string( inputBandCount ) +
                            " numbers" );
            for ( Json::ArrayIndex b = 0; b < row.size(); ++b )
            {
                if ( !row[b].isNumeric() )
                    refuse( ErrorCode::InvalidParameter,
                            std::string( inlineKey ) + " spectrum " + std::to_string( r ) +
                                " contains a non-numeric value" );
                out.flat.push_back( static_cast<float>( row[b].asDouble() ) );
            }
        }
        out.width = inputBandCount;
        out.count = static_cast<int>( arr.size() );
        out.sourceDescription = QStringLiteral( "inline %1 (%2 spectra)" )
                                    .arg( inlineKey )
                                    .arg( out.count );
        return out;
    }

    const std::string path = hasLibrary ? params["libraryPath"].asString()
                                        : params[refKey].asString();
    const char *usedKey = hasLibrary ? "libraryPath" : refKey;
    LoadedRows rows;
    QString error;
    if ( !loadRowsFromPath( QString::fromStdString( path ), QString::fromLatin1( usedKey ),
                            &rows, &error ) )
        refuse( ErrorCode::InvalidParameter, error.toStdString() );
    if ( hasLibrary )
        applyMaterialFilter(
            [&params]() {
                QStringList filter;
                for ( const std::string &m : params::getStringArray( params, "libraryMaterials" ) )
                    filter.append( QString::fromStdString( m ) );
                return filter;
            }(),
            &rows );

    reconcileWidths( rows, inputGrid, inputBandCount, usedKey,
                     QString::fromStdString( path ), &out );

    out.labels = rows.labels;
    out.materials = rows.materials;
    out.sourceDescription = rows.description;
    out.license = rows.license;
    out.citation = rows.citation;
    out.synthetic = rows.synthetic;
    return out;
}

Json::Value referenceInputSchemaProps( const char *inlineKey, const char *refKey,
                                       const char *inlineDescription )
{
    Json::Value props( Json::objectValue );

    Json::Value inlineArr( Json::objectValue );
    inlineArr["type"] = "array";
    inlineArr["description"] = inlineDescription;
    props[inlineKey] = inlineArr;

    Json::Value ref( Json::objectValue );
    ref["type"] = "string";
    ref["description"] =
        "Path to a spectral-table artifact (exp-rs:spectral-table) or a spectral "
        "library JSON; wavelength metadata on both sides enables automatic resampling "
        "onto the input bands";
    props[refKey] = ref;

    Json::Value library( Json::objectValue );
    library["type"] = "string";
    library["description"] =
        "Path to a spectral library JSON (validated); use libraryMaterials to select entries";
    props["libraryPath"] = library;

    Json::Value materials( Json::objectValue );
    materials["type"] = "array";
    materials["description"] = "Material labels to select from the library (requires libraryPath)";
    materials["items"]["type"] = "string";
    props["libraryMaterials"] = materials;

    return props;
}

} // namespace sicnu::operators::rs
