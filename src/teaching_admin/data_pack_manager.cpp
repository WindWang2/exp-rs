#include "data_pack_manager.h"
#include "json_util.h"

#include "lab_pack/lab_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace sicnu::teaching_admin {

namespace {

// sicnu.lab-pack/1 provenance tiers (src/lab_pack/lab_pack.h): verification
// strength follows the tier.
constexpr const char *kProvenanceCommitted = "committed-fixture";

bool isCommittedFixture( const QString &p )
{
    return p == QLatin1String( kProvenanceCommitted );
}

/// Post-resolution containment: the input's absolute path (and, when the
/// file exists, its canonical target) must stay under the canonical repo
/// root — lexical `..` rejection alone misses symlink escapes. This is the
/// admin-specific guard (the leaf knows no repoRoot); everything structural
/// about the pack document itself is validated by the shared leaf parser.
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

/// Admin-specific containment codes for the inputs array. Structural
/// authority rejections (sha256 shape, provenance tiers, duplicate paths,
/// required strings, …) are NOT re-implemented here — the shared leaf parser
/// owns them and its typed failure is mapped below.
ValidationResult validateInputContainment( const QJsonArray &inputs, const QString &repoRoot )
{
    ValidationResult r;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        const QJsonObject in = inputs.at( i ).toObject();
        const QString path = in.value( QStringLiteral( "path" ) ).toString();
        const QString at = QStringLiteral( "inputs[%1].path" ).arg( i );
        if ( path.isEmpty() )
            continue; // the authority parser reports the missing path itself
        if ( isUnsafeRelativePath( path ) )
        {
            r.addError( QStringLiteral( "path_traversal" ), at,
                        QStringLiteral( "pack input path escapes root or is absolute" ) );
        }
        else if ( !repoRoot.isEmpty() && !staysUnderRoot( repoRoot, path ) )
        {
            r.addError( QStringLiteral( "path_escape" ), at,
                        QStringLiteral( "pack input resolves outside the repo root" ) );
        }
    }
    return r;
}

/// Map the shared leaf parser's typed failure onto the admin issue
/// vocabulary. The admin adds NO parse decisions of its own — the class code
/// is a projection of the authority's (first-error-wins, exactly like the
/// agent-side loader).
ValidationResult validatePackDocumentViaAuthority( const QJsonObject &pack,
                                                   const QString &repoRoot )
{
    ValidationResult r = validateInputContainment(
      pack.value( QStringLiteral( "inputs" ) ).toArray(), repoRoot );

    const QByteArray canonicalBytes = QJsonDocument( pack ).toJson( QJsonDocument::Compact );
    const sicnu::labpack::PackLoadResult leaf = sicnu::labpack::PackVerifier::loadFromBytes(
      std::string( canonicalBytes.constData(),
                   static_cast<std::size_t>( canonicalBytes.size() ) ),
      "pack document" );
    if ( !leaf.ok )
    {
        QString code = QString::fromStdString( leaf.errorCode );
        if ( code == QLatin1String( "lab.pack_schema" ) )
            code = QStringLiteral( "pack_schema" );
        else if ( code == QLatin1String( "lab.pack_field" ) )
            code = QStringLiteral( "pack_field" );
        else if ( code == QLatin1String( "lab.pack_input" ) )
            code = QStringLiteral( "pack_input" );
        else
            code = QStringLiteral( "pack_schema" ); // unknown authority class: conservative
        r.addError( code, QStringLiteral( "pack" ), QString::fromStdString( leaf.errorMessage ) );
    }
    return r;
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
    return validatePackDocumentViaAuthority( pack, repoRoot );
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
        // Display projection only (lab id / version / generator strings for
        // the teacher UI); every VALIDATION decision below comes from the
        // shared leaf parser over the same bytes.
        const QJsonObject pack = QJsonDocument::fromJson( raw ).object();
        e.labId = pack.value( QStringLiteral( "lab_id" ) ).toString();
        e.packVersion = pack.value( QStringLiteral( "pack_version" ) ).toString();
        e.generator = pack.value( QStringLiteral( "generator" ) ).toString();
        e.declaredBytes = static_cast<qint64>( pack.value( QStringLiteral( "declared_offline_bytes" ) ).toDouble( -1 ) );

        const ValidationResult vr = validatePackDocumentViaAuthority( pack, repoRoot );
        e.issues = vr.issues;
        const bool authorityOk = vr.ok;

        // Structural inputs from the ONE parser: re-serialize once more for
        // the leaf view so the inventory loop works on authority-typed data.
        const sicnu::labpack::PackLoadResult leaf = sicnu::labpack::PackVerifier::loadFromBytes(
          std::string( raw.constData(), static_cast<std::size_t>( raw.size() ) ),
          e.packId.toStdString() );

        qint64 sum = 0;
        bool allPresent = true;
        bool digestsOk = true;
        qint64 declaredPerInputSum = 0;
        QStringList truths;
        if ( leaf.ok )
        {
            for ( std::size_t inputIdx = 0; inputIdx < leaf.pack.inputs.size(); ++inputIdx )
            {
                const sicnu::labpack::PackInput &in = leaf.pack.inputs.at( inputIdx );
                const QString rel = QString::fromStdString( in.path );
                const QString provenance = QString::fromStdString(
                  sicnu::labpack::provenanceToString( in.provenance ) );
                const bool committed = isCommittedFixture( provenance );
                const QString at =
                  QStringLiteral( "inputs[%1]" ).arg( static_cast<int>( inputIdx ) );
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
                    // Digest verification (strength follows the tier, as in
                    // the pack authority): declared sha256 on a committed
                    // fixture is a hard check, elsewhere an informative
                    // warning. The leaf parser already pinned the declared
                    // digest to 64 LOWERCASE hex chars, so the comparison is
                    // exact-case.
                    if ( !in.sha256.empty() )
                    {
                        const QString declaredSha = QString::fromStdString( in.sha256 );
                        const QString actual = sha256OfFile( abs );
                        if ( actual != declaredSha )
                        {
                            // Strength follows the tier (leaf policy): only
                            // a committed fixture's digest pins availability;
                            // elsewhere the mismatch stays an informative
                            // warning so admin and agent agree on the verdict.
                            if ( committed )
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
                if ( !in.sensorTruth.empty() )
                    truths.append( QString::fromStdString( in.sensorTruth ).left( 80 ) );
                if ( in.declaredBytes > 0 )
                {
                    declaredPerInputSum += in.declaredBytes;
                    // Byte pin: hard for committed fixtures (drift means the
                    // deployment is not the audited one), informative elsewhere.
                    if ( fi.exists() && fi.isFile()
                         && in.declaredBytes != static_cast<qint64>( fi.size() ) )
                        e.issues.push_back(
                          { QStringLiteral( "byte_mismatch" ), at,
                            QStringLiteral( "declared %1 bytes, actual %2" )
                              .arg( in.declaredBytes )
                              .arg( static_cast<qint64>( fi.size() ) ),
                            committed ? QStringLiteral( "error" ) : QStringLiteral( "warning" ) } );
                }
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
        // An authority-refused pack is never offline-available — the agent
        // loader would refuse it too (one parser truth).
        e.offlineAvailable = authorityOk && leaf.ok && allPresent && digestsOk;
        e.crsGridSummary = truths.join( QStringLiteral( " | " ) );

        inv.totalDeclaredBytes += std::max<qint64>( 0, e.declaredBytes );
        inv.packs.push_back( e );
    }
    inv.withinBudget = inv.totalDeclaredBytes <= inv.byteBudget;
    return inv;
}

} // namespace sicnu::teaching_admin
