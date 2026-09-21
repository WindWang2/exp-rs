// study_execution_plane.cpp — ExecutionPlane adapter implementation.
#include "study/study_execution_plane.h"

#include "framework/execution_plane.h"
#include "framework/task_center.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <chrono>

#include <json/json.h>

namespace sicnu::study
{
namespace
{

Diagnostic bridgeError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

QString jsonText( const Json::Value &value )
{
    return value.isString() ? QString::fromStdString( value.asString() ) : QString();
}

/// One submitted point on the execution spine.
class ExecutionPlaneSubmission : public StudySubmission
{
  public:
    ExecutionPlaneSubmission( long taskId, QString outputTarget )
        : m_taskId( taskId )
        , m_outputTarget( std::move( outputTarget ) )
    {
    }

    QString executionRef() const override { return QString::number( m_taskId ); }

    void cancel() override
    {
        m_cancelRequested.store( true );
        sicnu::TaskCenter::instance().cancelTask( m_taskId, sicnu::TaskCancelReason::User );
    }

    StudyExecutionOutcome wait( std::chrono::milliseconds timeout ) override
    {
        auto &plane = processing::ExecutionPlane::instance();
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        const Json::Value payload =
            plane.awaitResult( m_taskId, timeout, m_committer, nullptr, true );

        StudyExecutionOutcome outcome;
        const QString status = jsonText( payload.get( "status", Json::Value() ) );
        if ( status == QStringLiteral( "success" ) )
        {
            outcome.status = StudyExecutionOutcome::Status::Succeeded;
            outcome.payload = qjsonFromPayload( payload );
            return outcome;
        }

        // Terminal failure family — classify truthfully via the task state.
        const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo( m_taskId );
        const QString rawMessage = jsonText( payload.get( "errorMessage", Json::Value() ) );
        const QString message = rawMessage.isEmpty() ? info.errorMessage : rawMessage;
        if ( ( info.taskId == m_taskId && info.status == sicnu::TaskStatus::Canceled )
             || m_cancelRequested.load() )
        {
            if ( m_cancelRequested.load() )
            {
                // The study (or its caller) asked for this cancel.
                outcome.status = StudyExecutionOutcome::Status::Cancelled;
                outcome.errorCode = QStringLiteral( "study.run_cancelled" );
                outcome.errorMessage = message;
                return outcome;
            }
            // Someone ELSE cancelled the task. The per-run deadline does that
            // (awaitResult cancelOnTimeout), but so can shutdown or another
            // surface — distinguish by whether the deadline window actually
            // elapsed. Guessing "timeout" here would write a false reason
            // into store truth.
            const bool deadlineExpired =
                std::chrono::steady_clock::now() - started >= timeout;
            outcome.status = deadlineExpired ? StudyExecutionOutcome::Status::TimedOut
                                             : StudyExecutionOutcome::Status::Cancelled;
            outcome.errorCode = deadlineExpired ? QStringLiteral( "study.run_timeout" )
                                                : QStringLiteral( "study.run_cancelled" );
            outcome.errorMessage =
                deadlineExpired
                    ? message
                    : ( message.isEmpty() ? QStringLiteral( "cancelled outside the study" )
                                          : message );
            return outcome;
        }
        outcome.status = StudyExecutionOutcome::Status::Failed;
        outcome.errorCode = QStringLiteral( "study.operator_failed" );
        outcome.errorMessage = message;
        return outcome;
    }

  private:
    /// Commit policy: temp → stable, atomically, no catalog asset.
    bool commit( const sicnu::AlgorithmTaskInfo &info, std::string &outCommittedPath,
                 std::string &outCommitError, std::string & )
    {
        const QString temporary = info.outputLayerPath;
        if ( temporary.isEmpty() )
        {
            outCommitError = "study.commit_no_output";
            return false;
        }
        if ( QFileInfo( temporary ).absoluteFilePath() == QFileInfo( m_outputTarget ).absoluteFilePath() )
        {
            outCommittedPath = m_outputTarget.toStdString();
            return true;
        }
        const QFileInfo targetInfo( m_outputTarget );
        if ( !QDir().mkpath( targetInfo.absolutePath() ) )
        {
            outCommitError = "study.commit_mkdir_failed";
            return false;
        }
        // Stage the copy INSIDE the target directory, then rename within the
        // same directory (atomic on POSIX — a previous output at the target
        // survives a failed rename; the Windows fallback retries once after
        // removing the target, accepting the narrow tear window there).
        const QString staged = targetInfo.absoluteFilePath() + QStringLiteral( ".study-staging" );
        QFile::remove( staged );
        if ( !QFile::copy( temporary, staged ) )
        {
            outCommitError = "study.commit_copy_failed";
            return false;
        }
        if ( !QFile::rename( staged, m_outputTarget ) )
        {
#ifdef Q_OS_WIN
            QFile::remove( m_outputTarget );
            if ( QFile::rename( staged, m_outputTarget ) )
            {
                QFile::remove( temporary );
                outCommittedPath = m_outputTarget.toStdString();
                return true;
            }
#endif
            QFile::remove( staged );
            outCommitError = "study.commit_rename_failed";
            return false;
        }
        QFile::remove( temporary ); // best effort — the temp belongs to the work dir
        outCommittedPath = m_outputTarget.toStdString();
        return true;
    }

    static QJsonObject qjsonFromPayload( const Json::Value &payload )
    {
        // The payload's interesting fields for the study layer are scalars;
        // Qt-side conversion goes through the JSON text (bounded, trusted —
        // it was produced by the in-process spine).
        QJsonDocument document =
            QJsonDocument::fromJson( QString::fromStdString(
                                         payload.toStyledString() )
                                         .toUtf8() );
        return document.object();
    }

    long m_taskId;
    QString m_outputTarget;
    std::atomic<bool> m_cancelRequested{ false };
    processing::ExecutionPlane::OutputCommitterHandler m_committer =
        [ this ]( const sicnu::AlgorithmTaskInfo &info, std::string &outCommittedPath,
                  std::string &outCommitError, std::string &outAssetId ) {
            return commit( info, outCommittedPath, outCommitError, outAssetId );
        };
};

} // namespace

Result<std::unique_ptr<StudySubmission>> ExecutionPlaneStudyBackend::submit(
    const QString &algorithmId, const QJsonObject &pointParameters,
    const QString &correlationId, std::chrono::milliseconds timeout )
{
    processing::ExecutionRequest request;
    request.algorithmId = algorithmId;
    request.params = pointParameters.toVariantMap();
    request.source = QStringLiteral( "study" ); // Background lane by the source mapping
    request.correlationId = correlationId;
    request.timeout = timeout;
    request.cancelOnTimeout = true;

    processing::ExecutionPlane &plane = processing::ExecutionPlane::instance();
    const processing::ExecutionHandle handle = plane.submit( request );
    if ( !handle.valid() || handle.taskId() < 0 )
        return Result<std::unique_ptr<StudySubmission>>::failure( bridgeError(
            QStringLiteral( "study.run_submit_refused" ),
            QStringLiteral( "the execution plane refused the study point submission" ) ) );

    const QString outputTarget =
        pointParameters.value( QStringLiteral( "output" ) ).toString();
    return Result<std::unique_ptr<StudySubmission>>::success(
        std::make_unique<ExecutionPlaneSubmission>( handle.taskId(), outputTarget ) );
}

} // namespace sicnu::study
