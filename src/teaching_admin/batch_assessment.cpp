#include "batch_assessment.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include "platform/durable_sidecar.h"
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

namespace sicnu::teaching_admin {

namespace {

constexpr const char *kBatchCheckpointSchema = "sicnu.teaching.batch_checkpoint/1";
/// Upper bound for the worker pool regardless of config (a runaway
/// maxConcurrency must not fork the machine).
constexpr int kMaxBatchWorkers = 16;

QJsonObject batchConfigDigestInput( const BatchAssessmentConfig &cfg )
{
    // Semantic identity of a batch run: what was graded where, with which
    // versions. Deliberately excludes maxConcurrency (a scheduling detail)
    // and checkpointPath (a durability detail) — both may change between
    // restarts without invalidating completed rows.
    return sortKeys( QJsonObject{
        { QStringLiteral( "lab_id" ), cfg.labId },
        { QStringLiteral( "max_submissions" ), cfg.maxSubmissions },
        { QStringLiteral( "rubric_version" ), cfg.rubricVersion },
        { QStringLiteral( "lab_version" ), cfg.labVersion },
        { QStringLiteral( "software_version" ), cfg.softwareVersion },
        { QStringLiteral( "submissions_dir" ), cfg.submissionsDir },
    } );
}

QString batchConfigDigest( const BatchAssessmentConfig &cfg )
{
    return sha256Hex( canonicalJsonBytes( batchConfigDigestInput( cfg ) ) );
}

/// Artifact identity for checkpoint adoption, taken when a row is graded:
/// mtime + size + a bounded sha256 over sampled content windows. The
/// stat pair alone adopts a stale grade over a same-stat content swap
/// (cp -p with a backdated mtime, or a same-length rewrite with the mtime
/// restored — red tests in test_teaching_admin_checkpoint_stamp_r16_c_t06),
/// so the stamp also fingerprints content. Head-only hashing still adopted
/// over tail-only rewrites on artifacts past the window (round-1 review):
/// classroom rasters are typically far larger than one window with
/// informative pixels throughout, so both ends are fingerprinted.
///
/// #1520: head+tail alone left the INTERIOR of a >2×-window artifact
/// unfingerprinted (a same-stat, same-length rewrite confined to the middle
/// was adopted as the graded original, and the #1448 re-grade gate never
/// fired). The interior is now sampled at fixed stride: kStampMiddleWindows
/// evenly spaced windows between the two end windows. The windows may
/// overlap (files ≲ 8× window are fully covered) — this is a fingerprint,
/// not a dedup — and the bytes read stay bounded at
/// (kStampMiddleWindows + 2) × window regardless of artifact size.
/// The encoding stays opaque to the checkpoint schema (#1448 left it
/// evolvable — old mtime:size, head-only and head+tail stamps simply stop
/// matching and fail closed to re-grading).
/// Empty when the artifact is gone or unreadable (unverifiable → re-grade).
QString artifactStamp( const QString &path )
{
    const QFileInfo info( path );
    if ( !info.exists() )
        return QString();
    constexpr qint64 kStampWindowBytes = 64 * 1024;
    constexpr int kStampMiddleWindows = 6;
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return QString();
    const qint64 size = info.size();
    QByteArray fingerprint = f.read( kStampWindowBytes );
    if ( size > 2 * kStampWindowBytes )
    {
        // Fixed-stride interior sampling: stride is chosen so every sample
        // offset lands strictly inside [head window, tail window).
        const qint64 interior = size - 2 * kStampWindowBytes;
        const qint64 stride = interior / ( kStampMiddleWindows + 1 );
        for ( int i = 1; i <= kStampMiddleWindows; ++i )
        {
            if ( !f.seek( kStampWindowBytes + i * stride ) )
                return QString();
            fingerprint += f.read( kStampWindowBytes );
        }
    }
    if ( size > kStampWindowBytes
         && f.seek( size - kStampWindowBytes ) )
        fingerprint += f.read( kStampWindowBytes );
    return QString::number( info.lastModified().toMSecsSinceEpoch() ) + QLatin1Char( ':' )
         + QString::number( size ) + QLatin1Char( ':' )
         + sha256Hex( fingerprint );
}

/// A durable checkpoint row plus the artifact identity it was graded
/// against. Checkpoints from older builds carry no stamp and fail closed to
/// re-grading — adoption without identity verification would publish a
/// stale grade over a re-submitted artifact (#1448).
struct DurableRow
{
    BatchRowResult row;
    QString artifactStamp;
};

/// Atomically rewrite the checkpoint (temp + rename) from @p rows — always
/// the full, index-ordered set, so the file is never a partial document.
bool saveCheckpointAtomic( const QString &path, const QString &configDigest, int total,
                           const QVector<DurableRow> &rows )
{
    QJsonArray rowsArr;
    for ( const auto &entry : rows )
    {
        QJsonObject rowJson = entry.row.toJson();
        if ( !entry.artifactStamp.isEmpty() )
            rowJson.insert( QStringLiteral( "artifact_stamp" ), entry.artifactStamp );
        rowsArr.append( rowJson );
    }
    const QJsonObject doc = sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), kBatchCheckpointSchema },
        { QStringLiteral( "config_digest" ), configDigest },
        { QStringLiteral( "total" ), total },
        { QStringLiteral( "rows" ), rowsArr },
    } );
    const QByteArray bytes = QJsonDocument( doc ).toJson( QJsonDocument::Indented );
    // R6: the single sidecar write authority — temp + fsync + atomic publish;
    // a torn checkpoint can no longer be renamed over the resume state.
    const sicnu::platform::sidecar::WriteResult result = sicnu::platform::sidecar::write(
      { path.toUtf8().constData(),
        std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ), "" } );
    return static_cast<bool>( result );
}

