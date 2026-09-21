// test_study_runner.cpp — windowed runner contracts (RS14-07 Slice C).
//
// The fake backend is the oracle here: it records submission concurrency
// (the window must never exceed budget.maxInFlight), captures the exact
// parameters submitted (the runner owns "output"), and scripts terminal
// outcomes so every truthful-recording branch is exercised offline.
#include <catch2/catch_test_macros.hpp>

#include "study/study_runner.h"

#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <vector>

using namespace sicnu::study;

namespace
{

class FakeSubmission : public StudySubmission
{
  public:
    FakeSubmission( QString executionRef, StudyExecutionOutcome outcome,
                    std::shared_ptr<std::atomic<int>> inFlight )
        : m_executionRef( std::move( executionRef ) )
        , m_outcome( std::move( outcome ) )
        , m_inFlight( std::move( inFlight ) )
    {
    }

    ~FakeSubmission() override { ( *m_inFlight )--; }

    QString executionRef() const override { return m_executionRef; }

    StudyExecutionOutcome wait( std::chrono::milliseconds ) override
    {
        if ( m_cancelRequested )
        {
            StudyExecutionOutcome cancelled;
            cancelled.status = StudyExecutionOutcome::Status::Cancelled;
            return cancelled;
        }
        return m_outcome;
    }

    void cancel() override { m_cancelRequested = true; }

  private:
    QString m_executionRef;
    StudyExecutionOutcome m_outcome;
    std::shared_ptr<std::atomic<int>> m_inFlight;
    bool m_cancelRequested = false;
};

enum class Script
{
    Succeed,
    Fail,
    Timeout,
    SucceedWithoutOutput,
};

class FakeBackend : public IStudyExecutionBackend
{
  public:
    bool refuseAll = false;
    std::vector<Script> script; // one entry per submission, last repeats
    Script fallback = Script::Succeed;

    // Observation surface.
    int submissions = 0;
    QStringList algorithmIds;
    int maxObservedInFlight = 0;
    QVector<QJsonObject> submittedParameters;
    QStringList correlationIds;

    Result<std::unique_ptr<StudySubmission>> submit( const QString &algorithmId,
                                                    const QJsonObject &pointParameters,
                                                    const QString &correlationId,
                                                    std::chrono::milliseconds ) override
    {
        ++submissions;
        algorithmIds.append( algorithmId );
        correlationIds.append( correlationId );
        submittedParameters.append( pointParameters );
        if ( refuseAll )
            return Result<std::unique_ptr<StudySubmission>>::failure(
                runnerDiagnostic() );
        const int now = m_inFlight->fetch_add( 1 ) + 1;
        maxObservedInFlight = std::max( maxObservedInFlight, now );

        Script entry = script.size() > 0
            ? script.at( std::min<int>( submissions - 1, script.size() - 1 ) )
            : fallback;
        StudyExecutionOutcome outcome = outcomeFor( entry );
        return Result<std::unique_ptr<StudySubmission>>::success(
            std::make_unique<FakeSubmission>( QString::number( 1000 + submissions ),
                                              std::move( outcome ), m_inFlight ) );
    }

    int currentInFlight() const { return m_inFlight->load(); }

  private:
    static sicnu::data::Diagnostic runnerDiagnostic()
    {
        sicnu::data::Diagnostic d;
        d.code = QStringLiteral( "fake.refused" );
        d.message = QStringLiteral( "backend refused the submission" );
        d.severity = sicnu::data::DiagnosticSeverity::Error;
        return d;
    }

    static StudyExecutionOutcome outcomeFor( Script entry )
    {
        StudyExecutionOutcome outcome;
        switch ( entry )
        {
            case Script::Succeed:
            {
                outcome.status = StudyExecutionOutcome::Status::Succeeded;
                outcome.payload = QJsonObject{
                    { QStringLiteral( "output" ), QStringLiteral( "/committed/output.tif" ) },
                    { QStringLiteral( "maskedPercent" ), 42.5 },
                    { QStringLiteral( "maskedPixels" ), 425 },
                    { QStringLiteral( "taskId" ), QStringLiteral( "t" ) },
                };
                break;
            }
            case Script::Fail:
            {
                outcome.status = StudyExecutionOutcome::Status::Failed;
                outcome.errorCode = QStringLiteral( "study.operator_failed" );
                outcome.errorMessage = QStringLiteral( "operator exploded" );
                break;
            }
            case Script::Timeout:
            {
                outcome.status = StudyExecutionOutcome::Status::TimedOut;
                outcome.errorCode = QStringLiteral( "study.run_timeout" );
                break;
            }
            case Script::SucceedWithoutOutput:
            {
                outcome.status = StudyExecutionOutcome::Status::Succeeded;
                outcome.payload = QJsonObject{
                    { QStringLiteral( "maskedPercent" ), 1.0 },
                };
                break;
            }
        }
        return outcome;
    }

