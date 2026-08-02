// src/processing/framework/tool_call_dispatcher.h
#pragma once

#include <json/json.h>

#include <QString>
#include <QVariantMap>

#include <chrono>
#include <functional>

namespace sicnu::data {
class DataManager;
}

namespace sicnu {
struct AlgorithmTaskInfo;
class OutputCommitter;
}

namespace sicnu::processing {

/// How a parsed LLM tool-call envelope should be handled.
enum class ToolCallClassification {
  ToolCall,    ///< Single algorithm call: submit through the sink.
  PlanRequest, ///< Multi-step plan: route through plan approval, never the sink.
  Invalid      ///< Unparseable envelope or unresolvable algorithm name.
};

/// GUI-free dispatcher (QtCore/Json only) for LLM tool-call envelopes
/// (ADR 0021). Parses all historical envelope shapes, normalizes the algorithm
/// id, submits typed parameters through an injected sink, and delivers the
/// completion payload through an injected watcher. Never blocks the calling
/// thread in `submit`. Instance class: production wiring lives in the caller
/// (AgentCopilotDockWidget), tests inject fakes.
class ToolCallDispatcher {
public:
  /// Submits an algorithm for background execution; returns a task id (> 0) or
  /// -1 on failure. Production: TaskCenter::enqueueTask; tests: fakes.
  using SubmissionSink = std::function<long( const QString &algorithmId, const QVariantMap &params )>;
  /// Observes task completion; must invoke onComplete exactly once with the
  /// result payload once the task reaches a terminal status.
  using CompletionWatcher = std::function<void( long taskId, std::function<void( const Json::Value &resultPayload )> onComplete )>;
  using CompletionCallback = std::function<void( const Json::Value &resultPayload )>;

  ToolCallDispatcher( SubmissionSink sink, CompletionWatcher watcher );

  /// Injects DataManager asset authority to automatically handle transactional output asset committing.
  void setDataManager( sicnu::data::DataManager *dataManager );
  sicnu::data::DataManager *dataManager() const { return mDataManager; }

  /// Classifies an envelope without side effects:
  /// - PlanRequest when the arguments contain a top-level `steps` array;
  /// - ToolCall when the name resolves in AtomicAlgorithmRegistry (as-is, then
  ///   with the first underscore rewritten to `:` on miss);
  /// - Invalid for unparseable envelopes or unresolvable names.
  ToolCallClassification classify( const Json::Value &envelope ) const;

  /// Reports, without side effects, why an envelope would be rejected by
  /// submit() (malformed / plan / unresolvable name / missing required
  /// parameter). Empty when the envelope would be dispatched. The string
  /// matches what submit() reports via errorOut.
  QString rejectionReason( const Json::Value &envelope ) const;

  /// Parses the envelope (all historical shapes), normalizes the id, validates
  /// the descriptor's required inputs, submits the typed parameters via the
  /// sink and registers completion via the watcher. Never blocks the calling
  /// thread. Returns false and fills @a errorOut (when non-null) for
  /// Invalid / PlanRequest envelopes, missing required parameters, and sink
  /// rejections. When @a taskIdOut is non-null, it receives the submitted
  /// task id on success and is left untouched on failure.
  bool submit( const Json::Value &envelope, CompletionCallback onComplete,
               QString *errorOut = nullptr, long *taskIdOut = nullptr );

  /// Handler invoked to transactionally commit output assets upon completion.
  /// Returns true on success and sets @a outCommittedPath; returns false on failure and sets @a outCommitError.
  using OutputCommitterHandler = std::function<bool( const sicnu::AlgorithmTaskInfo &info,
                                                     std::string &outCommittedPath,
                                                     std::string &outCommitError )>;

  void setOutputCommitterHandler( OutputCommitterHandler handler ) { mOutputCommitterHandler = std::move( handler ); }
  const OutputCommitterHandler &outputCommitterHandler() const { return mOutputCommitterHandler; }

  /// Build a standardized result payload (status "success" / "error", output,
  /// errorMessage, etc.) from an AlgorithmTaskInfo struct. If @a committerHandler is
  /// provided and the task succeeded with an output path, transactionally commits
  /// the asset and rewrites output to the committed stable path. A refused commit
  /// downgrades the payload to status "error" (commitError and errorMessage set)
  /// and keeps the task's original output path.
  static Json::Value buildTaskResultPayload( const sicnu::AlgorithmTaskInfo &info,
                                              const OutputCommitterHandler &committerHandler = {} );

  /// Synchronous entry point: submits task, awaits completion or timeout,
  /// applies OutputCommitterHandler if configured, and returns standardized result JSON.
  Json::Value dispatchAndAwait( const Json::Value &envelope,
                                std::chrono::milliseconds timeout = std::chrono::minutes( 30 ) );

  /// Tests/headless convenience: submit + block until completion or timeout.
  /// Delegates directly to dispatchAndAwait.
  Json::Value submitBlocking( const Json::Value &envelope,
                              std::chrono::milliseconds timeout = std::chrono::minutes( 30 ) );

private:
  struct ParsedEnvelope {
    std::string name;
    Json::Value arguments; ///< object, or empty object when absent
    bool valid = false;
  };

  static ParsedEnvelope parseEnvelope( const Json::Value &envelope );
  /// Resolve-first id normalization: look the name up as-is; only on miss,
  /// retry with the FIRST underscore replaced by `:`. Never rewrites an id
  /// that already resolves. Returns the best candidate (may be unchanged).
  static std::string resolveAlgorithmId( const std::string &rawName );
  /// True when the parsed arguments carry a top-level `steps` array.
  static bool isPlanRequest( const ParsedEnvelope &parsed );
  /// Rejection reason for an already-parsed envelope; empty ⇒ submittable.
  /// Shared by submit() (via errorOut) and rejectionReason().
  QString rejectionReasonFor( const ParsedEnvelope &parsed ) const;

  SubmissionSink mSink;
  CompletionWatcher mWatcher;
  OutputCommitterHandler mOutputCommitterHandler;
  sicnu::data::DataManager *mDataManager = nullptr;
};

} // namespace sicnu::processing
