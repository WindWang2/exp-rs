// src/processing/framework/tool_call_dispatcher.h
#pragma once

#include <json/json.h>

#include <QString>
#include <QVariantMap>

#include <chrono>
#include <functional>
#include <memory>

class QObject;

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

  ToolCallDispatcher();
  ToolCallDispatcher( SubmissionSink sink, CompletionWatcher watcher );

  /// Injects DataManager asset authority to automatically handle transactional output asset committing.
  void setDataManager( sicnu::data::DataManager *dataManager );
  sicnu::data::DataManager *dataManager() const { return mDataManager; }

  /// Handler for `canvas:` tool calls — immediate, in-process canvas actions
  /// (e.g. draw_roi) that bypass the Task Center entirely. Unlike an algorithm
  /// submission, a canvas action runs synchronously when dispatched and its
  /// result is returned directly (no task id, no completion watcher). The
  /// handler receives the action name (the part after `canvas:`) and the parsed
  /// arguments, and returns a result payload (status "success"/"error" + fields)
  /// that submit()/dispatchAndAwait() deliver as if a task had completed. When
  /// unset, a `canvas:` call is rejected as unregistered. The agent surface
  /// wires this with a canvas adapter (QgsRubberBand draw + geometry write-back).
  using CanvasActionHandler = std::function<Json::Value( const std::string &action,
                                                         const Json::Value &arguments )>;
  void setCanvasActionHandler( CanvasActionHandler handler ) { mCanvasActionHandler = std::move( handler ); }
  const CanvasActionHandler &canvasActionHandler() const { return mCanvasActionHandler; }

  /// Handler for interaction tool calls (view:, roi:, canvas:, layer: namespaces).
  /// Receives the tool name and arguments and returns a structured Json result.
  using InteractionActionHandler = std::function<Json::Value( const std::string &toolName,
                                                              const Json::Value &arguments )>;
  void setInteractionActionHandler( InteractionActionHandler handler ) { mInteractionActionHandler = std::move( handler ); }
  const InteractionActionHandler &interactionActionHandler() const { return mInteractionActionHandler; }

  /// True when a tool name belongs to an interactive namespace (view:, roi:, canvas:, layer:).
  static bool isInteractionAction( const std::string &name );

  /// True when a tool name is a canvas action (the `canvas:` namespace). Shared
  /// by classify()/rejectionReasonFor()/submit() so the namespace check has one
  /// owner. `canvas:` is a sibling to `rs:` / provider algorithms, not a Task
  /// Center algorithm: it routes to the CanvasActionHandler, never the sink.
  static bool isCanvasAction( const std::string &name );

  /// Classifies an envelope without side effects:
  /// - PlanRequest when the arguments contain a top-level `steps` array;
  /// - ToolCall when the name resolves in AtomicAlgorithmRegistry (as-is, then
  ///   with the first underscore rewritten to `:` on miss);
  /// - Invalid for unparseable envelopes or unresolvable names.
  ToolCallClassification classify( const Json::Value &envelope ) const;

  /// Reports, without side effects, why an envelope would be rejected by
  /// submit() (malformed / plan / unresolvable name / schema validation
  /// failure). Empty when the envelope would be dispatched. The string
  /// matches what submit() reports via errorOut.
  QString rejectionReason( const Json::Value &envelope ) const;

  /// Structured, side-effect-free validation of an envelope. Runs the same
  /// path as rejectionReason() but returns machine-readable issues:
  /// {valid, errors:[{code, parameter, expected, actual, message}], warnings:[...]}.
  /// Shared by MCP/preflight so GUI, CLI, MCP and Agent surfaces validate
  /// parameters through the single schema_validator implementation.
  Json::Value validateCall( const Json::Value &envelope ) const;

  /// Extracts the arguments object from an envelope in any historical shape
  /// ({name, arguments} / {name, parameters} / {name, params}, or
  /// {function:{name, arguments}} with arguments as object or JSON string).
  /// Returns an empty object when the envelope has no arguments member and a
  /// null value when the envelope is malformed. Shares parseEnvelope with
  /// classify()/submit() so envelope-shape knowledge has a single owner.
  static Json::Value argumentsFor( const Json::Value &envelope );

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
  /// Returns true on success and sets @a outCommittedPath and @a outAssetId;
  /// returns false on failure and sets @a outCommitError.
  using OutputCommitterHandler = std::function<bool( const sicnu::AlgorithmTaskInfo &info,
                                                     std::string &outCommittedPath,
                                                     std::string &outCommitError,
                                                     std::string &outAssetId )>;

  void setOutputCommitterHandler( OutputCommitterHandler handler ) { mOutputCommitterHandler = std::move( handler ); }
  const OutputCommitterHandler &outputCommitterHandler() const { return mOutputCommitterHandler; }

  /// Optional post-commit verification handler.  When set, the dispatcher
  /// enriches successful payloads with "assetId", "assetKind" and a
  /// "verification" block.  The returned JSON must contain "ok" (bool),
  /// "kind" (string), "summary" (object) and "issues" (array of strings).
  /// Verification failures downgrade the payload to status "error" with
  /// "verified": false and "verificationIssues".
  using OutputVerificationHandler = std::function<Json::Value( const QString &committedPath,
                                                               const QString &kindHint )>;
  void setOutputVerificationHandler( OutputVerificationHandler handler ) { mOutputVerificationHandler = std::move( handler ); }
  const OutputVerificationHandler &outputVerificationHandler() const { return mOutputVerificationHandler; }

  /// Build a standardized result payload (status "success" / "error", output,
  /// errorMessage, etc.) from an AlgorithmTaskInfo struct. If @a committerHandler is
  /// provided and the task succeeded with an output path, transactionally commits
  /// the asset and rewrites output to the committed stable path. A refused commit
  /// downgrades the payload to status "error" (commitError and errorMessage set)
  /// and keeps the task's original output path.
  static Json::Value buildTaskResultPayload( const sicnu::AlgorithmTaskInfo &info,
                                              const OutputCommitterHandler &committerHandler = {},
                                              const OutputVerificationHandler &verificationHandler = {} );

  /// Synchronous entry point: submits task, awaits completion or timeout,
  /// applies OutputCommitterHandler if configured, and returns standardized result JSON.
  Json::Value dispatchAndAwait( const Json::Value &envelope,
                                std::chrono::milliseconds timeout = std::chrono::minutes( 30 ) );

  /// Tests/headless convenience: submit + block until completion or timeout.
  /// Delegates directly to dispatchAndAwait.
  Json::Value submitBlocking( const Json::Value &envelope,
                              std::chrono::milliseconds timeout = std::chrono::minutes( 30 ) );

  /// Event-loop-free sync wait for a task submitted through the sink. Wired by
  /// the Task Center flavor onto the ExecutionPlane so dispatchAndAwait() never
  /// depends on Qt queued delivery for its wakeup — the #559 deadlock class is
  /// structurally impossible on this path (the terminal notification arrives
  /// via a thread-safe channel, and the payload commit runs on the calling
  /// thread, which is the Data Manager's owning thread in every production
  /// caller). Test harnesses with fake sinks/watchers leave it unset and keep
  /// the legacy condition-variable wait.
  using SyncAwait = std::function<Json::Value( long taskId, std::chrono::milliseconds timeout )>;
  void setSyncAwait( SyncAwait await ) { mSyncAwait = std::move( await ); }

  /// Entry tag propagated into the ExecutionPlane request ("agent", "mcp",
  /// "cli", …) so JobEngine records carry which surface submitted the work.
  void setSourceTag( const QString &tag ) { mSourceTag = tag; }
  QString sourceTag() const { return mSourceTag; }

  /// Build the standardized result payload for a terminal task with the
  /// transactional output commit applied EXACTLY ONCE per task id (see
  /// ExecutionPlane::buildCommittedResultPayload). Multiple surfaces watching
  /// the same task (dispatcher watcher, copilot signal handler) can call this
  /// without racing the commit or double-registering the asset.
  Json::Value buildCommittedResultPayload( const sicnu::AlgorithmTaskInfo &info ) const;