    std::shared_ptr<std::atomic<int>> m_inFlight = std::make_shared<std::atomic<int>>( 0 );
};

struct Fixture
{
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    sicnu::experiment::MatrixLedger ledger{ store };
    FakeBackend backend;

    Fixture()
    {
        REQUIRE( dir.isValid() );
        REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
    }

    QString outputDir() const { return dir.filePath( QStringLiteral( "study-outputs" ) ); }
};

ParameterStudySpec specFor( int steps = 3, int maxInFlight = 2, qint64 maxRuns = 100 )
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "runner-contract" );
    spec.experimentId = QStringLiteral( "exp-runner" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.baseParameters = QJsonObject{
        { QStringLiteral( "input" ), QStringLiteral( "/data/ndvi.tif" ) } };
    spec.strategy = SamplingStrategy::Grid;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.1;
    dim.maxValue = 0.9;
    dim.stepCount = steps;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = maxRuns;
    spec.budget.maxInFlight = maxInFlight;
    spec.budget.perRunTimeoutMs = 5000;
    spec.budget.seedReplicates = 1;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    return spec;
}

} // namespace

TEST_CASE( "study runner records every point truthfully (happy path)", "[study][runner]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( /*steps=*/3 );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.totalPoints == 3 );
    REQUIRE( summary.recordedCount == 3 );
    REQUIRE( summary.failedCount == 0 );
    REQUIRE( summary.cancelledCount == 0 );
    REQUIRE( summary.stoppedReason.isEmpty() );
    REQUIRE( summary.runIds.size() == 3 );

    // The experiment was auto-created.
    REQUIRE( fix.store.experimentById( spec.experimentId ).has_value() );

    // Each recorded run: Completed, committed output artifact, declared
    // metrics only (maskedPercent), point linked through the ledger.
    const auto points = sampleStudyPoints( spec ).value();
    for ( int i = 0; i < 3; ++i )
    {
        const auto run = fix.store.runById( summary.runIds.at( i ) );
        REQUIRE( run.has_value() );
        // Runner-assigned output path landed in the submitted parameters…
        const QString pointId = points.at( i ).pointId;
        const QString expected = fix.outputDir() + QStringLiteral( "/" ) + pointId
                                 + QStringLiteral( "/output.tif" );
        REQUIRE( fix.backend.submittedParameters.at( i )
                     .value( QStringLiteral( "output" ) )
                     .toString() == expected );
        // …and the recorded parameters carry the swept value.
        REQUIRE( run.value().parameters()
                     .value( QStringLiteral( "threshold" ) )
                     .toDouble() != 0.0 );
        REQUIRE( run.value().executionRef() == QString::number( 1001 + i ) );
        // Ledger linkage exists for the point.
        REQUIRE( !fix.ledger.runsForCell( pointId ).isEmpty() );
    }

    // Metric selection: only declared metrics recorded (payload had
    // maskedPixels and taskId too).
    const auto run = fix.store.runById( summary.runIds.at( 0 ) );
    REQUIRE( run.value().metrics().value( QStringLiteral( "maskedPercent" ) ) == 42.5 );
    REQUIRE( !run.value().metrics().contains( QStringLiteral( "maskedPixels" ) ) );
    REQUIRE( !run.value().metrics().contains( QStringLiteral( "taskId" ) ) );
}

TEST_CASE( "study runner never exceeds the in-flight window", "[study][runner][budget]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( /*steps=*/5, /*maxInFlight=*/2 );
    std::atomic<bool> cancel{ false };
    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    REQUIRE( fix.backend.maxObservedInFlight == 2 ); // pipelined, bounded
}

