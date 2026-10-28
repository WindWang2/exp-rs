// src/data/import_preflight.cpp — import preflight explanation report
// (R25-B feature 1). See import_preflight.h for the contract.
//
// Wiring discipline: every fact in the report comes from a production piece
// the import path itself uses — ProductAdapterRegistry (recognition),
// inspectRaster/inspectVector (canonical metadata, one read-only GDAL open),
// localIdentityToken (proven identity) and DataManager::findByPath (dedup
// prediction). Nothing here invents a second catalog, opens a network
// socket, or mutates the catalog.
#include "import_preflight.h"

#include "data_asset.h"
#include "data_manager.h"

#include "geospatial/identity/asset_identity.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/products/product_registry.h"

#include <QFileInfo>
#include <QJsonArray>

namespace sicnu::data
{

namespace
{

bool isRemoteSpelling( const QString &path )
{
    const QString lowered = path.toLower();
    if ( lowered.startsWith( QLatin1String( "http://" ) ) ||
         lowered.startsWith( QLatin1String( "https://" ) ) ||
         lowered.startsWith( QLatin1String( "ftp://" ) ) ||
         lowered.startsWith( QLatin1String( "s3://" ) ) ||
         lowered.startsWith( QLatin1String( "gs://" ) ) )
        return true;
    // Every GDAL virtual system manager prefix (/vsicurl/, /vsis3/,
    // /vsizip/, /vsitar/, /vsigzip/, ...) denotes non-local access.
    return lowered.startsWith( QLatin1String( "/vsi" ) );
}

QString severityError() { return QStringLiteral( "error" ); }
QString severityWarning() { return QStringLiteral( "warning" ); }
QString severityInfo() { return QStringLiteral( "info" ); }

void addFinding( ImportPreflightReport &report, const QString &code, const QString &severity,
                 const QString &message, const QString &remediation = QString() )
{
    ImportPreflightFinding finding;
    finding.code = code;
    finding.severity = severity;
    finding.message = message;
    finding.remediation = remediation;
    report.findings.append( finding );
}

void fillFromRasterMetadata( ImportPreflightReport &report, const geo::RasterMetadata &metadata )
{
    report.driver = QString::fromStdString( metadata.driver );
    report.width = metadata.width;
    report.height = metadata.height;
    report.bandCount = metadata.bandCount;
    report.crsValid = metadata.crs.valid;
    report.crsAuthid = QString::fromStdString( metadata.crs.authid );
    report.crsWkt = QString::fromStdString( metadata.crs.wkt );
    report.hasGeotransform = metadata.hasGeotransform;
    report.hasExtent = metadata.hasExtent;
    report.minX = metadata.minX;
    report.minY = metadata.minY;
    report.maxX = metadata.maxX;
    report.maxY = metadata.maxY;
    report.sensor = QString::fromStdString( metadata.sensor );
    report.platform = QString::fromStdString( metadata.platform );
    report.processingLevel = QString::fromStdString( metadata.processingLevel );
    report.radiometricState = QString::fromStdString( metadata.radiometricState );
    report.acquisitionTime = QString::fromStdString( metadata.acquisitionTime );
    report.hasCloudCover = metadata.hasCloudCover;
    report.cloudCover = metadata.cloudCover;
    report.hasGsd = metadata.hasGsd;
    report.gsd = metadata.gsd;
    report.subdatasetCount = static_cast<int>( metadata.subdatasets.size() );
    report.bands.reserve( static_cast<int>( metadata.bands.size() ) );
    for ( const geo::BandInfo &band : metadata.bands )
    {
        ImportPreflightBandFact fact;
        fact.index = band.index;
        fact.dtype = QString::fromStdString( band.dtype );
        fact.role = QString::fromStdString( band.role );
        fact.unit = QString::fromStdString( band.unit );
        fact.hasNoData = band.hasNoData;
        fact.noDataValue = band.noDataValue;
        fact.hasWavelength = band.hasWavelength;
        fact.wavelengthNm = band.wavelengthNm;
        report.bands.append( fact );
    }
}

void appendRasterQualityFindings( ImportPreflightReport &report )
{
    if ( report.resourceKind == ImportPreflightResourceKind::Raster ||
         report.resourceKind == ImportPreflightResourceKind::Product )
    {
        if ( !report.crsValid )
            addFinding( report, QStringLiteral( "import.crs_missing" ), severityWarning(),
                        QStringLiteral( "the raster declares no CRS" ),
                        QStringLiteral( "assign a CRS before georeferenced analysis; "
                                         "processing operators will refuse or treat it as raw" ) );
        if ( !report.hasGeotransform )
            addFinding( report, QStringLiteral( "import.geotransform_missing" ), severityWarning(),
                        QStringLiteral( "the raster declares no geotransform (raw pixel grid)" ),
                        QStringLiteral( "georeference the raster or accept pixel-coordinate space" ) );
        bool anyNoData = false;
        bool anyRole = false;
        for ( const ImportPreflightBandFact &band : report.bands )
        {
            anyNoData = anyNoData || band.hasNoData;
            anyRole = anyRole || !band.role.isEmpty();
        }
        if ( report.bandCount > 0 && !anyNoData )
            addFinding( report, QStringLiteral( "import.nodata_undeclared" ), severityInfo(),
                        QStringLiteral( "no band declares a NoData value" ),
                        QStringLiteral( "masked pixels will be indistinguishable from valid data" ) );
        if ( report.bandCount > 0 && !anyRole )
            addFinding( report, QStringLiteral( "import.band_roles_unknown" ), severityInfo(),
                        QStringLiteral( "no band carries a semantic role (NIR/Red/...)" ),
                        QStringLiteral( "role-aware indices (NDVI and friends) will not resolve" ) );
    }
}

bool hasErrorFinding( const ImportPreflightReport &report )
{
    for ( const ImportPreflightFinding &finding : report.findings )
    {
        if ( finding.severity == severityError() )
            return true;
    }
    return false;
}

bool needsUserChoice( const ImportPreflightReport &report )
{
    for ( const ImportPreflightFinding &finding : report.findings )
    {
        if ( finding.code == QLatin1String( "import.container_subdatasets" ) ||
             finding.code == QLatin1String( "import.adapter_ambiguous" ) )
            return true;
    }
    return false;
}

} // namespace

QJsonObject ImportPreflightFinding::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "code" ), code );
    json.insert( QStringLiteral( "severity" ), severity );
    json.insert( QStringLiteral( "message" ), message );
    if ( !remediation.isEmpty() )
        json.insert( QStringLiteral( "remediation" ), remediation );
    return json;
}

