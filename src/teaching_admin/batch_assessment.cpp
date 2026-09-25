#include "batch_assessment.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QSaveFile>
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

/// Atomically rewrite the checkpoint (temp + rename) from @p rows — always
/// the full, index-ordered set, so the file is never a partial document.
bool saveCheckpointAtomic( const QString &path, const QString &configDigest, int total,
                           const QVector<BatchRowResult> &rows )
{
    QJsonArray rowsArr;
    for ( const auto &row : rows )
        rowsArr.append( row.toJson() );
    const QJsonObject doc = sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), kBatchCheckpointSchema },
        { QStringLiteral( "config_digest" ), configDigest },
        { QStringLiteral( "total" ), total },
        { QStringLiteral( "rows" ), rowsArr },
    } );
    const QByteArray bytes = QJsonDocument( doc ).toJson( QJsonDocument::Indented );
    QSaveFile sf( path );
    if ( !sf.open( QIODevice::WriteOnly ) )
        return false;
    if ( sf.write( bytes ) != bytes.size() )
        return false;
    return sf.commit();
}

/// Loads checkpoint rows whose config digest matches the current run. A
/// missing/corrupt/foreign checkpoint degrades to "no adopted rows" (fresh
/// start) — best-effort durability, never a wrong grade.
QVector<BatchRowResult> loadCheckpointRows( const QString &path, const QString &configDigest )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return {};
    const QJsonObject doc = QJsonDocument::fromJson( f.readAll() ).object();
    if ( doc.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( kBatchCheckpointSchema ) )
        return {};
    if ( doc.value( QStringLiteral( "config_digest" ) ).toString() != configDigest )
        return {};
    QVector<BatchRowResult> rows;
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
        BatchRowResult row = BatchRowResult::fromJson( v.toObject() );
        if ( !kKnownStatuses.contains( row.status ) || row.studentId.isEmpty() )
            continue;
        rows.append( row );
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
        return parts.first();
    QString base = fi.completeBaseName();
    // strip common suffixes
    for ( const char *suf : { "_submission", "_artifact", "_report" } )
    {
        if ( base.endsWith( QLatin1String( suf ) ) )
            base.chop( static_cast<int>( strlen( suf ) ) );
    }
    return base;
}

QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir )
{
    QVector<SubmissionItem> items;
    QDir root( submissionsDir );
    if ( !root.exists() )
        return items;

    // One-level student directories
    const auto subdirs = root.entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );
    for ( const QString &sub : subdirs )
    {
        QDir sd( root.filePath( sub ) );
        const auto files = sd.entryInfoList( QDir::Files, QDir::Name );
        for ( const QFileInfo &fi : files )
        {
            const QString name = fi.fileName();
            if ( name.startsWith( QLatin1String( "grades" ) )
                 || name.contains( QLatin1String( "batch" ) )
                 || name.contains( QLatin1String( "summary" ) )
                 || name.contains( QLatin1String( "feedback" ) ) )
                continue;
            SubmissionItem it;
            it.studentId = sub;
            it.path = fi.absoluteFilePath();
            it.kind = classifySubmission( it.path );
            it.bytes = fi.size();
            items.push_back( it );
        }
    }

    // Flat files in root
    const auto flat = root.entryInfoList( QDir::Files, QDir::Name );
    for ( const QFileInfo &fi : flat )
    {
        if ( fi.fileName().startsWith( QLatin1String( "grades" ) ) )
            continue;
        if ( fi.suffix() == QLatin1String( "csv" ) || fi.suffix() == QLatin1String( "json" ) )
        {
            // skip summary outputs sitting beside submissions
            if ( fi.fileName().contains( QLatin1String( "batch" ) )
                 || fi.fileName().contains( QLatin1String( "summary" ) )
                 || fi.fileName().contains( QLatin1String( "grades" ) ) )
                continue;
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

    auto items = discoverSubmissions( cfg.submissionsDir );
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

    // Restart: adopt durable rows from a matching checkpoint. Only rows that
    // went through the callable are checkpointed, and adoption requires the
    // same (student, artifact) pair — interrupted or cancelled items are
    // re-graded, never disguised as finished.
    if ( !cfg.checkpointPath.isEmpty() )
    {
        const QVector<BatchRowResult> durable =
          loadCheckpointRows( cfg.checkpointPath, batchConfigDigest( cfg ) );
        QHash<QString, BatchRowResult> byKey;
        for ( const auto &row : durable )
            byKey.insert( row.studentId + QLatin1Char( '\n' ) + row.artifactPath, row );
        for ( int i = 0; i < processLimit; ++i )
        {
            const QString key = items[i].studentId + QLatin1Char( '\n' ) + items[i].path;
            const auto it = byKey.constFind( key );
            if ( it != byKey.constEnd() )
            {
                rowSlot[static_cast<std::size_t>( i )] = it.value();
                filled[static_cast<std::size_t>( i )] = true;
                report.resumed += 1;
                byKey.erase( it );
            }
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
        QVector<BatchRowResult> durable;
        for ( int i = 0; i < processLimit; ++i )
            if ( filled[static_cast<std::size_t>( i )] )
                durable.append( rowSlot[static_cast<std::size_t>( i )] );
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
            if ( row.studentId.isEmpty() )
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

        // Missing evidence must never look like a silent zero
        if ( row.missingEvidence && row.score == 0.0 && row.verdict == QLatin1String( "fail" ) )
        {
            row.verdict = QStringLiteral( "unavailable" );
            row.status = QStringLiteral( "unavailable" );
            if ( row.message.isEmpty() )
                row.message = QStringLiteral( "missing evidence — not a silent zero" );
        }

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

    {
        QSaveFile sf( jsonPath );
        if ( !sf.open( QIODevice::WriteOnly ) )
            return false;
        if ( sf.write( jsonBytes ) != jsonBytes.size() )
            return false;
        if ( !sf.commit() )
            return false;
    }

    QString csv;
    csv += QStringLiteral( "\xEF\xBB\xBF" ); // UTF-8 BOM
    csv += QStringLiteral( "student_id,lab_id,score,verdict,status,message,missing_evidence,grader_digest,top_deduction,unavailable_reason,rubric_version,lab_version,software_version\r\n" );
    QVector<BatchRowResult> sorted = report.rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const BatchRowResult &a, const BatchRowResult &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.artifactPath < b.artifactPath;
               } );
    for ( const auto &row : sorted )
    {
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

    {
        QSaveFile sf( csvPath );
        if ( !sf.open( QIODevice::WriteOnly ) )
            return false;
        const QByteArray bytes = csv.toUtf8();
        if ( sf.write( bytes ) != bytes.size() )
            return false;
        if ( !sf.commit() )
            return false;
    }
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
