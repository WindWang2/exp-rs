/***************************************************************************
 * rs_product_import_plan.cpp — standardized CN product import plan service
 * (ADR 0147).
 ***************************************************************************/
#include "rs_product_import_plan.h"

#include "rs_cn_import_operator.h"
#include "geospatial/util/sha256.h"
#include "processing/algorithms/satellite_products.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace sicnu::operators::rs {

using namespace sicnu::geo;

namespace {

/// PMS-style directories carry two image/sidecar pairs (MSS + PAN). Beyond
/// the sidecar's own image, report the sibling pair with its declared role
/// (pan/ms) so plans can express the PAN/MS relationship instead of leaving
/// it implicit in directory contents.
void resolvePmsSibling( const QString &sidecarPath,
                        QString *siblingPath, QString *siblingRole )
{
    *siblingPath = QString();
    *siblingRole = QString();
    if ( sidecarPath.isEmpty() )
        return;

    const QFileInfo sidecarInfo( sidecarPath );
    const QString sidecarName = sidecarInfo.fileName();
    const auto roleOfName = []( const QString &name ) -> QString {
        // Boundary-anchored: "SPAN"/"JAPAN" must not read as PAN.
        static const QRegularExpression panRe( QStringLiteral( "(^|[^A-Z])PAN([0-9]|$)" ) );
        if ( panRe.match( name.toUpper() ).hasMatch() )
            return QStringLiteral( "pan" );
        const QString upper = name.toUpper();
        if ( upper.contains( "MSS" ) || upper.contains( "MSC" ) || upper.contains( "WFV" ) )
            return QStringLiteral( "ms" );
        return QString();
    };
    const QString sidecarRole = roleOfName( sidecarName );
    if ( sidecarRole.isEmpty() )
        return;

    // Enumerate sibling sidecars (bounded) with the opposite role and the
    // same stem-family (PMS1/PAN1 share the prefix before the mode token).
    const QString dir = sidecarInfo.absolutePath();
    QDir directory( dir );
    const QStringList entries =
        directory.entryList( QStringList() << QStringLiteral( "*.xml" ), QDir::Files, QDir::Name );
    constexpr int kMaxSidecars = 64;
    int visited = 0;
    for ( const QString &entry : entries )
    {
        if ( ++visited > kMaxSidecars )
            break;
        const QString entryPath = directory.absoluteFilePath( entry );
        if ( entryPath == sidecarPath )
            continue;
        const QString entryRole = roleOfName( entry );
        if ( entryRole.isEmpty() || entryRole == sidecarRole )
            continue;
        // The sibling image must exist beside the sibling sidecar (the same
        // stem-pairing rule as the primary image).
        const QString siblingImage =
            QString::fromStdString( cnLocateImageTiff( entryPath.toStdString() ) );
        if ( siblingImage.isEmpty() )
            continue;
        *siblingPath = siblingImage;
        *siblingRole = entryRole;
        return;
    }
}

} // namespace