QJsonObject ImportPreflightBandFact::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "index" ), index );
    json.insert( QStringLiteral( "dtype" ), dtype );
    if ( !role.isEmpty() )
        json.insert( QStringLiteral( "role" ), role );
    if ( !unit.isEmpty() )
        json.insert( QStringLiteral( "unit" ), unit );
    if ( hasNoData )
        json.insert( QStringLiteral( "nodata" ), noDataValue );
    if ( hasWavelength )
        json.insert( QStringLiteral( "wavelength_nm" ), wavelengthNm );
    return json;
}

QString ImportPreflightReport::verdictString() const
{
    switch ( verdict )
    {
        case ImportPreflightVerdict::Importable:
            return QStringLiteral( "importable" );
        case ImportPreflightVerdict::NeedsUserChoice:
            return QStringLiteral( "needs_user_choice" );
        case ImportPreflightVerdict::Refused:
            return QStringLiteral( "refused" );
        case ImportPreflightVerdict::OutOfScope:
            return QStringLiteral( "out_of_scope" );
    }
    return QStringLiteral( "refused" );
}

QString ImportPreflightReport::resourceKindString() const
{
    switch ( resourceKind )
    {
        case ImportPreflightResourceKind::Raster:
            return QStringLiteral( "raster" );
        case ImportPreflightResourceKind::Vector:
            return QStringLiteral( "vector" );
        case ImportPreflightResourceKind::Product:
            return QStringLiteral( "product" );
        case ImportPreflightResourceKind::Unknown:
            return QStringLiteral( "unknown" );
    }
    return QStringLiteral( "unknown" );
}