private:
  struct ParsedEnvelope {
    std::string name;
    Json::Value arguments; ///< object, or empty object when absent
    bool valid = false;
  };

  /// submit() core shared with dispatchAndAwait(): everything after envelope
  /// parsing/validation up to (optionally) completion-watcher registration.
  /// When @a allowWatcher is false the sink submission happens without
  /// registering mWatcher — used by the sync path whose wait/commit is owned
  /// by mSyncAwait (prevents a double commit: watcher payload + sync payload).
  bool submitParsed( const ParsedEnvelope &parsed, CompletionCallback onComplete,
                     QString *errorOut, long *taskIdOut, bool allowWatcher );

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
  SyncAwait mSyncAwait;
  QString mSourceTag = QStringLiteral( "dispatcher" );
  OutputCommitterHandler mOutputCommitterHandler;
  OutputVerificationHandler mOutputVerificationHandler;
  CanvasActionHandler mCanvasActionHandler;
  InteractionActionHandler mInteractionActionHandler;
  sicnu::data::DataManager *mDataManager = nullptr;
  /// QObject owned by the dispatcher's construction thread (the Data Manager's
  /// owning thread in production). The completion watcher routes payload
  /// construction — which commits outputs through the Data Manager — back to
  /// this object's thread via QueuedConnection, because the detached watcher
  /// thread must not touch the catalog directly. A shared_ptr keeps the bridge
  /// alive across a dispatcher destructed while a watcher is still running.
  std::shared_ptr<QObject> m_commitBridge;
};

} // namespace sicnu::processing