/// Shared atomic byte write for every batch artifact (checkpoint, JSON and
/// CSV reports) — one QSaveFile lane replaced by the R6 sidecar authority.
bool writeBytesAtomically( const QString &path, const QByteArray &bytes )
{
    const sicnu::platform::sidecar::WriteResult result = sicnu::platform::sidecar::write(
      { path.toUtf8().constData(),
        std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ), "" } );
    return static_cast<bool>( result );
}
/// Loads checkpoint rows whose config digest matches the current run. A
/// missing/corrupt/foreign checkpoint degrades to "no adopted rows" (fresh
/// start) — best-effort durability, never a wrong grade.
QVector<DurableRow> loadCheckpointRows( const QString &path, const QString &configDigest )
{
    // Bounded read (R14-E audit F5): a tampered multi-GB checkpoint beside
    // the submissions must degrade to a fresh start, not an OOM slurp.
    constexpr qint64 kMaxCheckpointBytes = 64 * 1024 * 1024;
    const BoundedReadResult read = readBounded( path, kMaxCheckpointBytes );
    if ( read.oversize || read.unreadable )
        return {};
    const QJsonObject doc = QJsonDocument::fromJson( read.bytes ).object();
    if ( doc.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( kBatchCheckpointSchema ) )
        return {};
    if ( doc.value( QStringLiteral( "config_digest" ) ).toString() != configDigest )
        return {};
    QVector<DurableRow> rows;
    const QJsonArray arr = doc.value( QStringLiteral( "rows" ) ).toArray();
    rows.reserve( arr.size() );
    // Adopt only rows whose status is in the orchestrator's own vocabulary:
    // a tampered or corrupt checkpoint degrades to a fresh start for that
    // row instead of importing garbage into the published report.
    static const QSet<QString> kKnownStatuses = {
        QStringLiteral( "pass" ),   QStringLiteral( "fail" ),
        QStringLiteral( "error" ),  QStringLiteral( "timeout" ),
        QStringLiteral( "crash" ),  QStringLiteral( "unavailable" ),
        QStringLiteral( "corrupted" ),
    };
    for ( const auto &v : arr )
    {
        const QJsonObject rowJson = v.toObject();
        DurableRow entry;
        entry.row = BatchRowResult::fromJson( rowJson );
        if ( !kKnownStatuses.contains( entry.row.status ) || entry.row.studentId.isEmpty() )
            continue;
        entry.artifactStamp = rowJson.value( QStringLiteral( "artifact_stamp" ) ).toString();
        rows.append( entry );
    }
    return rows;
}

} // namespace

QJsonObject BatchRowResult::toJson() const
{
    return sortKeys( QJsonObject{
        { QStringLiteral( "student_id" ), studentId },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "status" ), status },
        { QStringLiteral( "score" ), score },
        { QStringLiteral( "verdict" ), verdict },
        { QStringLiteral( "message" ), message },
        { QStringLiteral( "artifact_path" ), artifactPath },
        { QStringLiteral( "rubric_version" ), rubricVersion },
        { QStringLiteral( "lab_version" ), labVersion },
        { QStringLiteral( "software_version" ), softwareVersion },
        { QStringLiteral( "missing_evidence" ), missingEvidence },
        { QStringLiteral( "grader_digest" ), graderDigest },
        { QStringLiteral( "top_deduction" ), topDeduction },
        { QStringLiteral( "unavailable_reason" ), unavailableReason },
    } );
}

BatchRowResult BatchRowResult::fromJson( const QJsonObject &o )
{
    BatchRowResult row;
    row.studentId = o.value( QStringLiteral( "student_id" ) ).toString();
    row.labId = o.value( QStringLiteral( "lab_id" ) ).toString();
    row.status = o.value( QStringLiteral( "status" ) ).toString();
    row.score = o.value( QStringLiteral( "score" ) ).toDouble( -1.0 );
    row.verdict = o.value( QStringLiteral( "verdict" ) ).toString();
    row.message = o.value( QStringLiteral( "message" ) ).toString();
    row.artifactPath = o.value( QStringLiteral( "artifact_path" ) ).toString();
    row.rubricVersion = o.value( QStringLiteral( "rubric_version" ) ).toString();
    row.labVersion = o.value( QStringLiteral( "lab_version" ) ).toString();
    row.softwareVersion = o.value( QStringLiteral( "software_version" ) ).toString();
    row.missingEvidence = o.value( QStringLiteral( "missing_evidence" ) ).toBool();
    row.graderDigest = o.value( QStringLiteral( "grader_digest" ) ).toString();
    row.topDeduction = o.value( QStringLiteral( "top_deduction" ) ).toString();
    row.unavailableReason = o.value( QStringLiteral( "unavailable_reason" ) ).toString();
    return row;
}