const ImportPreflightFinding *ImportPreflightReport::finding( const QString &code ) const
{
    for ( const ImportPreflightFinding &entry : findings )
    {
        if ( entry.code == code )
            return &entry;
    }
    return nullptr;
}

QJsonObject ImportPreflightReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.import.preflight/1" ) );
    json.insert( QStringLiteral( "schema_version" ), kSchemaVersion );
    json.insert( QStringLiteral( "path" ), path );
    if ( !canonicalPath.isEmpty() )
        json.insert( QStringLiteral( "canonical_path" ), canonicalPath );
    json.insert( QStringLiteral( "verdict" ), verdictString() );
    json.insert( QStringLiteral( "resource_kind" ), resourceKindString() );

    QJsonObject identity;
    identity.insert( QStringLiteral( "token" ), identityToken );
    identity.insert( QStringLiteral( "strength" ), identityStrength );
    identity.insert( QStringLiteral( "size_bytes" ), static_cast<double>( identitySizeBytes ) );
    identity.insert( QStringLiteral( "hashed_bytes" ), static_cast<double>( identityHashedBytes ) );
    json.insert( QStringLiteral( "identity" ), identity );

    QJsonObject dedup;
    dedup.insert( QStringLiteral( "already_registered" ), alreadyRegistered );
    if ( alreadyRegistered )
    {
        dedup.insert( QStringLiteral( "existing_asset_id" ), existingAssetId );
        dedup.insert( QStringLiteral( "existing_asset_revision" ),
                      static_cast<double>( existingAssetRevision ) );
    }
    json.insert( QStringLiteral( "dedup" ), dedup );

    QJsonObject recognition;
    recognition.insert( QStringLiteral( "product_kind" ), productKind );
    recognition.insert( QStringLiteral( "adapter_id" ), adapterId );
    if ( !productId.isEmpty() )
        recognition.insert( QStringLiteral( "product_id" ), productId );
    if ( !candidateAdapterIds.isEmpty() )
        recognition.insert( QStringLiteral( "candidate_adapter_ids" ),
                            QJsonArray::fromStringList( candidateAdapterIds ) );
    if ( !completeness.isEmpty() )
    {
        recognition.insert( QStringLiteral( "completeness" ), completeness );
        if ( !missingConstituents.isEmpty() )
            recognition.insert( QStringLiteral( "missing_constituents" ),
                                QJsonArray::fromStringList( missingConstituents ) );
    }
    json.insert( QStringLiteral( "recognition" ), recognition );

    if ( resourceKind == ImportPreflightResourceKind::Raster ||
         resourceKind == ImportPreflightResourceKind::Product )
    {
        QJsonObject raster;
        raster.insert( QStringLiteral( "driver" ), driver );
        raster.insert( QStringLiteral( "width" ), width );
        raster.insert( QStringLiteral( "height" ), height );
        raster.insert( QStringLiteral( "band_count" ), bandCount );
        raster.insert( QStringLiteral( "crs_valid" ), crsValid );
        if ( !crsAuthid.isEmpty() )
            raster.insert( QStringLiteral( "crs_authid" ), crsAuthid );
        raster.insert( QStringLiteral( "has_geotransform" ), hasGeotransform );
        if ( hasExtent )
        {
            QJsonObject extent;
            extent.insert( QStringLiteral( "min_x" ), minX );
            extent.insert( QStringLiteral( "min_y" ), minY );
            extent.insert( QStringLiteral( "max_x" ), maxX );
            extent.insert( QStringLiteral( "max_y" ), maxY );
            raster.insert( QStringLiteral( "extent" ), extent );
        }
        QJsonObject semantics;
        auto putWhenSet = [&semantics]( const char *key, const QString &value )
        {
            if ( !value.isEmpty() )
                semantics.insert( QLatin1String( key ), value );
        };
        putWhenSet( "sensor", sensor );
        putWhenSet( "platform", platform );
        putWhenSet( "processing_level", processingLevel );
        putWhenSet( "radiometric_state", radiometricState );
        putWhenSet( "acquisition_time", acquisitionTime );
        if ( hasCloudCover )
            semantics.insert( QStringLiteral( "cloud_cover_percent" ), cloudCover );
        if ( hasGsd )
            semantics.insert( QStringLiteral( "gsd_meters" ), gsd );
        if ( !semantics.isEmpty() )
            raster.insert( QStringLiteral( "semantics" ), semantics );
        QJsonArray bandArray;
        for ( const ImportPreflightBandFact &band : bands )
            bandArray.append( band.toJson() );
        raster.insert( QStringLiteral( "bands" ), bandArray );
        if ( subdatasetCount > 0 )
            raster.insert( QStringLiteral( "subdataset_count" ), subdatasetCount );
        json.insert( QStringLiteral( "raster" ), raster );
    }

    if ( resourceKind == ImportPreflightResourceKind::Vector )
    {
        QJsonObject vector;
        vector.insert( QStringLiteral( "driver" ), driver );
        vector.insert( QStringLiteral( "layer_count" ), layerCount );
        if ( !layerNames.isEmpty() )
            vector.insert( QStringLiteral( "layer_names" ), QJsonArray::fromStringList( layerNames ) );
        json.insert( QStringLiteral( "vector" ), vector );
    }

    QJsonArray findingArray;
    for ( const ImportPreflightFinding &entry : findings )
        findingArray.append( entry.toJson() );
    json.insert( QStringLiteral( "findings" ), findingArray );
    return json;
}

