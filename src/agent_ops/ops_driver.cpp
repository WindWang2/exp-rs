// src/agent_ops/ops_driver.cpp
#include "agent_ops/ops_driver.h"

#include "agent_ops/session_surface.h"


#include <filesystem>
#include <fstream>

namespace sicnu::agent_ops {
namespace {

Json::Value errorDoc(const std::string &action, const std::string &error)
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = kOpsDriverSchema;
    doc["action"] = action;
    doc["ok"] = false;
    doc["error"] = error;
    return doc;
}

/// String argument that never throws: a wrong-typed value counts as absent
/// (the caller asked for something the driver cannot read — typed refusal
/// downstream, never a Json::LogicError escaping the wire contract).
std::string getStringArg(const Json::Value &args, const char *key)
{
    if (!args.isObject() || !args.isMember(key) || !args[key].isString())
        return {};
    return args[key].asString();
}

/// Bounded scan of a journal directory: non-terminal journals with SUBMITTED
/// runs are restart hazards. Surfaced as a warning on new launches into the
/// same directory — a blind fresh session would silently redo that science.
Json::Value scanRestartHazards(const std::string &directory, ResumeReconciler &reconciler)
{
    static constexpr std::size_t kMaxScanned = 256;
    Json::Value hazards(Json::arrayValue);
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec), end;
    if (ec)
        return hazards;
    for (; it != end && hazards.size() < kMaxScanned; it.increment(ec))
    {
        if (ec || !it->is_regular_file() || it->path().extension() != ".json")
            continue;
        const std::string stem = it->path().stem().string();
        auto recon = reconciler.reconcileFile(directory, stem);
        if (recon.ok && !recon.resumable && recon.duplicateSubmitRisk)
        {
            Json::Value entry(Json::objectValue);
            entry["session_id"] = stem;
            entry["reason_code"] = recon.reasonCode;
            Json::Value runs(Json::arrayValue);
            for (const auto &id : recon.submittedRunIds)
                runs.append(id);
            entry["submitted_run_ids"] = runs;
            hazards.append(entry);
        }
    }
    return hazards;
}

/// The seams each run mode needs to serve a session honestly. The loop
/// itself refuses missing seams mid-run; the driver refuses BEFORE launch
/// with a precise typed code so hosts fail closed without a half-run.
std::vector<std::string> requiredSeamsFor(sicnu::agent_loop::RunMode mode)
{
    using sicnu::agent_loop::RunMode;
    // Every mode snapshots data state, plans and preflights.
    std::vector<std::string> required = {"data", "planner", "preflight"};
    if (mode == RunMode::ExecuteWithVerify)
    {
        required.push_back("executor");
        required.push_back("verifier");
        // A failed verified execution routes to diagnose; running real
        // science without a diagnoser would dead-end in an internal error
        // instead of a typed refusal — require it up front.
        required.push_back("diagnoser");
    }
    return required;
}

} // namespace

OpsDriver::OpsDriver(Options options)
    : mCoordinator(std::move(options.deps), std::move(options.recorder))
{
}

Json::Value OpsDriver::actions() const
{
    Json::Value doc = sessionSurfaceActions();
    doc["schema"] = kOpsDriverSchema;
    Json::Value extended(Json::arrayValue);
    extended.append("reconcile");
    for (const auto &a : doc["actions"])
        extended.append(a);
    doc["actions"] = extended;
    return doc;
}

std::string OpsDriver::missingSeamsFor(sicnu::agent_loop::RunMode mode) const
{
    const auto &seams = mCoordinator.dependencies().seams;
    std::string missing;
    for (const auto &name : requiredSeamsFor(mode))
    {
        const bool present = name == "data"       ? seams.data != nullptr
                             : name == "planner"  ? seams.planner != nullptr
                             : name == "preflight"? seams.preflight != nullptr
                             : name == "executor" ? seams.executor != nullptr
                             : name == "diagnoser"? seams.diagnoser != nullptr
                                                  : seams.verifier != nullptr;
        if (!present)
            missing += (missing.empty() ? "" : ",") + name;
    }
    return missing;
}

const OpsRunResult *OpsDriver::findResult(const std::string &sessionId) const
{
    const auto it = mSessions.find(sessionId);
    return it == mSessions.end() ? nullptr : &it->second;
}

