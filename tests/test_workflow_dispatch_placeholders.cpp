#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/task_center.h"
#include "support/bounded_wait.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <atomic>
#include <memory>
#include <string>

namespace
{
void ensureApp()
{
    static int argc = 1;
    static char name[] = "test_workflow_dispatch_placeholders";
    static char *argv[] = { name, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );
}

struct EngineScope
{
    EngineScope()
    {
        ensureApp();
        sicnu::jobs::JobEngine::instance().shutdownForTests();
        sicnu::jobs::JobEngine::instance().clearExecutors();
        sicnu::jobs::JobEngine::instance().setMaxWorkers( 1 );
        sicnu::TaskCenter::instance().clearCompletedTasks();
    }
    ~EngineScope()
    {
        sicnu::jobs::JobEngine::instance().shutdownForTests();
        sicnu::TaskCenter::instance().clearCompletedTasks();
    }
};

long completedParent( sicnu::TaskCenter &center, const QString &output )
{
    const long id = center.enqueueTask( QStringLiteral( "dispatch:parent" ),
                                       { { QStringLiteral( "OUTPUT" ), output } }, false );
    REQUIRE( id > 0 );
    center.markTaskCompleted( id );
    REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );
    return id;
}

sicnu::AlgorithmTaskInfo runChild( sicnu::TaskCenter &center, const Json::Value &params,
                                  const QList<long> &parents,
                                  const std::shared_ptr<std::atomic<int>> &calls )
{
    sicnu::jobs::JobRequest request;
    request.algorithmId = "dispatch:child";
    request.params = params;
    const long id = center.submitJob(
        request,
        [calls]( const sicnu::jobs::JobRequest &resolved, sicnu::operators::RSOperatorContext & ) {
            ++*calls;
            Json::Value result( Json::objectValue );
            result["observed"] = resolved.params;
            return result;
        }, {}, false, sicnu::TaskPriority::Normal, parents );
    REQUIRE( id > 0 );
    REQUIRE( sicnu_test::waitUntil(
        [&] { return sicnu::isTerminalStatus( center.getTaskInfo( id ).status ); }, 10000,
        [&] { return center.getTaskInfo( id ).errorMessage.toStdString(); },
        [] { QCoreApplication::processEvents( QEventLoop::AllEvents, 5 ); } ) );
    return center.getTaskInfo( id );
}
} // namespace

TEST_CASE( "ambiguous parent references never launch while explicit references select the right parent",
           "[workflow][dispatch][issue-1779]" )
{
    EngineScope scope;
    auto &center = sicnu::TaskCenter::instance();
    const long first = completedParent( center, QStringLiteral( "/first.tif" ) );
    const long second = completedParent( center, QStringLiteral( "/second.tif" ) );
    const auto calls = std::make_shared<std::atomic<int>>( 0 );
    Json::Value params( Json::objectValue );
    params["input"] = "${task.parent}";
    for ( const QList<long> &parents : { QList<long>{ first, second }, QList<long>{ second, first } } )
    {
        const auto failed = runChild( center, params, parents, calls );
        CHECK( failed.status == sicnu::TaskStatus::Failed );
        CHECK( failed.errorMessage.contains( QStringLiteral( "${task.parent}" ) ) );
        CHECK( failed.jobId.empty() );
        CHECK( calls->load() == 0 );
    }

    params["input"] = "${task." + std::to_string( second ) + ".output}";
    const auto explicitParent = runChild( center, params, { first, second }, calls );
    REQUIRE( explicitParent.status == sicnu::TaskStatus::Completed );
    CHECK( explicitParent.resultPayload["observed"]["input"].asString() == "/second.tif" );
    CHECK( calls->load() == 1 );

    params["input"] = "${task.parent}";
    const auto soleParent = runChild( center, params, { first }, calls );
    REQUIRE( soleParent.status == sicnu::TaskStatus::Completed );
    CHECK( soleParent.resultPayload["observed"]["input"].asString() == "/first.tif" );
    CHECK( calls->load() == 2 );
}

TEST_CASE( "unresolved references in nested parameters fail before the executor is submitted",
           "[workflow][dispatch][issue-1780]" )
{
    EngineScope scope;
    auto &center = sicnu::TaskCenter::instance();
    const auto calls = std::make_shared<std::atomic<int>>( 0 );
    Json::Value params( Json::objectValue );
    params["inputs"][0]["source"] = "$missing.output";
    const auto failed = runChild( center, params, {}, calls );
    CHECK( failed.status == sicnu::TaskStatus::Failed );
    CHECK( failed.errorMessage.contains( QStringLiteral( "$missing.output" ) ) );
    CHECK( failed.jobId.empty() );
    CHECK( calls->load() == 0 );

    params["inputs"][0]["source"] = "/plain-input.tif";
    const auto plain = runChild( center, params, {}, calls );
    REQUIRE( plain.status == sicnu::TaskStatus::Completed );
    CHECK( plain.resultPayload["observed"]["inputs"][0]["source"].asString() == "/plain-input.tif" );
    CHECK( calls->load() == 1 );
}