Json::Value ProductImportPlan::toJson() const
{
    Json::Value plan( Json::objectValue );
    plan["productFamily"] = identity.kindName;
    plan["satellite"] = profile.satellite.empty() ? identity.satellite : profile.satellite;
    plan["sensor_mode"] = profile.sensorMode.empty() ? identity.sensorMode : profile.sensorMode;
    plan["sensor_key"] = sensorKey;
    plan["sensor_profile"] = profile.sensorKey;
    plan["sensor_profile_version"] = profile.schemaVersion;
    plan["modality"] = profile.modality;
    if ( profile.hasGsdM )
        plan["nominal_gsd_m"] = profile.gsdM;
    if ( !profile.calibrationRule.empty() )
        plan["calibration_rule"] = profile.calibrationRule;
    Json::Value constituents( Json::arrayValue );
    for ( const std::string &c : profile.constituents )
        constituents.append( c );
    plan["expected_constituents"] = constituents;
    plan["sidecar"] = sidecarPath.toStdString();
    plan["image"] = imagePath.toStdString();
    if ( !rpcPath.isEmpty() )
        plan["rpc"] = rpcPath.toStdString();
    if ( !siblingImagePath.isEmpty() )
    {
        plan["sibling_image"] = siblingImagePath.toStdString();
        plan["sibling_role"] = siblingImageRole.toStdString();
    }
    plan["band_source"] = bandOrderDeclared ? "declared_band_ids" : "band_role_table";
    plan["band_order_unverified"] = !bandOrderDeclared;
    Json::Value bands( Json::arrayValue );
    for ( const QString &band : bandNames )
    {
        Json::Value entry( Json::objectValue );
        entry["band"] = band.toStdString();
        if ( const SensorBandProfile *spec = profile.findBand( band.toStdString() ) )
        {
            entry["role"] = spec->role;
            if ( !spec->roleReason.empty() )
                entry["role_reason"] = spec->roleReason;
            if ( spec->hasWavelengthNm )
                entry["wavelength_nm"] = spec->wavelengthNm;
            if ( spec->hasCenterWavelengthNm )
                entry["center_wavelength_nm"] = spec->centerWavelengthNm;
            if ( spec->hasFwhmNm )
                entry["fwhm_nm"] = spec->fwhmNm;
            if ( !spec->spectralRangeUm.empty() )
                entry["spectral_range_um"] = spec->spectralRangeUm;
        }
        else
        {
            entry["role"] = "unknown";
            entry["role_reason"] = "band not present in sensor profile " + sensorKey;
        }
        bands.append( entry );
    }
    plan["bands"] = bands;
    plan["completeness"] = productCompletenessName( completeness );
    Json::Value missing( Json::arrayValue );
    for ( const QString &m : missingConstituents )
        missing.append( m.toStdString() );
    plan["missing_constituents"] = missing;
    Json::Value warningsJson( Json::arrayValue );
    for ( const QString &w : warnings )
        warningsJson.append( w.toStdString() );
    plan["warnings"] = warningsJson;
    plan["metadata"] = metadata.toJson();
    return plan;
}

ProductImportPlan planCnProductImport( const std::string &input, const char *familyFilter )
{
    ProductImportPlan plan;

    // ── identify ────────────────────────────────────────────────────────────
    plan.identity = cnIdentifyProduct( input );
    if ( !plan.identity.supported )
    {
        Json::Value details;
        details["input"] = input;
        if ( !plan.identity.reason.empty() )
            details["reason"] = plan.identity.reason;
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               plan.identity.reason.empty()
                                 ? "Input does not name a supported Chinese satellite product"
                                 : plan.identity.reason,
                               details );
    }
    if ( familyFilter && *familyFilter && plan.identity.kindName != familyFilter )
    {
        Json::Value details;
        details["input"] = input;
        details["expectedFamily"] = familyFilter;
        details["actualFamily"] = plan.identity.kindName;
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "Input belongs to a different CN product family: " ) +
                                 plan.identity.kindName,
                               details );
    }

    // ── inspect (declared metadata + generation diagnostics) ───────────────
    try
    {
        plan.metadata = readCnProductMetadata( input, plan.identity );
    }
    catch ( const GeoError &error )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "CN product metadata unavailable: " ) + error.what() );
    }

    // ── resolve constituents ────────────────────────────────────────────────
    const std::string xml = cnLocateSidecarXml( input );
    const std::string tiff = cnLocateImageTiff( input, xml );
    if ( xml.empty() )
        throw RSOperatorError( ErrorCode::FileNotFound, "No L1A sidecar XML found for input" );
    if ( tiff.empty() )
        throw RSOperatorError(
            ErrorCode::FileNotFound,
            "No measurement TIFF could be paired with the sidecar (ambiguous multi-image "
            "product directories are refused, never resolved by directory order)" );
    plan.sidecarPath = QString::fromStdString( xml );
    plan.imagePath = QString::fromStdString( tiff );
    plan.rpcPath = QString::fromStdString( cnLocateRpcFile( input, tiff ) );
    resolvePmsSibling( plan.sidecarPath, &plan.siblingImagePath, &plan.siblingImageRole );

    // ── role map (registry-backed sensor truth) ────────────────────────────
    plan.sensorKey = cnSensorKey( plan.identity, plan.metadata );
    try
    {
        plan.profile = loadSensorProfile( plan.sensorKey );
    }
    catch ( const GeoError &error )
    {
        // Fail-closed: no sensor profile, no promised role semantics.
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "Sensor profile unavailable: " ) + error.what() );
    }
    for ( const std::string &unknownKey : plan.profile.unknownKeys )
        plan.warnings << QString::fromStdString( "registry: unknown key ignored: " + unknownKey );

    // ── band inventory ──────────────────────────────────────────────────────
    if ( plan.metadata.declaredBandIds.empty() )
    {
        for ( const SensorBandProfile &spec : plan.profile.bands )
            plan.bandNames << QString::fromStdString( spec.band );
        plan.bandOrderDeclared = false;
        plan.warnings << QStringLiteral(
            "sidecar declares no band inventory; TIFF band order is ASSUMED to match "
            "the sensor profile layout (unverified — reported via bandOrderUnverified, "
            "not hidden)" );
    }
    else
    {
        for ( const std::string &band : plan.metadata.declaredBandIds )
            plan.bandNames << QString::fromStdString( band );
        plan.bandOrderDeclared = true;
    }

    // ── validate (completeness verdict over resolved constituents) ─────────
    if ( plan.metadata.declaredBandIds.empty() )
        plan.missingConstituents << QStringLiteral( "declared band inventory (BandID/Bands)" );
    if ( plan.rpcPath.isEmpty() && plan.profile.constituents.end() !=
             std::find( plan.profile.constituents.begin(), plan.profile.constituents.end(),
                        "rpc_rpb" ) )
        plan.missingConstituents << QStringLiteral( "RPC document (.rpb)" );
    // Calibration absence is reported through missingDeclaredFields (below);
    // it degrades the import, not the verdict.
    plan.completeness = plan.missingConstituents.isEmpty()
                          ? ProductCompleteness::Complete
                          : ProductCompleteness::PartialReadable;
    return plan;
}