void OpsDriver::remember(const OpsRunResult &result)
{
    const std::string id = result.session.sessionId;
    if (id.empty())
        return;
    if (mSessions.find(id) == mSessions.end())
    {
        mOrder.push_back(id);
        while (mOrder.size() > kMaxTrackedSessions)
        {
            mSessions.erase(mOrder.front());
            mOrder.pop_front();
        }
    }
    mSessions[id] = result;
    mLastSessionId = id;
}

Json::Value OpsDriver::statusFor(const OpsRunResult &result, const std::string &action) const
{
    // The surface document IS the wire contract (CLI/MCP/panel parity); the
    // driver only adds its own schema tag.
    Json::Value doc = sessionSurfaceStatus(result);
    doc["schema"] = kSessionSurfaceSchema;
    doc["action"] = action;
    doc["driver_schema"] = kOpsDriverSchema;
    return doc;
}

Json::Value OpsDriver::apply(const std::string &action, const Json::Value &args)
{
    // Single boundary for wrong-typed arguments: jsoncpp throws LogicError
    // on type mismatches; the wire contract is typed documents, so the
    // exception is translated ONCE here for every action path.
    try
    {
        return applyChecked(action, args);
    }
    catch (const Json::LogicError &)
    {
        return errorDoc(action, "INVALID_ARGS");
    }
}