QJsonObject BatchAssessmentReport::toJson() const
{
    QJsonArray rowsArr;
    // Deterministic order by student_id
    QVector<BatchRowResult> sorted = rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const BatchRowResult &a, const BatchRowResult &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.artifactPath < b.artifactPath;
               } );
    for ( const auto &row : sorted )
        rowsArr.append( row.toJson() );
    // Refused files ride along in discovery order (already sorted): the
    // accounting is part of the published outcome, so a class report can
    // be audited for silent omissions (#1466).
    QJsonArray skipsArr;
    for ( const auto &skip : skippedFiles )
    {
        skipsArr.append( sortKeys( QJsonObject{
            { QStringLiteral( "student_id" ), skip.studentId },
            { QStringLiteral( "path" ), skip.path },
            { QStringLiteral( "reason" ), skip.reason },
        } ) );
    }
    return sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), schema },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "total" ), total },
        { QStringLiteral( "graded" ), graded },
        { QStringLiteral( "failed" ), failed },
        { QStringLiteral( "corrupted" ), corrupted },
        { QStringLiteral( "cancelled" ), cancelled },
        { QStringLiteral( "missing_evidence" ), missingEvidence },
        { QStringLiteral( "unavailable" ), unavailable },
        { QStringLiteral( "cancelled_early" ), cancelledEarly },
        { QStringLiteral( "truncated" ), truncated },
        { QStringLiteral( "resumed" ), resumed },
        { QStringLiteral( "skipped" ), skippedFiles.size() },
        { QStringLiteral( "skipped_files" ), skipsArr },
        { QStringLiteral( "rubric_version" ), rubricVersion },
        { QStringLiteral( "lab_version" ), labVersion },
        { QStringLiteral( "software_version" ), softwareVersion },
        { QStringLiteral( "rows" ), rowsArr },
    } );
}

SubmissionKind classifySubmission( const QString &path )
{
    const QString lower = path.toLower();
    if ( lower.endsWith( QLatin1String( ".capsule.json" ) ) || lower.contains( QLatin1String( "capsule" ) ) )
        return SubmissionKind::Capsule;
    if ( lower.endsWith( QLatin1String( ".labreport.json" ) ) || lower.endsWith( QLatin1String( ".report.json" ) ) )
        return SubmissionKind::LabReport;
    if ( lower.endsWith( QLatin1String( ".zip" ) ) || lower.endsWith( QLatin1String( ".bundle" ) ) )
        return SubmissionKind::Bundle;
    if ( lower.endsWith( QLatin1String( ".tif" ) ) || lower.endsWith( QLatin1String( ".tiff" ) )
         || lower.endsWith( QLatin1String( ".png" ) ) || lower.endsWith( QLatin1String( ".json" ) ) )
        return SubmissionKind::ArtifactFile;
    return SubmissionKind::Unknown;
}

static QString studentIdFromPath( const QString &path, const QString &root )
{
    const QFileInfo fi( path );
    const QDir rootDir( root );
    const QString rel = rootDir.relativeFilePath( path );
    const QStringList parts = rel.split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    if ( parts.size() >= 2 )
        return parts.first().normalized( QString::NormalizationForm_C );
    QString base = fi.completeBaseName();
    // strip common suffixes
    for ( const char *suf : { "_submission", "_artifact", "_report" } )
    {
        if ( base.endsWith( QLatin1String( suf ) ) )
            base.chop( static_cast<int>( strlen( suf ) ) );
    }
    return base.normalized( QString::NormalizationForm_C );
}

QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir )
{
    return discoverSubmissions( submissionsDir, nullptr );
}

namespace {
/// Every entry of @p dir except "." and "..", name-sorted — with NO QDir
/// type filter applied. Type-filtered listings (QDir::Files etc.) follow
/// symlinks, hide dangling links, and their type-bits semantics shift
/// across platforms; discovery must SEE every entry and classify it itself
/// (R14-E audit F1).
QFileInfoList allEntries( const QDir &dir )
{
    // Empirically pinned on Qt 6.12 (probe in .planning/r14-E): this exact
    // combination is the ONLY listing shape that returns live links, DANGLING
    // links, specials and hidden entries alike. QDir::NoFilter falls back to
    // the engine default (AllEntries), which silently drops dangling links
    // and linked dirs; Files/Dirs-only listings follow or drop links.
    return dir.entryInfoList(
      QDir::Dirs | QDir::Files | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
      QDir::Name );
}
} // namespace