namespace {

/// Streams @p path read-only through SHA-256 up to @p budgetBytes. Returns
/// false when the file cannot be opened (note carries the reason); the
/// report says explicitly whether the digest covers the whole file.
bool hashConstituent( const QString &path, qint64 budgetBytes, ConstituentReport *report )
{
    QFileInfo info( path );
    report->exists = info.exists();
    report->bytes = info.size();
    if ( !report->exists )
    {
        report->note = QStringLiteral( "file does not exist" );
        return false;
    }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        report->note = file.errorString();
        return false;
    }
    sicnu::geo::Sha256 sha;
    qint64 hashed = 0;
    bool readFailed = false;
    constexpr qint64 kBlock = 65536;
    std::vector<char> block( static_cast<std::size_t>( kBlock ) );
    while ( hashed < budgetBytes )
    {
        const qint64 want = std::min( kBlock, budgetBytes - hashed );
        const qint64 got = file.read( block.data(), want );
        if ( got < 0 )
        {
            readFailed = true;
            report->note = file.errorString();
            break;
        }
        if ( got == 0 )
            break;
        sha.update( block.data(), static_cast<std::size_t>( got ) );
        hashed += got;
    }
    if ( readFailed )
        return false;
    report->readable = true;
    report->hashedBytes = hashed;
    report->hashComplete = hashed >= report->bytes;
    report->sha256Hex = sicnu::geo::toHex( sha.finalize() );
    report->digestScope = report->hashComplete
                            ? QStringLiteral( "file" )
                            : QStringLiteral( "first %1 bytes (declared budget cap)" ).arg( hashed );
    return true;
}

} // namespace

Json::Value ProductImportDryRun::toJson() const
{
    Json::Value dryRun( Json::objectValue );
    dryRun["plan"] = plan.toJson();
    dryRun["read_only"] = true;
    Json::Value checksum( Json::objectValue );
    checksum["algorithm"] = checksumAlgorithm.toStdString();
    checksum["budget_bytes"] = static_cast<Json::Int64>( hashBudgetBytes );
    dryRun["checksum"] = checksum;
    Json::Value nodes( Json::arrayValue );
    for ( const ConstituentReport &constituent : constituents )
    {
        Json::Value node( Json::objectValue );
        node["role"] = constituent.role.toStdString();
        node["path"] = constituent.path.toStdString();
        node["exists"] = constituent.exists;
        node["readable"] = constituent.readable;
        node["bytes"] = static_cast<Json::Int64>( constituent.bytes );
        if ( constituent.readable )
        {
            node["sha256"] = constituent.sha256Hex;
            node["hash_complete"] = constituent.hashComplete;
            node["hashed_bytes"] = static_cast<Json::Int64>( constituent.hashedBytes );
            node["digest_scope"] = constituent.digestScope.toStdString();
        }
        if ( !constituent.note.isEmpty() )
            node["note"] = constituent.note.toStdString();
        nodes.append( node );
    }
    dryRun["constituents"] = nodes;
    return dryRun;
}