TEST_CASE( "failed points are recorded as failures with typed evidence", "[study][runner]" )
{
    Fixture fix;
    fix.backend.script = { Script::Succeed, Script::Fail, Script::Succeed };
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 3 );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.recordedCount == 2 );
    REQUIRE( summary.failedCount == 1 );
    REQUIRE( summary.runIds.size() == 3 ); // complete accounting

    const auto failed = fix.store.runById( summary.runIds.at( 1 ) );
    REQUIRE( failed.has_value() );
    const QJsonObject metrics = failed.value().metrics();
    const QJsonObject error = metrics.value( QStringLiteral( "error" ) ).toObject();
    REQUIRE( !error.isEmpty() );
    REQUIRE( error.value( QStringLiteral( "error_code" ) ).toString()
             == QStringLiteral( "study.operator_failed" ) );
    // The failed point is still linked in the ledger.
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( !fix.ledger.runsForCell( points.at( 1 ).pointId ).isEmpty() );
}

TEST_CASE( "submit refusals become failed runs with an empty execution ref",
           "[study][runner]" )
{
    Fixture fix;
    fix.backend.refuseAll = true;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 2 );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.failedCount == 2 );
    REQUIRE( summary.recordedCount == 0 );

    const auto run = fix.store.runById( summary.runIds.at( 0 ) );
    REQUIRE( run.has_value() );
    REQUIRE( run.value().executionRef().isEmpty() );
    const QJsonObject error = run.value().metrics()
                                  .value( QStringLiteral( "error" ) )
                                  .toObject();
    REQUIRE( error.value( QStringLiteral( "error_code" ) ).toString()
             == QStringLiteral( "study.run_submit_refused" ) );
}

TEST_CASE( "timed-out executions are recorded as deadline failures",
           "[study][runner][budget]" )
{
    Fixture fix;
    fix.backend.fallback = Script::Timeout;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 2 );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    REQUIRE( result.value().failedCount == 2 );
    const auto run = fix.store.runById( result.value().runIds.at( 0 ) );
    const QJsonObject error =
        run.value().metrics().value( QStringLiteral( "error" ) ).toObject();
    REQUIRE( error.value( QStringLiteral( "error_code" ) ).toString()
             == QStringLiteral( "study.run_timeout" ) );
}

TEST_CASE( "a success without a committed output is not a success", "[study][runner]" )
{
    Fixture fix;
    fix.backend.fallback = Script::SucceedWithoutOutput;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 2 );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    REQUIRE( result.value().recordedCount == 0 );
    REQUIRE( result.value().failedCount == 2 );
    const auto run = fix.store.runById( result.value().runIds.at( 0 ) );
    const QJsonObject error =
        run.value().metrics().value( QStringLiteral( "error" ) ).toObject();
    REQUIRE( error.value( QStringLiteral( "error_code" ) ).toString()
             == QStringLiteral( "study.run_missing_output" ) );
}

TEST_CASE( "caller cancellation stops submissions and records in-flight points as cancelled",
           "[study][runner][cancel]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 4, /*maxInFlight=*/2 );
    std::atomic<bool> cancel{ false };

    runner.setProgressCallback(
        [&]( const StudyProgress &progress ) {
            if ( progress.phase == StudyProgress::Phase::PointSubmitted )
                cancel.store( true ); // cancel while two points are in flight
        } );

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.stoppedReason == QStringLiteral( "cancelled" ) );
    REQUIRE( summary.cancelledCount >= 1 );
    // No further submissions after the cancel: at most 2 happened.
    REQUIRE( fix.backend.submissions <= 2 );
    // Every created run is terminal (no dangling non-terminal states).
    qint64 terminal = 0;
    for ( const QString &runId : summary.runIds )
    {
        const auto run = fix.store.runById( runId );
        REQUIRE( run.has_value() );
        const auto status = run.value().status();
        if ( status == sicnu::experiment::RunStatus::Completed
             || status == sicnu::experiment::RunStatus::Cancelled
             || status == sicnu::experiment::RunStatus::Failed )
            ++terminal;
    }
    REQUIRE( terminal == summary.runIds.size() );
}