namespace {
/// Null-safe refusal bookkeeping: the skip set itself is unchanged — only
/// its visibility changes (#1466).
void recordSkip( QVector<DiscoverySkip> *skipped, const QString &studentId,
                 const QString &path, const QString &reason )
{
    if ( !skipped )
        return;
    DiscoverySkip skip;
    skip.studentId = studentId;
    skip.path = path;
    skip.reason = reason;
    skipped->append( skip );
}

/// First reserved substring the name trips on, for a specific refusal
/// reason instead of a generic one.
QString reservedNameReason( const QString &name )
{
    static const QList<QPair<const char *, const char *>> kReserved = {
        { "batch", "reserved output name: contains 'batch'" },
        { "summary", "reserved output name: contains 'summary'" },
        { "feedback", "reserved output name: contains 'feedback'" },
        { "grades", "reserved output name: contains 'grades'" },
    };
    for ( const auto &[needle, reason] : kReserved )
    {
        if ( name.contains( QLatin1String( needle ) ) )
            return QString::fromLatin1( reason );
    }
    return QStringLiteral( "reserved output name" );
}

/// Containment refusal for one enumerated entry inside a discovery root
/// (R14-E audit F1): Qt's QDir::Files/Dirs filters follow symlinks, so a
/// linked student folder or file would grade content from outside the
/// submissions root, and a DANGLING link matches neither filter and used to
/// vanish without a row. Links (live or dangling) and non-regular specials
/// are typed refusals. Hidden-ness stays the CALLING loop's concern: the
/// unfiltered enumeration sees hidden entries too, and the old QDir::Files
/// / QDir::Dirs filters never listed them — the loops preserve that.
bool entryRefused( const QFileInfo &fi, QString *reason )
{
    if ( fi.isSymLink() )
    {
        *reason = QStringLiteral(
          "symlinked entry refused: discovery never follows links out of the submissions root" );
        return true;
    }
    if ( !fi.isFile() && !fi.isDir() )
    {
        *reason = QStringLiteral( "non-regular entry refused (device/fifo/socket)" );
        return true;
    }
    return false;
}
} // namespace

QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir,
                                             QVector<DiscoverySkip> *skipped )
{
    QVector<SubmissionItem> items;
    QDir root( submissionsDir );
    if ( !root.exists() )
        return items;

    // One-level student directories. Enumerated with NO filter so links and
    // special entries are SEEN before any stat-based filter can follow or
    // drop them ("."/".." are skipped by name — a type-filtered listing
    // hides dangling links and type-bits change semantics across Qt
    // versions).
    const auto subdirs = allEntries( root );
    for ( const QFileInfo &subInfo : subdirs )
    {
        // NFC (R14-E audit F4): macOS/zip-tool extractors can decompose
        // combining marks, so 张三(NFC) and 张三(NFD) would otherwise grade
        // as two students and break count reconciliation. One canonical
        // form for rows, skips and checkpoint keys alike.
        const QString sub =
            subInfo.fileName().normalized( QString::NormalizationForm_C );
        QString reason;
        if ( entryRefused( subInfo, &reason ) )
        {
            recordSkip( skipped, sub, subInfo.absoluteFilePath(), reason );
            continue;
        }
        // Root-level plain files belong to the flat pass below; hidden
        // entries keep the old invisibility (QDir::Files/Dirs never
        // listed them and the unfiltered enumeration here does).
        if ( !subInfo.isDir() || subInfo.isHidden() )
            continue;
        QDir sd( subInfo.absoluteFilePath() );
        const auto files = allEntries( sd );
        for ( const QFileInfo &fi : files )
        {
            const QString name = fi.fileName();
            QString entryReason;
            if ( entryRefused( fi, &entryReason ) )
            {
                recordSkip( skipped, sub, fi.absoluteFilePath(), entryReason );
                continue;
            }
            if ( !fi.isFile() || fi.isHidden() )
                continue; // hidden entries and nested dirs stay invisible
            if ( name.startsWith( QLatin1String( "grades" ) )
                 || name.contains( QLatin1String( "batch" ) )
                 || name.contains( QLatin1String( "summary" ) )
                 || name.contains( QLatin1String( "feedback" ) ) )
            {
                // Teacher-side output sitting in a student folder (e.g. the
                // console's feedback file for that student): not a
                // submission, but the refusal is attributable and visible.
                recordSkip( skipped, sub, fi.absoluteFilePath(),
                            name.startsWith( QLatin1String( "grades" ) )
                              ? QStringLiteral( "reserved output name: 'grades' prefix" )
                              : reservedNameReason( name ) );
                continue;
            }
            SubmissionItem it;
            it.studentId = sub;
            it.path = fi.absoluteFilePath();
            it.kind = classifySubmission( it.path );
            it.bytes = fi.size();
            items.push_back( it );
        }
    }

    // Flat files in root. Links and specials at the root were already
    // refused — and attributed to their name — by the student-directory
    // pass above; only plain, visible files belong to this pass.
    const auto flat = allEntries( root );
    for ( const QFileInfo &fi : flat )
    {
        if ( fi.isSymLink() || !fi.isFile() || fi.isHidden() )
            continue;
        if ( fi.fileName().startsWith( QLatin1String( "grades" ) ) )
        {
            // The console's own gradebook beside the submissions.
            recordSkip( skipped, QString(), fi.absoluteFilePath(),
                        QStringLiteral( "reserved output name: 'grades' prefix" ) );
            continue;
        }
        if ( fi.suffix() == QLatin1String( "csv" ) || fi.suffix() == QLatin1String( "json" ) )
        {
            // skip summary outputs sitting beside submissions
            if ( fi.fileName().contains( QLatin1String( "batch" ) )
                 || fi.fileName().contains( QLatin1String( "summary" ) )
                 || fi.fileName().contains( QLatin1String( "grades" ) ) )
            {
                recordSkip( skipped, QString(), fi.absoluteFilePath(),
                            reservedNameReason( fi.fileName() ) );
                continue;
            }
        }
        SubmissionItem it;
        it.path = fi.absoluteFilePath();
        it.studentId = studentIdFromPath( it.path, submissionsDir );
        it.kind = classifySubmission( it.path );
        it.bytes = fi.size();
        items.push_back( it );
    }
    std::sort( items.begin(), items.end(),
               []( const SubmissionItem &a, const SubmissionItem &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.path < b.path;
               } );
    if ( skipped )
    {
        std::sort( skipped->begin(), skipped->end(),
                   []( const DiscoverySkip &a, const DiscoverySkip &b ) {
                       if ( a.studentId != b.studentId )
                           return a.studentId < b.studentId;
                       return a.path < b.path;
                   } );
    }
    return items;
}