ProductImportDryRun dryRunCnProductImport( const std::string &input, const char *familyFilter,
                                           qint64 hashBudgetBytes, RSOperatorContext *context )
{
    ProductImportDryRun dryRun;
    dryRun.hashBudgetBytes = hashBudgetBytes > 0 ? hashBudgetBytes : 0;
    dryRun.plan = planCnProductImport( input, familyFilter );

    auto run = [ & ] ( const QString &role, const QString &path ) {
        if ( context )
        {
            context->reportProgress( 0.0, ( "Inspecting " + role ).toStdString() );
            context->throwIfCancelled();
        }
        ConstituentReport report;
        report.role = role;
        report.path = path;
        if ( !path.isEmpty() )
            hashConstituent( path, dryRun.hashBudgetBytes, &report );
        else
            report.note = QStringLiteral( "not resolved by the plan" );
        dryRun.constituents.push_back( std::move( report ) );
    };

    run( QStringLiteral( "sidecar" ), dryRun.plan.sidecarPath );
    run( QStringLiteral( "image" ), dryRun.plan.imagePath );
    if ( !dryRun.plan.rpcPath.isEmpty() )
        run( QStringLiteral( "rpc" ), dryRun.plan.rpcPath );
    if ( !dryRun.plan.siblingImagePath.isEmpty() )
        run( QStringLiteral( "sibling_image" ), dryRun.plan.siblingImagePath );
    return dryRun;
}

CalibrationDecision evaluateCalibration( const ProductImportPlan &plan,
                                         bool applyCalibration,
                                         const QStringList &requestedBands )
{
    CalibrationDecision decision;
    decision.requested = applyCalibration;
    if ( !applyCalibration )
        return decision;

    for ( const QString &band : requestedBands )
    {
        const BandCalibration *calibration = nullptr;
        for ( const BandCalibration &candidate : plan.metadata.bandCalibration )
        {
            if ( QString::fromStdString( candidate.band ).compare( band, Qt::CaseInsensitive ) == 0 )
            {
                calibration = &candidate;
                break;
            }
        }
        if ( !calibration || !calibration->hasGain || !calibration->hasBias )
            decision.bandsMissingCoefficients << band;
    }
    // An empty request is never calibration-applicable (vacuous truth would
    // stamp radiance over digital numbers).
    decision.applicable = !requestedBands.isEmpty()
                          && decision.bandsMissingCoefficients.isEmpty();
    return decision;
}

