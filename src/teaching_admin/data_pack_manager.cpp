#include "data_pack_manager.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace sicnu::teaching_admin {

namespace {

// Provenance tiers of the sicnu.lab-pack/1 authority
// (src/agent/lab_data_pack.h): verification strength follows the tier.
constexpr const char *kProvenanceCommitted = "committed-fixture";
constexpr const char *kProvenanceGeneratedTmp = "generated-tmp";
constexpr const char *kProvenanceGeneratedSamples = "generated-samples";

bool isKnownProvenance( const QString &p )
{
    return p == QLatin1String( kProvenanceCommitted )
           || p == QLatin1String( kProvenanceGeneratedTmp )
           || p == QLatin1String( kProvenanceGeneratedSamples );
}

bool isCommittedFixture( const QString &p )
{
    return p == QLatin1String( kProvenanceCommitted );
}

bool isHexSha256( const QString &s )
{
    if ( s.size() != 64 )
        return false;
    for ( const QChar &c : s )
    {
        if ( !( ( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) )
                || ( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'f' ) )
                || ( c >= QLatin1Char( 'A' ) && c <= QLatin1Char( 'F' ) ) ) )
            return false;
    }
    return true;
}

/// Post-resolution containment: the input's absolute path (and, when the
/// file exists, its canonical target) must stay under the canonical repo
/// root — lexical `..` rejection alone misses symlink escapes.
bool staysUnderRoot( const QString &repoRoot, const QString &rel )
{
    const QString rootCanon =
      QFileInfo( repoRoot ).canonicalFilePath();
    const QString root = rootCanon.isEmpty() ? QDir( repoRoot ).absolutePath() : rootCanon;
    QString normalized = rel;
    normalized.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
    const QString abs = QDir( root ).filePath( normalized );
    if ( !abs.startsWith( root + QLatin1Char( '/' ) ) && abs != root )
        return false;
    const QString canonical = QFileInfo( abs ).canonicalFilePath();
    if ( !canonical.isEmpty() && !canonical.startsWith( root + QLatin1Char( '/' ) ) )
        return false;
    return true;
}

} // namespace

QJsonObject PackInventoryEntry::toJson() const
{
    QJsonArray issueArr = issuesToJson( issues );
    return QJsonObject{
        { QStringLiteral( "pack_id" ), packId },
        { QStringLiteral( "path" ), path },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "pack_version" ), packVersion },
        { QStringLiteral( "declared_bytes" ), static_cast<double>( declaredBytes ) },
        { QStringLiteral( "computed_bytes" ), static_cast<double>( computedBytes ) },
        { QStringLiteral( "digest" ), digest },
        { QStringLiteral( "offline_available" ), offlineAvailable },
        { QStringLiteral( "generator" ), generator },
        { QStringLiteral( "crs_grid_summary" ), crsGridSummary },
        { QStringLiteral( "issues" ), issueArr },
    };
}

QJsonObject PackInventory::toJson() const
{
    QJsonArray arr;
    for ( const auto &p : packs )
        arr.append( p.toJson() );
    return QJsonObject{
        { QStringLiteral( "packs" ), arr },
        { QStringLiteral( "total_declared_bytes" ), static_cast<double>( totalDeclaredBytes ) },
        { QStringLiteral( "byte_budget" ), static_cast<double>( byteBudget ) },
        { QStringLiteral( "within_budget" ), withinBudget },
    };
}