BatchAssessmentReport runBatchAssessment( const BatchAssessmentConfig &cfg, const GradeCallable &grade,
                                          std::atomic<bool> *cancelFlag, const BatchProgressFn &progress )
{
    BatchAssessmentReport report;
    report.labId = cfg.labId;
    report.rubricVersion = cfg.rubricVersion;
    report.labVersion = cfg.labVersion;
    report.softwareVersion = cfg.softwareVersion;

    auto items = discoverSubmissions( cfg.submissionsDir, &report.skippedFiles );
    const int discovered = items.size();
    report.total = discovered;
    // maxSubmissions >= 0 caps the processed count (0 = dry-run: nothing
    // graded, everything typed truncated); negative means unlimited.
    const int processLimit =
      cfg.maxSubmissions >= 0 ? std::min( discovered, cfg.maxSubmissions ) : discovered;
    report.truncated = discovered - processLimit;

    // Slot per submission, written by exactly one worker / adoption; final
    // classification happens below in discovery order, so the report is
    // byte-identical regardless of worker completion order.
    std::vector<BatchRowResult> rowSlot( static_cast<std::size_t>( processLimit ) );
    // char, NOT vector<bool>: workers write distinct flags concurrently, and
    // vector<bool> packs several flags into one word (lost updates).
    std::vector<char> filled( static_cast<std::size_t>( processLimit ), 0 );
    // Artifact identity per slot, published under the checkpoint mutex with
    // its row so the durable checkpoint always carries both (#1448).
    std::vector<QString> stampSlot( static_cast<std::size_t>( processLimit ) );

    // Restart: adopt durable rows from a matching checkpoint. Only rows that
    // went through the callable are checkpointed, and adoption requires the
    // same (student, artifact) pair — interrupted or cancelled items are
    // re-graded, never disguised as finished.
    if ( !cfg.checkpointPath.isEmpty() )
    {
        const QVector<DurableRow> durable =
          loadCheckpointRows( cfg.checkpointPath, batchConfigDigest( cfg ) );
        QHash<QString, DurableRow> byKey;
        for ( const auto &entry : durable )
            byKey.insert( entry.row.studentId + QLatin1Char( '\n' ) + entry.row.artifactPath,
                          entry );
        for ( int i = 0; i < processLimit; ++i )
        {
            const QString key = items[i].studentId + QLatin1Char( '\n' ) + items[i].path;
            const auto it = byKey.constFind( key );
            if ( it == byKey.constEnd() )
                continue;
            // Adoption also requires the artifact to still be what was
            // graded: a re-submission after the checkpoint re-grades instead
            // of publishing the stale row. No recorded stamp (older
            // checkpoint) or a missing artifact fails closed to re-grading
            // (#1448).
            const QString stamp = artifactStamp( items[i].path );
            if ( stamp.isEmpty() || stamp != it.value().artifactStamp )
                continue;
            BatchRowResult adopted = it.value().row;
            // R18-E D3: adopted rows carry the in-memory resumed flag (the
            // report-level resumed counter is the published visibility);
            // the row document itself stays byte-stable so a resumed run
            // publishes byte-identical output to an uninterrupted one
            // (restart-identity oracle).
            adopted.resumed = true;
            rowSlot[static_cast<std::size_t>( i )] = adopted;
            stampSlot[static_cast<std::size_t>( i )] = stamp;
            filled[static_cast<std::size_t>( i )] = true;
            report.resumed += 1;
            byKey.erase( it );
        }
    }

    // Bounded worker pool: workers pull the next index atomically, observe
    // cancellation before starting an item, and isolate callable exceptions.
    const int workers = std::clamp( cfg.maxConcurrency, 1, kMaxBatchWorkers );
    std::atomic<int> nextIndex{ 0 };
    std::atomic<int> doneRows{ 0 };
    QMutex checkpointMutex;
    auto checkpointAll = [&]() {
        // Rewrite from the slots (index-ordered) under the lock: durable
        // partial results, never a torn or reordered document.
        QMutexLocker locker( &checkpointMutex );
        QVector<DurableRow> durable;
        for ( int i = 0; i < processLimit; ++i )
            if ( filled[static_cast<std::size_t>( i )] )
                durable.append( DurableRow{ rowSlot[static_cast<std::size_t>( i )],
                                            stampSlot[static_cast<std::size_t>( i )] } );
        saveCheckpointAtomic( cfg.checkpointPath, batchConfigDigest( cfg ), discovered, durable );
    };
    auto worker = [&]() {
        for ( ;; )
        {
            const int i = nextIndex.fetch_add( 1 );
            if ( i >= processLimit )
                return;
            if ( cancelFlag && cancelFlag->load() )
                return; // slot stays empty → typed cancelled row below
            if ( filled[static_cast<std::size_t>( i )] )
                continue; // adopted from the checkpoint — do not re-grade
            int newlyDone = 0;
            BatchRowResult row;
            // Artifact identity BEFORE the callable (R18-E D4): the stamp
            // used to be taken only after grading, so an artifact swapped
            // DURING the call bound the old verdict to the new bytes and a
            // restart adopted the stale grade (#1448/#1525's class,
            // reopened through a race window). A changed stamp demotes the
            // row to typed unavailable and checkpoints an EMPTY stamp, so
            // resume re-grades instead of adopting.
            const QString preStamp = artifactStamp( items[i].path );
            try
            {
                row = grade( items[i] );
            }
            catch ( ... )
            {
                row.studentId = items[i].studentId;
                row.labId = cfg.labId;
                row.status = QStringLiteral( "error" );
                row.verdict = QStringLiteral( "error" );
                row.message = QStringLiteral( "grader threw; isolated" );
                row.artifactPath = items[i].path;
            }
            QString stamp = artifactStamp( items[i].path );
            // Both directions (round-1 review P2-1): an artifact that
            // APPEARED during the call binds a stamp over bytes the grade
            // never saw, exactly like a swap; "" == "" (absent throughout)
            // is the only non-demotion.
            if ( stamp != preStamp )
            {
                row.studentId = items[i].studentId;
                row.labId = cfg.labId;
                row.status = QStringLiteral( "unavailable" );
                row.verdict = QStringLiteral( "unavailable" );
                row.message = QStringLiteral(
                  "artifact changed during grading — not graded; restart re-grades" );
                row.artifactPath = items[i].path;
                if ( row.rubricVersion.isEmpty() )
                    row.rubricVersion = cfg.rubricVersion;
                if ( row.labVersion.isEmpty() )
                    row.labVersion = cfg.labVersion;
                if ( row.softwareVersion.isEmpty() )
                    row.softwareVersion = cfg.softwareVersion;
                row.missingEvidence = false;
                // No grade payload survives a demotion: score/digest/
                // deduction describe bytes nobody graded (R2 review P3-2).
                row.score = -1.0;
                row.graderDigest.clear();
                row.topDeduction.clear();
                row.unavailableReason =
                  QStringLiteral( "artifact changed during grading" );
                stamp.clear(); // empty stamps are never adopted (#1448)
            }
            // Identity authority (R16-C/T06 WP4): discovery owns the
            // (student, artifact) pair. A callable answering with a
            // foreign studentId used to be published verbatim — a
            // callable bug then attributed one student's score to another
            // id in the report/CSV. Overwrite, never trust: the grade
            // itself stays the callable's. Exception (round-1 review):
            // a discovered id that is EMPTY (flat root file named exactly
            // "_submission.*") must not blank the row — the callable's id
            // is then the only attribution available.
            if ( !items[i].studentId.isEmpty() )
                row.studentId = items[i].studentId;
            if ( row.labId.isEmpty() )
                row.labId = cfg.labId;
            if ( row.rubricVersion.isEmpty() )
                row.rubricVersion = cfg.rubricVersion;
            if ( row.labVersion.isEmpty() )
                row.labVersion = cfg.labVersion;
            if ( row.softwareVersion.isEmpty() )
                row.softwareVersion = cfg.softwareVersion;
            if ( row.artifactPath.isEmpty() )
                row.artifactPath = items[i].path;
            {
                // Publish under the checkpoint mutex: checkpointAll() reads
                // ALL slots while holding it, so the writers must take it
                // too for the release/acquire edge (a plain write races
                // with a sibling worker's snapshot and can tear a QString).
                QMutexLocker locker( &checkpointMutex );
                rowSlot[static_cast<std::size_t>( i )] = row;
                stampSlot[static_cast<std::size_t>( i )] = stamp;
                filled[static_cast<std::size_t>( i )] = true;
                newlyDone = doneRows.fetch_add( 1 ) + 1;
            }
            if ( progress )
                progress( newlyDone, processLimit );
            const bool finalRow = newlyDone == processLimit;
            // Throttled durable checkpoint: a full rewrite after every row
            // is O(n^2) bytes on large classes; every 16th row (plus the
            // final row and the post-loop write) bounds the loss of a hard
            // crash to 15 rows while staying linear overall.
            if ( !cfg.checkpointPath.isEmpty() && ( finalRow || newlyDone % 16 == 0 ) )
                checkpointAll();
        }
    };
    {
        std::vector<std::thread> pool;
        pool.reserve( static_cast<std::size_t>( workers ) );
        for ( int w = 0; w < workers; ++w )
            pool.emplace_back( worker );
        for ( auto &t : pool )
            t.join();
    }

    // Assemble + classify in discovery order: completed work is kept,
    // never-started items become typed cancelled rows (or, without a cancel
    // flag, a defensive isolated error — a missing row is a bug, not a zero).
    for ( int i = 0; i < processLimit; ++i )
    {
        BatchRowResult row;
        if ( filled[static_cast<std::size_t>( i )] )
        {
            row = rowSlot[static_cast<std::size_t>( i )];
        }
        else if ( cancelFlag && cancelFlag->load() )
        {
            row.studentId = items[i].studentId;
            row.labId = cfg.labId;
            row.status = QStringLiteral( "cancelled" );
            row.verdict = QStringLiteral( "cancelled" );
            row.message = QStringLiteral( "batch cancelled" );
            row.artifactPath = items[i].path;
            row.rubricVersion = cfg.rubricVersion;
            row.labVersion = cfg.labVersion;
            row.softwareVersion = cfg.softwareVersion;
        }
        else
        {
            row.studentId = items[i].studentId;
            row.labId = cfg.labId;
            row.status = QStringLiteral( "error" );
            row.verdict = QStringLiteral( "error" );
            row.message = QStringLiteral( "grader produced no row (bug); isolated" );
            row.artifactPath = items[i].path;
            row.rubricVersion = cfg.rubricVersion;
            row.labVersion = cfg.labVersion;
            row.softwareVersion = cfg.softwareVersion;
        }

        // Missing evidence must never look like a silent zero. The rewrite
        // runs BEFORE classification so the counters describe the published
        // rows: a row demoted to "unavailable" must not be counted as
        // graded (#1448).
        if ( row.missingEvidence && row.score == 0.0 && row.verdict == QLatin1String( "fail" ) )
        {
            row.verdict = QStringLiteral( "unavailable" );
            row.status = QStringLiteral( "unavailable" );
            if ( row.message.isEmpty() )
                row.message = QStringLiteral( "missing evidence — not a silent zero" );
        }

        if ( row.status == QLatin1String( "corrupted" ) )
            report.corrupted += 1;
        else if ( row.status == QLatin1String( "pass" ) || row.status == QLatin1String( "fail" ) )
            report.graded += 1;
        else if ( row.status == QLatin1String( "cancelled" ) )
        {
            report.cancelled += 1;
            report.cancelledEarly = true;
        }
        else if ( row.status == QLatin1String( "unavailable" ) )
        {
            // grader-side unavailability; counted in the unavailable pass below
        }
        else
            report.failed += 1;

        if ( row.missingEvidence )
            report.missingEvidence += 1;

        report.rows.push_back( row );
    }

    // Grader-side unavailability (CLI missing/timeout, unverifiable artifact),
    // counted from final rows: a class that could not be graded is not a
    // silent success. Missing-evidence rewrites above keep their own counter.
    for ( const auto &row : report.rows )
    {
        if ( row.status == QLatin1String( "unavailable" ) && !row.missingEvidence )
            report.unavailable += 1;
    }

    // Final durable checkpoint: adopted + newly completed rows, so an
    // interrupted RESTART of the restart still resumes instead of redoing.
    if ( !cfg.checkpointPath.isEmpty() )
        checkpointAll();

    return report;
}