Json::Value executeCnProductImport( const ProductImportPlan &plan,
                                    const std::string &outputPath,
                                    const QStringList &requestedBandsIn,
                                    bool applyCalibration,
                                    RSOperatorContext &context )
{
    // An empty request means "the declared inventory": materializing it here
    // keeps bandCount, the stacking selection and the calibration gate
    // consistent (stackToGeoTiff's own empty-name default would otherwise
    // stack every non-QA band while we reported zero bands).
    QStringList requestedBands = requestedBandsIn;
    if ( requestedBands.isEmpty() )
        requestedBands = plan.bandNames;

    // ── optional calibration gate ───────────────────────────────────────────
    const CalibrationDecision calibration =
        evaluateCalibration( plan, applyCalibration, requestedBands );
    if ( applyCalibration && !calibration.applicable )
    {
        Json::Value details;
        details["reason"] = "calibration coefficients are not declared for every requested band";
        Json::Value missing( Json::arrayValue );
        for ( const QString &band : calibration.bandsMissingCoefficients )
            missing.append( band.toStdString() );
        details["bandsMissingCoefficients"] = missing;
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "Declared-coefficient calibration requested but the sidecar does not declare "
            "gain and bias for every requested band; consult missingDeclaredFields or the "
            "published per-date coefficient tables",
            details );
    }

    // ── stack (unchanged fail-closed contract) ─────────────────────────────
    // The inventory is built from the plan's FULL band list (declared sidecar
    // order = TIFF band order), so sourceBand indexes the SOURCE TIFF and a
    // requested subset/reordering only re-selects by name (#676: unknown
    // names fail closed instead of re-indexing pixels).
    SatelliteProducts::ProductInfo info;
    info.productId = QString::fromStdString( plan.metadata.productId );
    info.spacecraft = QString::fromStdString( plan.metadata.platform );
    info.processingLevel = QString::fromStdString( plan.metadata.processingLevel );
    info.acquisitionDate = QString::fromStdString( plan.metadata.acquisitionTime );
    info.attributes[QStringLiteral( "SICNU_SENSOR" )] =
        QString::fromStdString( plan.metadata.sensor );
    info.attributes[QStringLiteral( "SICNU_BAND_SOURCE" )] =
        plan.bandOrderDeclared ? QStringLiteral( "declared_band_ids" )
                               : QStringLiteral( "band_role_table" );
    if ( !plan.bandOrderDeclared )
        info.attributes[QStringLiteral( "SICNU_BAND_ORDER_UNVERIFIED" )] =
            QStringLiteral( "true" );
    for ( int i = 0; i < plan.bandNames.size(); ++i )
    {
        SatelliteProducts::BandFile band;
        band.path = plan.imagePath;
        band.name = plan.bandNames[i];
        band.sourceBand = i + 1;
        if ( const SensorBandProfile *spec =
                 plan.profile.findBand( plan.bandNames[i].toStdString() ) )
        {
            if ( spec->hasCenterWavelengthNm )
                band.wavelengthNm = static_cast<int>( spec->centerWavelengthNm + 0.5 );
            else if ( spec->hasWavelengthNm )
                band.wavelengthNm = static_cast<int>( spec->wavelengthNm + 0.5 );
            band.role = sicnu::data::bandRoleFromString( QString::fromStdString( spec->role ) );
        }
        info.bands.append( band );
    }

    const QStringList missing = SatelliteProducts::unresolvableBands( info, requestedBands );
    if ( !missing.isEmpty() )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               ( "Requested bands not found in product (missingBands: "
                                 + missing.join( QStringLiteral( ", " ) ) + ")" )
                                   .toStdString() );
    }

    context.reportProgress( 0.15, "Stacking bands to GeoTIFF" );
    QString err;
    bool ok = false;
    try
    {
        ok = SatelliteProducts::stackToGeoTiff(
            info, requestedBands, QString::fromStdString( outputPath ), &err,
            [&]( double fraction, const QString &message ) {
                context.reportProgress( 0.15 + 0.7 * fraction, message.toStdString() );
                context.throwIfCancelled();
            } );
    }
    catch ( ... )
    {
        // stackToGeoTiff removes its partial output on IO-failure RETURNS,
        // but an exception from the progress bridge (cancellation) unwinds
        // past every cleanup path — a half-stacked GeoTIFF must never
        // survive a cancel (ADR 0159 zero-half-product contract).
        QFile::remove( QString::fromStdString( outputPath ) );
        throw;
    }
    if ( !ok )
    {
        throw RSOperatorError( ErrorCode::ComputationError,
                               err.isEmpty() ? "Failed to stack CN product bands"
                                             : err.toStdString() );
    }

    // ── stamp declared metadata ────────────────────────────────────────────
    context.reportProgress( 0.90, "Writing CN product metadata" );
    if ( !writeCnImportMetadata( QString::fromStdString( outputPath ), plan.metadata,
                                 requestedBands,
                                 QString::fromStdString( plan.identity.kindName ), &err,
                                 /*bandOrderUnverified=*/!plan.bandOrderDeclared ) )
    {
        // An unstamped stack is not a finished import: remove it instead of
        // handing callers a file that claims nothing about what it is.
        QFile::remove( QString::fromStdString( outputPath ) );
        throw RSOperatorError( ErrorCode::ComputationError,
                               err.isEmpty() ? "Failed to write CN import metadata"
                                             : err.toStdString() );
    }

    // ── optional calibration application ───────────────────────────────────
    bool calibrationApplied = false;
    if ( calibration.applicable )
    {
        context.reportProgress( 0.93, "Applying declared calibration (DN -> radiance)" );
        GDALDatasetH dataset =
            GDALOpen( QString::fromStdString( outputPath ).toUtf8().constData(), GA_Update );
        if ( dataset == nullptr )
        {
            QFile::remove( QString::fromStdString( outputPath ) );
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Cannot reopen stacked GeoTIFF to apply declared calibration" );
        }
        const int bandCount = GDALGetRasterCount( dataset );
        constexpr int kChunk = 4096;
        std::vector<double> chunk( static_cast<std::size_t>( kChunk ), 0.0 );
        bool transformFailed = false;
        for ( int index = 1; index <= bandCount && index <= requestedBands.size() && !transformFailed;
              ++index )
        {
            const BandCalibration *calibrationForBand = nullptr;
            for ( const BandCalibration &candidate : plan.metadata.bandCalibration )
            {
                if ( QString::fromStdString( candidate.band )
                         .compare( requestedBands[index - 1], Qt::CaseInsensitive ) == 0 )
                {
                    calibrationForBand = &candidate;
                    break;
                }
            }
            if ( !calibrationForBand )
                continue;
            GDALRasterBandH rasterBand = GDALGetRasterBand( dataset, index );
            const int width = GDALGetRasterBandXSize( rasterBand );
            const int height = GDALGetRasterBandYSize( rasterBand );
            for ( int line = 0; line < height && !transformFailed; ++line )
            {
                context.throwIfCancelled();
                for ( int column0 = 0; column0 < width; column0 += kChunk )
                {
                    const int columns = std::min( kChunk, width - column0 );
                    if ( GDALRasterIO( rasterBand, GF_Read, column0, line, columns, 1,
                                       chunk.data(), columns, 1, GDT_Float64, 0, 0 ) != CE_None )
                    {
                        transformFailed = true;
                        break;
                    }
                    for ( int column = 0; column < columns; ++column )
                        chunk[static_cast<std::size_t>( column )] =
                            chunk[static_cast<std::size_t>( column )] * calibrationForBand->gain +
                            calibrationForBand->bias;
                    if ( GDALRasterIO( rasterBand, GF_Write, column0, line, columns, 1,
                                       chunk.data(), columns, 1, GDT_Float64, 0, 0 ) != CE_None )
                    {
                        transformFailed = true;
                        break;
                    }
                }
            }
        }
        GDALClose( dataset );
        if ( transformFailed )
        {
            // Never leave a half-scaled stack behind: the file now mixes DN
            // and radiance rows/bands with a digital_number stamp — unusable.
            QFile::remove( QString::fromStdString( outputPath ) );
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "Raster IO failed while applying declared calibration; "
                                   "partial output removed" );
        }
        calibrationApplied = true;
        if ( !SatelliteProducts::setRadiometricState(
                 QString::fromStdString( outputPath ),
                 SatelliteProducts::kRadiometricStateRadiance, &err ) )
        {
            // The pixels are already radiance but the stamp would stay
            // digital_number — a mislabeled file is worse than no file.
            QFile::remove( QString::fromStdString( outputPath ) );
            throw RSOperatorError( ErrorCode::ComputationError,
                                   err.isEmpty()
                                     ? "Failed to stamp radiance state after calibration"
                                     : err.toStdString() );
        }
    }

    // ── provenance ─────────────────────────────────────────────────────────
    context.reportProgress( 1.0, "CN product import complete" );
    Json::Value result( Json::objectValue );
    // Legacy keys (ADR 0157 operator results — kept stable for courses).
    result["output"] = outputPath;
    result["productId"] = plan.metadata.productId;
    result["productKind"] = plan.identity.kindName;
    result["satellite"] = plan.metadata.platform.empty() ? plan.identity.satellite
                                                         : plan.metadata.platform;
    result["sensor"] = plan.metadata.sensor;
    result["sensorMode"] = plan.metadata.sensorMode.empty() ? plan.identity.sensorMode
                                                            : plan.metadata.sensorMode;
    result["sensorKey"] = plan.sensorKey;
    result["processingLevel"] = plan.metadata.processingLevel;
    result["acquisitionTime"] = plan.metadata.acquisitionTime;
    result["radiometricState"] = calibrationApplied
                                   ? std::string( SatelliteProducts::kRadiometricStateRadiance )
                                   : plan.metadata.radiometricState;
    result["bandSource"] = plan.bandOrderDeclared ? "declared_band_ids" : "band_role_table";
    result["bandOrderUnverified"] = !plan.bandOrderDeclared;
    result["bandCount"] = static_cast<Json::Int64>( requestedBands.size() );
    Json::Value bands( Json::arrayValue );
    Json::Value roles( Json::arrayValue );
    Json::Value wavelengths( Json::arrayValue );
    for ( const QString &band : requestedBands )
    {
        bands.append( band.toStdString() );
        std::string role;
        if ( const SensorBandProfile *spec = plan.profile.findBand( band.toStdString() ) )
        {
            role = spec->role;
            if ( spec->hasCenterWavelengthNm )
                wavelengths.append( spec->centerWavelengthNm );
            else if ( spec->hasWavelengthNm )
                wavelengths.append( spec->wavelengthNm );
            else
                wavelengths.append( Json::nullValue );
        }
        else
        {
            wavelengths.append( Json::nullValue );
        }
        roles.append( role );
    }
    result["bands"] = bands;
    result["bandRoles"] = roles;
    result["bandWavelengthsNm"] = wavelengths;
    Json::Value declared( Json::objectValue );
    declared["sunElevationDeg"] = plan.metadata.hasSunElevation;
    declared["sunAzimuthDeg"] = plan.metadata.hasSunAzimuth;
    if ( !plan.metadata.sunElevationSource.empty() )
        result["sunElevationSource"] = plan.metadata.sunElevationSource;
    declared["calibration"] = !plan.metadata.bandCalibration.empty();
    declared["cloudCover"] = plan.metadata.hasCloudCover;
    declared["orbitId"] = !plan.metadata.orbitId.empty();
    declared["bandInventory"] = plan.bandOrderDeclared;
    declared["rpc"] = !plan.rpcPath.isEmpty();
    result["declared"] = declared;
    result["missingDeclaredFields"] = cnMissingDeclaredFields( plan.metadata );

    // Platform 10.0 provenance (ADR 0147).
    result["sensorProfile"] = plan.sensorKey;
    result["sensorProfileVersion"] = plan.profile.schemaVersion;
    result["modality"] = plan.profile.modality;
    if ( plan.profile.hasGsdM )
        result["nominalGsdM"] = plan.profile.gsdM;
    result["sidecarGeneration"] = plan.metadata.parseDiagnostics.get( "generation", "" ).asString();
    result["sidecarRoot"] = plan.metadata.parseDiagnostics.get( "root_element", "" ).asString();
    result["sidecar"] = plan.sidecarPath.toStdString();
    result["image"] = plan.imagePath.toStdString();
    if ( !plan.rpcPath.isEmpty() )
        result["rpc"] = plan.rpcPath.toStdString();
    if ( !plan.siblingImagePath.isEmpty() )
    {
        result["siblingImage"] = plan.siblingImagePath.toStdString();
        result["siblingRole"] = plan.siblingImageRole.toStdString();
    }
    result["completeness"] = productCompletenessName( plan.completeness );
    Json::Value missingConstituents( Json::arrayValue );
    for ( const QString &m : plan.missingConstituents )
        missingConstituents.append( m.toStdString() );
    result["missingConstituents"] = missingConstituents;
    Json::Value warnings( Json::arrayValue );
    for ( const QString &w : plan.warnings )
        warnings.append( w.toStdString() );
    result["warnings"] = warnings;
    Json::Value calibrationRecord( Json::objectValue );
    calibrationRecord["requested"] = calibration.requested;
    calibrationRecord["applied"] = calibrationApplied;
    calibrationRecord["formula"] = calibration.formula;
    if ( !calibration.bandsMissingCoefficients.isEmpty() )
    {
        Json::Value missingCoefficients( Json::arrayValue );
        for ( const QString &band : calibration.bandsMissingCoefficients )
            missingCoefficients.append( band.toStdString() );
        calibrationRecord["bandsMissingCoefficients"] = missingCoefficients;
    }
    result["calibration"] = calibrationRecord;
    return result;
}

} // namespace sicnu::operators::rs