ValidationResult validatePackDocument( const QJsonObject &pack, const QString &repoRoot )
{
    ValidationResult r;
    const QString schema = pack.value( QStringLiteral( "schema_version" ) ).toString(
        pack.value( QStringLiteral( "schema" ) ).toString() );
    if ( schema != QLatin1String( "sicnu.lab-pack/1" ) )
        r.addError( QStringLiteral( "pack_schema" ), QStringLiteral( "schema_version" ),
                    QStringLiteral( "must be sicnu.lab-pack/1" ) );

    const QJsonArray inputs = pack.value( QStringLiteral( "inputs" ) ).toArray();
    QSet<QString> seen;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        const QJsonObject in = inputs.at( i ).toObject();
        const QString path = in.value( QStringLiteral( "path" ) ).toString();
        const QString at = QStringLiteral( "inputs[%1].path" ).arg( i );
        // Parity with the pack authority loader (lab_data_pack): repo/bundle
        // paths are forward-slash; a backslash is rejected, not normalized.
        if ( path.contains( QLatin1Char( '\\' ) ) )
            r.addError( QStringLiteral( "backslash_path" ), at,
                        QStringLiteral( "pack input paths must use forward slashes" ) );
        if ( isUnsafeRelativePath( path ) )
            r.addError( QStringLiteral( "path_traversal" ), at,
                        QStringLiteral( "pack input path escapes root or is absolute" ) );
        else if ( !repoRoot.isEmpty() && !staysUnderRoot( repoRoot, path ) )
            r.addError( QStringLiteral( "path_escape" ), at,
                        QStringLiteral( "pack input resolves outside the repo root" ) );
        if ( seen.contains( path ) )
            r.addError( QStringLiteral( "duplicate_pack_path" ), at,
                        QStringLiteral( "duplicate input path" ) );
        seen.insert( path );

        // Provenance contract of the pack authority: value must be a known
        // tier, and committed fixtures must pin a sha256 digest.
        const QString provenance = in.value( QStringLiteral( "provenance" ) ).toString(
            QString::fromLatin1( kProvenanceGeneratedSamples ) );
        const QString provAt = QStringLiteral( "inputs[%1].provenance" ).arg( i );
        if ( !isKnownProvenance( provenance ) )
            r.addError( QStringLiteral( "invalid_provenance" ), provAt,
                        QStringLiteral( "provenance must be committed-fixture | generated-samples | generated-tmp" ) );
        const QString sha = in.value( QStringLiteral( "sha256" ) ).toString();
        if ( !sha.isEmpty() && !isHexSha256( sha ) )
            r.addError( QStringLiteral( "invalid_sha256" ),
                        QStringLiteral( "inputs[%1].sha256" ).arg( i ),
                        QStringLiteral( "sha256 must be 64 hex chars" ) );
        else if ( isCommittedFixture( provenance ) && sha.isEmpty() )
            r.addError( QStringLiteral( "missing_sha256" ),
                        QStringLiteral( "inputs[%1].sha256" ).arg( i ),
                        QStringLiteral( "committed-fixture input requires sha256" ) );
        // Same parity: a required role and a byte pin on committed fixtures.
        if ( !in.contains( QStringLiteral( "role" ) )
             || in.value( QStringLiteral( "role" ) ).toString().isEmpty() )
            r.addError( QStringLiteral( "missing_role" ),
                        QStringLiteral( "inputs[%1].role" ).arg( i ),
                        QStringLiteral( "pack input requires a role" ) );
        if ( isCommittedFixture( provenance )
             && static_cast<qint64>( in.value( QStringLiteral( "bytes" ) ).toDouble( -1 ) ) <= 0 )
            r.addError( QStringLiteral( "missing_bytes" ),
                        QStringLiteral( "inputs[%1].bytes" ).arg( i ),
                        QStringLiteral( "committed-fixture input requires a byte size" ) );
    }
    return r;
}