bool publishBatchOutputsAtomic( const BatchAssessmentReport &report, const QString &outPrefix )
{
    const QByteArray jsonBytes = QJsonDocument( report.toJson() ).toJson( QJsonDocument::Indented );
    const QString jsonPath = outPrefix + QStringLiteral( ".json" );
    const QString csvPath = outPrefix + QStringLiteral( ".csv" );

    if ( !writeBytesAtomically( jsonPath, jsonBytes ) )
        return false;

    QString csv;
    csv += QStringLiteral( "student_id,lab_id,score,verdict,status,message,missing_evidence,grader_digest,top_deduction,unavailable_reason,rubric_version,lab_version,software_version\r\n" );
    QVector<BatchRowResult> sorted = report.rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const BatchRowResult &a, const BatchRowResult &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.artifactPath < b.artifactPath;
               } );
    // Twin of csv_safe() in scripts/run_classroom_batch.py and
    // csvSafeCell() in src/cli/lab_batch_runner.cpp: a leading =+-@TABCR
    // gets an apostrophe prefix so spreadsheets open the cell as text.
    auto esc = []( QString s ) {
        s.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
        if ( !s.isEmpty() )
        {
            switch ( s.at( 0 ).unicode() )
            {
                case '=':
                case '+':
                case '-':
                case '@':
                case '\t':
                case '\r':
                    s.prepend( QLatin1Char( '\'' ) );
                    break;
                default:
                    break;
            }
        }
        return QStringLiteral( "\"" ) + s + QStringLiteral( "\"" );
    };
    for ( const auto &row : sorted )
    {
        csv += esc( row.studentId ) + QLatin1Char( ',' );
        csv += esc( row.labId ) + QLatin1Char( ',' );
        csv += ( row.score < 0 ? QString() : QString::number( row.score ) ) + QLatin1Char( ',' );
        csv += esc( row.verdict ) + QLatin1Char( ',' );
        csv += esc( row.status ) + QLatin1Char( ',' );
        csv += esc( row.message ) + QLatin1Char( ',' );
        csv += ( row.missingEvidence ? QStringLiteral( "1" ) : QStringLiteral( "0" ) ) + QLatin1Char( ',' );
        csv += esc( row.graderDigest ) + QLatin1Char( ',' );
        csv += esc( row.topDeduction ) + QLatin1Char( ',' );
        csv += esc( row.unavailableReason ) + QLatin1Char( ',' );
        csv += esc( row.rubricVersion ) + QLatin1Char( ',' );
        csv += esc( row.labVersion ) + QLatin1Char( ',' );
        csv += esc( row.softwareVersion ) + QStringLiteral( "\r\n" );
    }
    // Discovery refusals as typed rows (status="skipped"): the gradebook
    // shows every student the console saw, including those whose ONLY file
    // was refused — an explicit refusal instead of an invisible student
    // (#1466). Not BatchRowResults: they never enter counters or the
    // checkpoint, and the message carries the reason plus the file name so
    // the row is self-contained without the JSON.
    for ( const auto &skip : report.skippedFiles )
    {
        csv += esc( skip.studentId ) + QLatin1Char( ',' );   // student_id
        csv += esc( report.labId ) + QLatin1Char( ',' );     // lab_id
        csv += QLatin1Char( ',' );                           // score: none
        csv += QLatin1Char( ',' );                           // verdict: none
        csv += QStringLiteral( "\"skipped\"," );             // status
        csv += esc( QStringLiteral( "%1 (%2)" )
                      .arg( skip.reason, QFileInfo( skip.path ).fileName() ) )
             + QLatin1Char( ',' );                           // message
        csv += QStringLiteral( "0," );                       // missing_evidence
        csv += QLatin1Char( ',' );                           // grader_digest
        csv += QLatin1Char( ',' );                           // top_deduction
        csv += QLatin1Char( ',' );                           // unavailable_reason
        csv += esc( report.rubricVersion ) + QLatin1Char( ',' );
        csv += esc( report.labVersion ) + QLatin1Char( ',' );
        csv += esc( report.softwareVersion ) + QStringLiteral( "\r\n" );
    }

    // Real UTF-8 BOM as BYTES: a QStringLiteral "ï»¿" would hold
    // code points U+00EF/U+00BB/U+00BF and toUtf8() would double-encode
    // them (C3 AF ...) — a BOM no spreadsheet recognizes.
    if ( !writeBytesAtomically( csvPath, QByteArrayLiteral( "\xEF\xBB\xBF" ) + csv.toUtf8() ) )
        return false;
    return true;
}

QJsonObject regradeTraceability( const BatchAssessmentReport &report )
{
    // Regrade identity covers the grading OUTCOME: rows plus the grading
    // counters. Run-metadata fields (resumed / truncated / cancelled_early)
    // describe HOW this run came to be, not what was graded — a resumed
    // restart and a fresh uninterrupted run of the same class share the
    // digest.
    QJsonObject outcome = report.toJson();
    outcome.remove( QStringLiteral( "resumed" ) );
    outcome.remove( QStringLiteral( "truncated" ) );
    outcome.remove( QStringLiteral( "cancelled_early" ) );
    return QJsonObject{
        { QStringLiteral( "lab_id" ), report.labId },
        { QStringLiteral( "rubric_version" ), report.rubricVersion },
        { QStringLiteral( "lab_version" ), report.labVersion },
        { QStringLiteral( "software_version" ), report.softwareVersion },
        { QStringLiteral( "report_digest" ), sha256Hex( canonicalJsonBytes( outcome ) ) },
        { QStringLiteral( "row_count" ), report.rows.size() },
    };
}

} // namespace sicnu::teaching_admin
