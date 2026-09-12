// execution_event_conversion.h — WorkflowRun → ExecutionEvent conversion
// shared by the bridge consumers (workflow_experiment_adapter and
// lab_run_recorder). The conversion is the single projection point of
// authoritative execution truth into the bridge's neutral event vocabulary;
// both consumers must use THE SAME one so recorded evidence is identical
// regardless of which surface enabled recording.
#pragma once

#include "experiment/run_bridge.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <json/json.h>

namespace sicnu::workflow
{
class WorkflowRun;
} // namespace sicnu::workflow

namespace sicnu::experiment
{

/// Bounded jsoncpp → QJson conversion (≤16 depth, ≤512 members/elements;
/// truncation is marked with an explicit "…truncated" entry, never silent).
QJsonValue jsonCppToQJson( const Json::Value &value, int depth = 0 );

/// ONE locked-snapshot conversion of the authoritative run aggregate into an
/// execution event (see workflow_experiment_adapter.cpp for the thread-safety
/// contract). `state` must be a member of bridgeExecutionStates().
ExecutionEvent workflowRunToExecutionEvent( const workflow::WorkflowRun &run,
                                            const QString &state, qint64 startedMs,
                                            qint64 finishedMs );

} // namespace sicnu::experiment