PackInventory inventoryPacks( const QString &packsDir, const QString &repoRoot, qint64 byteBudgetBytes )
{
    PackInventory inv;
    inv.byteBudget = byteBudgetBytes;
    QDir dir( packsDir );
    if ( !dir.exists() )
        return inv;

    const auto files = dir.entryList( { QStringLiteral( "*.pack.json" ) }, QDir::Files, QDir::Name );
    for ( const QString &name : files )
    {
        PackInventoryEntry e;
        e.path = dir.filePath( name );
        e.packId = name;
        if ( e.packId.endsWith( QLatin1String( ".pack.json" ) ) )
            e.packId.chop( 10 );

        QFile f( e.path );
        if ( !f.open( QIODevice::ReadOnly ) )
        {
            e.issues.push_back( { QStringLiteral( "pack_unreadable" ), e.path,
                                  QStringLiteral( "cannot read pack" ), QStringLiteral( "error" ) } );
            inv.packs.push_back( e );
            continue;
        }
        const QByteArray raw = f.readAll();
        e.digest = sha256Hex( raw );
        const QJsonObject pack = QJsonDocument::fromJson( raw ).object();
        e.labId = pack.value( QStringLiteral( "lab_id" ) ).toString();
        e.packVersion = pack.value( QStringLiteral( "pack_version" ) ).toString();
        e.generator = pack.value( QStringLiteral( "generator" ) ).toString();
        e.declaredBytes = static_cast<qint64>( pack.value( QStringLiteral( "declared_offline_bytes" ) ).toDouble( -1 ) );

        const ValidationResult vr = validatePackDocument( pack, repoRoot );
        e.issues = vr.issues;

        qint64 sum = 0;
        bool allPresent = true;
        bool digestsOk = true;
        qint64 declaredPerInputSum = 0;
        QStringList truths;
        for ( int idx = 0; idx < pack.value( QStringLiteral( "inputs" ) ).toArray().size(); ++idx )
        {
            const QJsonObject in =
              pack.value( QStringLiteral( "inputs" ) ).toArray().at( idx ).toObject();
            const QString rel = in.value( QStringLiteral( "path" ) ).toString();
            const QString at = QStringLiteral( "inputs[%1]" ).arg( idx );
            const QString provenance = in.value( QStringLiteral( "provenance" ) ).toString(
                QString::fromLatin1( kProvenanceGeneratedSamples ) );
            const bool committed = isCommittedFixture( provenance );
            if ( isUnsafeRelativePath( rel ) )
            {
                allPresent = false;
                continue;
            }
            const QString abs = QDir( repoRoot ).filePath( rel );
            QFileInfo fi( abs );
            if ( fi.exists() && fi.isFile() )
            {
                sum += fi.size();
                // Digest verification (strength follows the tier, as in the
                // pack authority): declared sha256 on a committed fixture is
                // a hard check, elsewhere an informative warning.
                const QString declaredSha = in.value( QStringLiteral( "sha256" ) ).toString();
                if ( !declaredSha.isEmpty() )
                {
                    const QString actual = sha256OfFile( abs );
                    if ( actual.compare( declaredSha, Qt::CaseInsensitive ) != 0 )
                    {
                        digestsOk = false;
                        e.issues.push_back(
                          { QStringLiteral( "digest_mismatch" ), at,
                            QStringLiteral( "sha256 mismatch: declared %1, computed %2" )
                              .arg( declaredSha, actual ),
                            committed ? QStringLiteral( "error" ) : QStringLiteral( "warning" ) } );
                    }
                }
            }
            else
            {
                allPresent = false;
                e.issues.push_back(
                  { QStringLiteral( "input_missing" ), at,
                    QStringLiteral( "input file missing: " ) + rel,
                    committed ? QStringLiteral( "error" ) : QStringLiteral( "warning" ) } );
            }
            const QString truth = in.value( QStringLiteral( "sensor_truth" ) ).toString();
            if ( !truth.isEmpty() )
                truths.append( truth.left( 80 ) );
            const qint64 declared = static_cast<qint64>( in.value( QStringLiteral( "bytes" ) ).toDouble( -1 ) );
            if ( declared > 0 )
            {
                declaredPerInputSum += declared;
                // Byte pin: hard for committed fixtures (drift means the
                // deployment is not the audited one), informative elsewhere.
                if ( fi.exists() && fi.isFile()
                     && declared != static_cast<qint64>( fi.size() ) )
                    e.issues.push_back(
                      { QStringLiteral( "byte_mismatch" ), at,
                        QStringLiteral( "declared %1 bytes, actual %2" )
                          .arg( declared )
                          .arg( static_cast<qint64>( fi.size() ) ),
                        committed ? QStringLiteral( "error" ) : QStringLiteral( "warning" ) } );
            }
        }
        e.computedBytes = sum;
        if ( e.declaredBytes < 0 )
            e.declaredBytes = declaredPerInputSum > 0 ? declaredPerInputSum : sum;
        // Pack-level declared_offline_bytes vs the sum of the inputs.
        const qint64 packDeclared =
          static_cast<qint64>( pack.value( QStringLiteral( "declared_offline_bytes" ) ).toDouble( -1 ) );
        if ( packDeclared >= 0 && packDeclared != e.computedBytes )
            e.issues.push_back(
              { QStringLiteral( "byte_mismatch" ), QStringLiteral( "declared_offline_bytes" ),
                QStringLiteral( "pack declares %1 bytes, inputs compute %2" )
                  .arg( packDeclared )
                  .arg( e.computedBytes ),
                QStringLiteral( "warning" ) } );
        e.offlineAvailable = allPresent && digestsOk && vr.ok;
        e.crsGridSummary = truths.join( QStringLiteral( " | " ) );

        inv.totalDeclaredBytes += std::max<qint64>( 0, e.declaredBytes );
        inv.packs.push_back( e );
    }
    inv.withinBudget = inv.totalDeclaredBytes <= inv.byteBudget;
    return inv;
}

} // namespace sicnu::teaching_admin