Json::Value OpsDriver::applyChecked(const std::string &action, const Json::Value &args)
{
    if (action == "run")
    {
        const std::string goal = getStringArg(args, "goal");
        if (goal.empty())
            return errorDoc(action, "MISSING_ARGS");

        // Wire-visible mode selection (defaults to the loop's own default).
        sicnu::agent_loop::RunMode mode = sicnu::agent_loop::RunMode::ExecuteWithVerify;
        const std::string modeText = getStringArg(args, "mode");
        if (!modeText.empty() && !sicnu::agent_loop::parseRunMode(modeText, mode))
            return errorDoc(action, "UNKNOWN_MODE");

        const std::string missing = missingSeamsFor(mode);
        if (!missing.empty())
        {
            Json::Value doc = errorDoc(action, "SEAMS_UNAVAILABLE");
            doc["missing_seams"] = missing;
            return doc;
        }

        // No silent duplicate submit: a NAMED session must never be blind
        // re-run. Resumable journals belong to resume(); journals holding
        // submitted runs or terminal outcomes are refused with the evidence
        // attached — restart as a NEW session, or reconcile and decide.
        const std::string journalDirectory = getStringArg(args, "journal_directory");
        const std::string namedSession = getStringArg(args, "session_id");
        if (!journalDirectory.empty() && !namedSession.empty())
        {
            auto recon = mReconciler.reconcileFile(journalDirectory, namedSession);
            if (recon.ok && recon.resumable)
            {
                Json::Value doc = errorDoc(action, "SESSION_RESUMABLE_USE_RESUME");
                doc["reconcile"] = recon.toJson();
                return doc;
            }
            if (recon.ok && !recon.resumable && recon.duplicateSubmitRisk)
            {
                // Same typed code the coordinator resume path reports for
                // delivered journals; crashed mid-run journals report the
                // submitted-run variant.
                Json::Value doc = errorDoc(action, recon.terminalState == "delivered"
                                                             ? "DUPLICATE_SUBMIT_REFUSED"
                                                             : "SUBMITTED_RUN_EXISTS");
                doc["reconcile"] = recon.toJson();
                return doc;
            }
        }

        OpsRunRequest request;
        request.session.goal = goal;
        request.session.intent = getStringArg(args, "intent");
        if (args.isObject() && args.isMember("refs"))
        {
            if (!args["refs"].isObject())
                return errorDoc(action, "INVALID_ARGS");
            request.session.refs = args["refs"];
        }
        request.policy.mode = mode;
        request.journalDirectory = journalDirectory;
        request.domain = getStringArg(args, "domain").empty()
                             ? std::string("research")
                             : getStringArg(args, "domain");
        request.role = getStringArg(args, "role");
        request.approvePendingRepair =
            args.isObject() && args.isMember("approve_pending_repair") &&
            args["approve_pending_repair"].asBool();
        OpsRunResult result = mCoordinator.run(request);
        remember(result);
        Json::Value doc = statusFor(result, action);
        // A launch into a directory that holds crashed journals with
        // submitted runs is honest work only when the operator SEES them:
        // attach the hazard list without refusing the (legitimate) new
        // session.
        if (!journalDirectory.empty() && namedSession.empty())
            doc["restart_hazards"] = scanRestartHazards(journalDirectory, mReconciler);
        return doc;
    }

    if (action == "resume")
    {
        const std::string goal = getStringArg(args, "goal");
        const std::string journalDirectory = getStringArg(args, "journal_directory");
        const std::string sessionId = getStringArg(args, "session_id");
        if (goal.empty() || journalDirectory.empty() || sessionId.empty())
            return errorDoc(action, "MISSING_ARGS");

        sicnu::agent_loop::RunMode mode = sicnu::agent_loop::RunMode::ExecuteWithVerify;
        const std::string modeText = getStringArg(args, "mode");
        if (!modeText.empty() && !sicnu::agent_loop::parseRunMode(modeText, mode))
            return errorDoc(action, "UNKNOWN_MODE");
        const std::string missing = missingSeamsFor(mode);
        if (!missing.empty())
        {
            Json::Value doc = errorDoc(action, "SEAMS_UNAVAILABLE");
            doc["missing_seams"] = missing;
            return doc;
        }

        OpsRunRequest request;
        request.session.goal = goal;
        request.session.intent = getStringArg(args, "intent");
        request.policy.mode = mode;
        request.journalDirectory = journalDirectory;
        request.domain = getStringArg(args, "domain").empty()
                             ? std::string("research")
                             : getStringArg(args, "domain");
        request.role = getStringArg(args, "role");
        request.approvePendingRepair =
            args.isObject() && args.isMember("approve_pending_repair") &&
            args["approve_pending_repair"].asBool();
        OpsRunResult result = mCoordinator.resume(journalDirectory, sessionId, request);
        // A refused resume carries no session result (typed refusal only),
        // so remember() is a no-op there and a previously tracked session
        // is never clobbered by the refusal.
        remember(result);
        return statusFor(result, action);
    }

    if (action == "reconcile")
    {
        if (!args.isObject() || args.get("journal_directory", "").asString().empty() ||
            args.get("session_id", "").asString().empty())
            return errorDoc(action, "MISSING_ARGS");
        auto recon = mReconciler.reconcileFile(args["journal_directory"].asString(),
                                               args["session_id"].asString());
        Json::Value doc(Json::objectValue);
        doc["schema"] = kOpsDriverSchema;
        doc["action"] = action;
        doc["ok"] = true; // the report succeeded; the verdict lives inside
        doc["reconcile"] = recon.toJson();
        return doc;
    }

    if (action == "status")
    {
        std::string sessionId = getStringArg(args, "session_id");
        if (sessionId.empty())
            sessionId = mLastSessionId;
        if (sessionId.empty())
            return errorDoc(action, "NO_SESSION");
        const OpsRunResult *result = findResult(sessionId);
        if (!result)
            return errorDoc(action, "UNKNOWN_SESSION");
        return statusFor(*result, action);
    }

    if (action == "timeline")
    {
        std::string sessionId = getStringArg(args, "session_id");
        if (sessionId.empty())
            sessionId = mLastSessionId;
        if (sessionId.empty())
            return errorDoc(action, "NO_SESSION");
        const OpsRunResult *result = findResult(sessionId);
        if (!result)
            return errorDoc(action, "UNKNOWN_SESSION");
        Json::Value doc(Json::objectValue);
        doc["schema"] = kSessionSurfaceSchema;
        doc["driver_schema"] = kOpsDriverSchema;
        doc["action"] = action;
        doc["projection"] = result->projection.toJson();
        doc["ok"] = result->ok;
        return doc;
    }

    if (action == "export")
    {
        std::string sessionId = getStringArg(args, "session_id");
        if (sessionId.empty())
            sessionId = mLastSessionId;
        if (sessionId.empty())
            return errorDoc(action, "NO_SESSION");
        const OpsRunResult *result = findResult(sessionId);
        if (!result)
            return errorDoc(action, "UNKNOWN_SESSION");
        // The capsule payload IS the export document (drivers save it
        // as-is), rebuilt from the NAMED session — never from whatever the
        // coordinator happened to run last.
        Json::Value doc = DeliveryAssembler().capsuleExportDocument(result->delivery);
        doc["action"] = action;
        doc["ok"] = true;
        doc["driver_schema"] = kOpsDriverSchema;
        return doc;
    }

    if (action == "actions")
        return actions();

    // Control actions: pause/cancel/clear_*/approve_repair and the legacy
    // resume_clear_pause alias pass straight through to the surface
    // (wrong-typed arguments are caught by the apply() boundary above).
    return sessionSurfaceApply(mCoordinator, action, args);
}

} // namespace sicnu::agent_ops