TEST_CASE( "invalid specs and output dirs are typed refusals before any submission",
           "[study][runner]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    std::atomic<bool> cancel{ false };

    SECTION( "invalid spec" )
    {
        auto spec = specFor();
        spec.studyId.clear();
        const auto result = runner.run( spec, cancel, fix.outputDir() );
        REQUIRE( !result.has_value() );
        bool found = false;
        for ( const auto &d : result.diagnostics() )
            found = found || d.code == QStringLiteral( "study.spec_invalid_study_id" );
        REQUIRE( found );
        REQUIRE( fix.backend.submissions == 0 );
    }
    SECTION( "over-budget sampling" )
    {
        auto spec = specFor( /*steps=*/10, /*maxInFlight=*/2, /*maxRuns=*/5 );
        const auto result = runner.run( spec, cancel, fix.outputDir() );
        REQUIRE( !result.has_value() );
        bool found = false;
        for ( const auto &d : result.diagnostics() )
            found = found || d.code == QStringLiteral( "study.budget_exceeded" );
        REQUIRE( found );
        REQUIRE( fix.backend.submissions == 0 );
    }
    SECTION( "relative output dir" )
    {
        const auto spec = specFor();
        const auto result = runner.run( spec, cancel, QStringLiteral( "relative/outputs" ) );
        REQUIRE( !result.has_value() );
        bool found = false;
        for ( const auto &d : result.diagnostics() )
            found = found || d.code == QStringLiteral( "study.output_dir_invalid" );
        REQUIRE( found );
        REQUIRE( fix.backend.submissions == 0 );
    }
    SECTION( "output parameter is runner-owned" )
    {
        auto spec = specFor();
        spec.baseParameters.insert( QStringLiteral( "output" ),
                                    QStringLiteral( "/tmp/x.tif" ) );
        const auto result = runner.run( spec, cancel, fix.outputDir() );
        REQUIRE( !result.has_value() );
        bool found = false;
        for ( const auto &d : result.diagnostics() )
            found = found || d.code == QStringLiteral( "study.spec_output_reserved" );
        REQUIRE( found );
        REQUIRE( fix.backend.submissions == 0 );
    }
}

TEST_CASE( "progress callbacks observe the full lifecycle", "[study][runner]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 2 );
    std::atomic<bool> cancel{ false };
    QVector<StudyProgress::Phase> phases;
    runner.setProgressCallback(
        [&]( const StudyProgress &progress ) { phases.append( progress.phase ); } );

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    REQUIRE( phases.contains( StudyProgress::Phase::Started ) );
    REQUIRE( phases.count( StudyProgress::Phase::PointSubmitted ) == 2 );
    REQUIRE( phases.count( StudyProgress::Phase::PointFinished ) == 2 );
    REQUIRE( phases.contains( StudyProgress::Phase::Finished ) );
}

// ── Slice G: replay / cancel / budget hardening oracles ─────────────────────

TEST_CASE( "deterministic replay: same spec on a fresh store yields identical "
           "point identities and run parameter documents",
           "[study][runner][replay]" )
{
    Fixture fixA;
    Fixture fixB;
    StudyRunner runnerA( fixA.store, fixA.ledger, fixA.backend );
    StudyRunner runnerB( fixB.store, fixB.ledger, fixB.backend );
    const auto spec = specFor( /*steps=*/4 );
    std::atomic<bool> cancel{ false };

    REQUIRE( runnerA.run( spec, cancel, fixA.outputDir() ).has_value() );
    REQUIRE( runnerB.run( spec, cancel, fixB.outputDir() ).has_value() );

    REQUIRE( fixA.backend.submittedParameters.size()
             == fixB.backend.submittedParameters.size() );
    for ( int i = 0; i < fixA.backend.submittedParameters.size(); ++i )
    {
        // The "output" location is environment, not semantics — strip it and
        // require the scientific parameters to be identical.
        QJsonObject a = fixA.backend.submittedParameters.at( i );
        QJsonObject b = fixB.backend.submittedParameters.at( i );
        a.remove( QStringLiteral( "output" ) );
        b.remove( QStringLiteral( "output" ) );
        REQUIRE( a == b );
    }

    const auto pointsA = sampleStudyPoints( spec ).value();
    const auto pointsB = sampleStudyPoints( spec ).value();
    REQUIRE( pointsA == pointsB );
    REQUIRE( fixA.backend.submittedParameters.size() == pointsA.size() );
}

TEST_CASE( "cancellation before any submission leaves zero runs and a typed reason",
           "[study][runner][cancel]" )
{
    Fixture fix;
    StudyRunner runner( fix.store, fix.ledger, fix.backend );
    const auto spec = specFor( 3 );
    std::atomic<bool> cancel{ true }; // cancelled before the study even starts

    const auto result = runner.run( spec, cancel, fix.outputDir() );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.stoppedReason == QStringLiteral( "cancelled" ) );
    REQUIRE( summary.recordedCount == 0 );
    REQUIRE( summary.failedCount == 0 );
    REQUIRE( summary.cancelledCount == 0 );
    REQUIRE( fix.backend.submissions == 0 );
    REQUIRE( summary.runIds.isEmpty() );
}
