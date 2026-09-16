/***************************************************************************
 * src/cli/lab_self_check.cpp — `lab --self-check` implementation (see header)
 ***************************************************************************/
#include "lab_self_check.h"

#include "agent/lab_data_pack.h"
#include "agent/output_verifier.h"
#include "data/offline_mode.h"

#include <QDir>
#include <QFileInfo>

#include <gdal.h>
#include <ogr_srs_api.h>
#include <cpl_conv.h>

#include <algorithm>

#ifdef SICNU_SOURCE_DIR
#define LAB_SELF_CHECK_DEFAULT_ROOT QString::fromUtf8( SICNU_SOURCE_DIR )
#else
#define LAB_SELF_CHECK_DEFAULT_ROOT QString()
#endif

namespace sicnu::cli {

namespace {

constexpr const char *kSchema = "sicnu.lab.self-check/1";

struct Check
{
    QString name;
    QString status; // "ok" | "degraded" | "failed"
    QString detail;
    Json::Value extra; // optional structured evidence
};

Json::Value checkJson( const Check &check )
{
    Json::Value json( Json::objectValue );
    json["check"] = check.name.toStdString();
    json["status"] = check.status.toStdString();
    if ( !check.detail.isEmpty() )
        json["detail"] = check.detail.toStdString();
    if ( !check.extra.isNull() )
        json["evidence"] = check.extra;
    return json;
}

/// PROJ/GDAL sanity: the projection authority must resolve EPSG:4326 without
/// touching the network. The most common broken classroom install is a
/// missing GDAL/PROJ data directory; this check names it.
Check checkProjAuthority()
{
    Check check;
    check.name = QStringLiteral( "proj_authority" );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    if ( !srs )
    {
        check.status = QStringLiteral( "failed" );
        check.detail = QStringLiteral( "cannot allocate OGRSpatialReference" );
        return check;
    }
    const OGRErr err = OSRSetFromUserInput( srs, "EPSG:4326" );
    OSRDestroySpatialReference( srs );
    if ( err != OGRERR_NONE )
    {
        check.status = QStringLiteral( "failed" );
        check.detail = QStringLiteral(
                         "EPSG:4326 does not resolve — check GDAL_DATA/PROJ_LIB "
                         "(typical broken offline install)" );
        return check;
    }
    check.status = QStringLiteral( "ok" );
    check.detail = QStringLiteral( "EPSG:4326 resolves" );
    return check;
}

Check checkOfflineGate()
{
    Check check;
    check.name = QStringLiteral( "offline_gate" );
    check.extra["engaged"] = sicnu::data::offline::enabled();
    check.extra["env_flag"] = sicnu::data::offline::enabledFromEnv();
    check.extra["remote_target_refused"] =
      sicnu::data::offline::isRemoteTarget( QStringLiteral( "/vsicurl/https://example.test/r.tif" ) );
    check.status = QStringLiteral( "ok" );
    check.detail = sicnu::data::offline::enabled()
                     ? QStringLiteral( "gate engaged — remote opens refuse" )
                     : QStringLiteral( "gate open (set SICNU_OFFLINE=1 for the classroom)" );
    return check;
}

Check checkLabPacks( const QString &packRoot, const QString &labId, bool packsOnly )
{
    Check check;
    check.name = QStringLiteral( "lab_data_packs" );

    const QString packsDir = QDir( packRoot ).filePath( QStringLiteral( "data/labs/packs" ) );
    if ( !QDir( packsDir ).exists() )
    {
        check.status = QStringLiteral( "failed" );
        check.detail = QStringLiteral( "packs directory missing: %1" ).arg( packsDir );
        return check;
    }

    QVector<sicnu::agent::LabDataPackResult> problems;
    const auto packs =
      sicnu::agent::LabPackVerifier::loadPacksFromDir( packsDir, &problems );

    Json::Value evidence( Json::objectValue );
    Json::Value packStates( Json::objectValue );
    bool failed = !problems.isEmpty();
    bool degraded = false;
    for ( const auto &problem : problems )
    {
        packStates[QStringLiteral( "<load-error>" ).toStdString()] =
          problem.errorCode().toStdString();
        check.detail += problem.errorMessage() + QStringLiteral( "; " );
    }
    for ( const auto &pack : packs )
    {
        if ( !labId.isEmpty() && pack.labId != labId )
            continue;
        const auto verification = sicnu::agent::LabPackVerifier::verify( pack, packRoot );
        packStates[pack.labId.toStdString()] = verification.overall.toStdString();
        if ( verification.overall == QLatin1String( "failed" ) )
        {
            failed = true;
            check.detail += QStringLiteral( "%1: %2; " ).arg( pack.labId, verification.overall );
        }
        else if ( verification.overall == QLatin1String( "degraded" ) )
        {
            // Regenerable inputs missing in a fresh deployment are honest
            // degraded state, not a failure — the classroom runs GENERATE first.
            if ( !packsOnly || pack.labId == QLatin1String( "grading_corpus" ) )
                degraded = true;
        }
    }
    evidence["packs"] = packStates;
    check.extra = evidence;
    check.status = failed ? QStringLiteral( "failed" )
                   : degraded ? QStringLiteral( "degraded" )
                              : QStringLiteral( "ok" );
    if ( check.detail.isEmpty() )
        check.detail = QStringLiteral( "%1 packs verified" ).arg( packs.size() );
    return check;
}

Check checkGradingRules( const QString &rulesDir )
{
    Check check;
    check.name = QStringLiteral( "grading_rules" );
    const sicnu::agent::OutputVerifier verifier;

    // Every pack with a grading corpus? No — enumerate the rules directory
    // directly when it exists; else fall back to a rules-probe per pack id.
    QString dir = rulesDir;
    if ( dir.isEmpty() )
    {
#ifdef SICNU_SOURCE_DIR
        dir = QString::fromUtf8( SICNU_SOURCE_DIR ) + QStringLiteral( "/data/labs/grading" );
#endif
    }
    const QDir rules( dir );
    if ( !rules.exists() )
    {
        check.status = QStringLiteral( "degraded" );
        check.detail = QStringLiteral( "rules directory not found: %1" ).arg( dir );
        return check;
    }

    const auto ruleFiles =
      rules.entryList( QStringList() << QStringLiteral( "*.rules.json" ), QDir::Files, QDir::Name );
    Json::Value evidence( Json::objectValue );
    bool failed = false;
    int parsed = 0;
    for ( const QString &file : ruleFiles )
    {
        const QString labId = file.chopped( QStringLiteral( ".rules.json" ).size() );
        // The REAL grading seam is the oracle. Raster-mode rules stop with a
        // usage error naming the ARTIFACT (rules resolved and parsed);
        // file-mode rules grade the (missing) submission and return a graded
        // result. Any other usage message = the rules themselves are broken.
        const auto result = verifier.gradeArtifact( labId, QString() );
        const bool rulesParsed =
          result.graded
          || result.error.contains( QLatin1String( "Artifact does not exist" ) );
        evidence[labId.toStdString()] =
          rulesParsed ? Json::Value( "parsed" ) : Json::Value( result.error.toStdString() );
        if ( !rulesParsed )
            failed = true;
        else
            ++parsed;
    }
    check.extra = evidence;
    check.status = failed ? QStringLiteral( "failed" ) : QStringLiteral( "ok" );
    check.detail = QStringLiteral( "%1/%2 rules files parse" ).arg( parsed ).arg( ruleFiles.size() );
    return check;
}

} // namespace

Json::Value runLabSelfCheck( const LabSelfCheckOptions &options, bool *ok )
{
    QString packRoot = options.packRoot;
    if ( packRoot.isEmpty() )
        packRoot = LAB_SELF_CHECK_DEFAULT_ROOT;

    QVector<Check> checks;
    checks.append( checkOfflineGate() );
    checks.append( checkProjAuthority() );
    if ( !packRoot.isEmpty() )
        checks.append( checkLabPacks( packRoot, options.labId, options.packsOnly ) );
    checks.append( checkGradingRules( options.rulesDir ) );

    bool failed = false;
    bool degraded = false;
    Json::Value checkArray( Json::arrayValue );
    for ( const Check &check : checks )
    {
        checkArray.append( checkJson( check ) );
        if ( check.status == QLatin1String( "failed" ) )
            failed = true;
        else if ( check.status == QLatin1String( "degraded" ) )
            degraded = true;
    }

    Json::Value document( Json::objectValue );
    document["schema"] = kSchema;
    document["overall"] = failed ? "failed" : degraded ? "degraded" : "ok";
    document["checks"] = checkArray;
    if ( ok )
        *ok = !failed;
    return document;
}

} // namespace sicnu::cli
