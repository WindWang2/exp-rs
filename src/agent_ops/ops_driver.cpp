// src/agent_ops/ops_driver.cpp
#include "agent_ops/ops_driver.h"

#include "agent_ops/session_surface.h"

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
    if (action == "run")
    {
        if (!args.isObject() || args.get("goal", "").asString().empty())
            return errorDoc(action, "MISSING_ARGS");

        // Wire-visible mode selection (defaults to the loop's own default).
        sicnu::agent_loop::RunMode mode = sicnu::agent_loop::RunMode::ExecuteWithVerify;
        const std::string modeText = args.get("mode", "").asString();
        if (!modeText.empty() && !sicnu::agent_loop::parseRunMode(modeText, mode))
            return errorDoc(action, "UNKNOWN_MODE");

        const std::string missing = missingSeamsFor(mode);
        if (!missing.empty())
        {
            Json::Value doc = errorDoc(action, "SEAMS_UNAVAILABLE");
            doc["missing_seams"] = missing;
            return doc;
        }

        // No silent duplicate submit: when the caller names a session that
        // already journaled SUBMITTED runs, the restart hazard is surfaced
        // and the launch refused — restart as a NEW session, or reconcile
        // and decide; never a blind re-run of recorded science.
        const std::string journalDirectory = args.get("journal_directory", "").asString();
        const std::string namedSession = args.get("session_id", "").asString();
        if (!journalDirectory.empty() && !namedSession.empty())
        {
            auto recon = mReconciler.reconcileFile(journalDirectory, namedSession);
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
        request.session.goal = args["goal"].asString();
        request.session.intent = args.get("intent", "").asString();
        if (args.isMember("refs"))
            request.session.refs = args["refs"];
        request.policy.mode = mode;
        request.journalDirectory = journalDirectory;
        request.domain = args.get("domain", "research").asString();
        request.role = args.get("role", "").asString();
        request.approvePendingRepair = args.get("approve_pending_repair", false).asBool();
        OpsRunResult result = mCoordinator.run(request);
        remember(result);
        return statusFor(result, action);
    }

    if (action == "resume")
    {
        if (!args.isObject() || args.get("journal_directory", "").asString().empty() ||
            args.get("session_id", "").asString().empty() ||
            args.get("goal", "").asString().empty())
            return errorDoc(action, "MISSING_ARGS");

        sicnu::agent_loop::RunMode mode = sicnu::agent_loop::RunMode::ExecuteWithVerify;
        const std::string modeText = args.get("mode", "").asString();
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
        request.session.goal = args["goal"].asString();
        request.session.intent = args.get("intent", "").asString();
        request.policy.mode = mode;
        request.journalDirectory = args["journal_directory"].asString();
        request.domain = args.get("domain", "research").asString();
        request.role = args.get("role", "").asString();
        request.approvePendingRepair = args.get("approve_pending_repair", false).asBool();
        const std::string sessionId = args["session_id"].asString();
        OpsRunResult result =
            mCoordinator.resume(request.journalDirectory, sessionId, request);
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
        const std::string sessionId = args.get("session_id", mLastSessionId).asString();
        if (sessionId.empty())
            return errorDoc(action, "NO_SESSION");
        const OpsRunResult *result = findResult(sessionId);
        if (!result)
            return errorDoc(action, "UNKNOWN_SESSION");
        return statusFor(*result, action);
    }

    if (action == "timeline")
    {
        const std::string sessionId = args.get("session_id", mLastSessionId).asString();
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
        const std::string sessionId = args.get("session_id", mLastSessionId).asString();
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
    // resume_clear_pause alias pass straight through to the surface.
    return sessionSurfaceApply(mCoordinator, action, args);
}

} // namespace sicnu::agent_ops