ImportPreflightReport ImportPreflight::inspect( const QString &path, const DataManager *manager,
                                                const Options &options )
{
    ImportPreflightReport report;
    report.path = path;

    if ( path.trimmed().isEmpty() )
    {
        addFinding( report, QStringLiteral( "import.empty_path" ), severityError(),
                    QStringLiteral( "no source path was given" ) );
        report.verdict = ImportPreflightVerdict::Refused;
        return report;
    }

    if ( isRemoteSpelling( path ) )
    {
        // Doctrine: the preflight never emits packets. Remote sources keep
        // their dedicated registration paths (deferred network open).
        addFinding( report, QStringLiteral( "import.remote_out_of_scope" ), severityInfo(),
                    QStringLiteral( "remote spelling: local preflight not attempted (no network)" ),
                    QStringLiteral( "register the remote source directly; its open is deferred" ) );
        report.verdict = ImportPreflightVerdict::OutOfScope;
        return report;
    }

    const QFileInfo info( path );
    if ( !info.exists() )
    {
        addFinding( report, QStringLiteral( "import.path_missing" ), severityError(),
                    QStringLiteral( "the source path does not exist" ),
                    QStringLiteral( "check the path spelling or relocate the file before import" ) );
        report.verdict = ImportPreflightVerdict::Refused;
        return report;
    }
    report.canonicalPath = info.canonicalFilePath();

    // --- proven identity (fail-closed: unprovable is reported, not guessed) --
    if ( options.probeIdentity )
    {
        geo::LocalIdentityOptions identityOptions;
        identityOptions.hashBytes = options.identityHashBytes;
        const geo::LocalIdentityToken token =
            geo::localIdentityToken( path.toStdString(), identityOptions );
        report.identityToken = QString::fromStdString( token.token );
        report.identityStrength = QString::fromStdString( token.strength );
        report.identitySizeBytes = static_cast<quint64>( token.sizeBytes );
        report.identityHashedBytes = static_cast<quint64>( token.hashedBytes );
        if ( !token.provable() )
            addFinding( report, QStringLiteral( "import.identity_unprovable" ), severityWarning(),
                        QStringLiteral( "no provable local identity could be computed" ),
                        QStringLiteral( "re-import detection for this source stays path-based" ) );
    }

    // --- dedup prediction (read-only; no registration happens here) ---------
    if ( manager != nullptr && options.probeDedup )
    {
        const std::optional<AssetSnapshot> existing = manager->findByPath( path );
        if ( existing )
        {
            report.alreadyRegistered = true;
            report.existingAssetId = existing->id().toString();
            report.existingAssetRevision = existing->revision().value();
            addFinding( report, QStringLiteral( "import.already_registered" ), severityWarning(),
                        QStringLiteral( "this source is already registered (asset %1, revision %2)" )
                            .arg( report.existingAssetId )
                            .arg( report.existingAssetRevision ),
                        QStringLiteral( "registerSource will reuse the existing asset and only "
                                        "re-resolve it when the structure changed" ) );
        }
    }

    // --- recognition through the production adapter registry ----------------
    const std::string stdPath = path.toStdString();
    geo::ProductAdapterRegistry &registry = geo::ProductAdapterRegistry::instance();
    std::vector<geo::ProductAdapter *> candidates;
    try
    {
        candidates = registry.adaptersAccepting( stdPath );
    }
    catch ( const std::exception & )
    {
        // accepts() is documented cheap/read-only; a throw here is treated as
        // "no candidates" and the generic inspection below still runs.
    }
    for ( const geo::ProductAdapter *candidate : candidates )
        report.candidateAdapterIds.append( QString::fromStdString( candidate->id() ) );

    if ( candidates.size() > 1 )
    {
        addFinding( report, QStringLiteral( "import.adapter_ambiguous" ), severityInfo(),
                    QStringLiteral( "%1 adapter families claim this path; the first (%2) wins by "
                                    "registration order" )
                        .arg( candidates.size() )
                        .arg( report.candidateAdapterIds.constFirst() ),
                    QStringLiteral( "confirm the intended product family before importing" ) );
    }

    bool recognizedAsProduct = false;
    geo::ProductMetadata productMetadata;
    if ( !candidates.empty() )
    {
        try
        {
            const geo::ProductAssets assets = candidates.front()->enumerate( stdPath );
            report.productKind = QString::fromUtf8( geo::productKindName( assets.kind ) );
            report.adapterId = QString::fromStdString( assets.adapterId );
            report.productId = QString::fromStdString( assets.productId );
            report.completeness = QLatin1String( geo::productCompletenessName( assets.completeness ) );
            for ( const std::string &missing : assets.missingConstituents )
                report.missingConstituents.append( QString::fromStdString( missing ) );
            if ( assets.kind != geo::ProductKind::GenericRaster )
                recognizedAsProduct = true;
            productMetadata = assets.metadata;
            if ( assets.completeness == geo::ProductCompleteness::PartialReadable )
                addFinding( report, QStringLiteral( "import.product_incomplete" ), severityWarning(),
                            QStringLiteral( "product is partially readable: %1 core constituent(s) missing" )
                                .arg( report.missingConstituents.size() ),
                            QStringLiteral( "restore the missing constituents or import with the "
                                            "documented gaps" ) );
            else if ( assets.completeness == geo::ProductCompleteness::Invalid )
                addFinding( report, QStringLiteral( "import.product_invalid" ), severityError(),
                            QStringLiteral( "product metadata is missing or unparseable for this family" ) );
            else if ( assets.completeness == geo::ProductCompleteness::UnsupportedVersion )
                addFinding( report, QStringLiteral( "import.product_unsupported_version" ), severityError(),
                            QStringLiteral( "product family recognized but its version is not understood" ) );
        }
        catch ( const geo::GeoError &error )
        {
            if ( error.code() == geo::ErrorCode::UnsupportedProduct )
            {
                addFinding( report, QStringLiteral( "import.product_unsupported" ), severityError(),
                            QString::fromUtf8( error.what() ) );
            }
            // Any other enumerate() failure falls through: the generic
            // raster/vector inspection below is the honest fallback (the
            // file may still be a perfectly importable plain dataset).
        }
    }

    // --- canonical metadata (one read-only GDAL open per kind) --------------
    bool opened = false;
    try
    {
        const geo::RasterMetadata metadata = geo::inspectRaster( stdPath );
        opened = true;
        report.resourceKind = recognizedAsProduct ? ImportPreflightResourceKind::Product
                                                  : ImportPreflightResourceKind::Raster;
        fillFromRasterMetadata( report, metadata );
        // Prefer the adapter's normalized metadata when both sides know it
        // (product adapters parse sidecars the generic open cannot see).
        if ( recognizedAsProduct )
        {
            if ( !productMetadata.productId.empty() )
                report.productId = QString::fromStdString( productMetadata.productId );
            if ( !productMetadata.sensor.empty() )
                report.sensor = QString::fromStdString( productMetadata.sensor );
            if ( !productMetadata.platform.empty() )
                report.platform = QString::fromStdString( productMetadata.platform );
            if ( !productMetadata.processingLevel.empty() )
                report.processingLevel = QString::fromStdString( productMetadata.processingLevel );
            if ( !productMetadata.acquisitionTime.empty() )
                report.acquisitionTime = QString::fromStdString( productMetadata.acquisitionTime );
            if ( !productMetadata.radiometricState.empty() )
                report.radiometricState = QString::fromStdString( productMetadata.radiometricState );
            if ( productMetadata.hasCloudCover )
            {
                report.hasCloudCover = true;
                report.cloudCover = productMetadata.cloudCover;
            }
            if ( productMetadata.hasResolution )
            {
                report.hasGsd = true;
                report.gsd = productMetadata.resolutionMeters;
            }
        }
        if ( report.subdatasetCount > 0 )
            addFinding( report, QStringLiteral( "import.container_subdatasets" ), severityWarning(),
                        QStringLiteral( "dataset is a container with %1 subdataset(s)" )
                            .arg( report.subdatasetCount ),
                        QStringLiteral( "pick the subdataset to import; registering the container "
                                        "keeps it at file level" ) );
    }
    catch ( const geo::GeoError & )
    {
        try
        {
            const geo::VectorMetadata metadata = geo::inspectVector( stdPath );
            opened = true;
            report.resourceKind = ImportPreflightResourceKind::Vector;
            report.driver = QString::fromStdString( metadata.driver );
            report.layerCount = static_cast<int>( metadata.layers.size() );
            for ( const geo::VectorLayerInfo &layer : metadata.layers )
                report.layerNames.append( QString::fromStdString( layer.name ) );
        }
        catch ( const geo::GeoError &vectorError )
        {
            addFinding( report, QStringLiteral( "import.unreadable" ), severityError(),
                        QStringLiteral( "source is neither an openable raster nor vector (%1)" )
                            .arg( QString::fromUtf8( geo::GeoError::errorCodeName( vectorError.code() ) ) ),
                        QStringLiteral( "verify the file format is supported and not corrupted" ) );
        }
    }
    if ( !opened && report.resourceKind == ImportPreflightResourceKind::Unknown )
    {
        // A directory product whose constituents could not be opened directly
        // still deserves an honest kind label from the adapter verdict.
        if ( recognizedAsProduct )
            report.resourceKind = ImportPreflightResourceKind::Product;
    }

    appendRasterQualityFindings( report );

    if ( hasErrorFinding( report ) )
        report.verdict = ImportPreflightVerdict::Refused;
    else if ( needsUserChoice( report ) )
        report.verdict = ImportPreflightVerdict::NeedsUserChoice;
    else if ( opened || recognizedAsProduct )
        report.verdict = ImportPreflightVerdict::Importable;
    else
        report.verdict = ImportPreflightVerdict::Refused;
    return report;
}

} // namespace sicnu::data
