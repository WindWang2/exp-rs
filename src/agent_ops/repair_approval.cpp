// src/agent_ops/repair_approval.cpp
#include "agent_ops/repair_approval.h"

#include "repair_planner/repair_sha256.h"

#include <atomic>

namespace sicnu::agent_ops {
namespace {

std::atomic<long long> gInstanceCounter{0};

std::string intToString(long long value)
{
    return std::to_string(value);
}

/// The binding digest: sha256/16 over exactly the fields the token commits
/// to, joined in a fixed canonical order.
std::string approvalDigest(const Json::Value &tokenDoc)
{
    std::string hex = sicnu::repair::sha256Hex(
        tokenDoc["plan_id"].asString() + "|" + intToString(tokenDoc["coordinator_id"].asInt64()) +
        "|" + intToString(tokenDoc["issued_at_ms"].asInt64()) + "|" +
        intToString(tokenDoc["expires_at_ms"].asInt64()));
    hex.resize(16);
    return hex;
}

bool isHex16(const std::string &text)
{
    if (text.size() != 16)
        return false;
    for (char c : text)
    {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex)
            return false;
    }
    return true;
}

} // namespace

long long nextRepairApprovalInstanceId()
{
    return gInstanceCounter.fetch_add(1) + 1;
}

Json::Value mintRepairApprovalToken(const std::string &planId, long long coordinatorId,
                                    long long nowMs, long long ttlMs)
{
    // Fail closed: an unusable argument mints nothing, never a bare
    // "approved" marker without binding or expiry.
    if (planId.empty() || coordinatorId <= 0 || nowMs <= 0 || ttlMs <= 0)
        return Json::Value();
    Json::Value doc(Json::objectValue);
    doc["kind"] = kRepairApprovalKind;
    doc["schema_version"] = kRepairApprovalSchema;
    doc["plan_id"] = planId;
    doc["coordinator_id"] = static_cast<Json::Int64>(coordinatorId);
    doc["issued_at_ms"] = static_cast<Json::Int64>(nowMs);
    doc["expires_at_ms"] = static_cast<Json::Int64>(nowMs + ttlMs);
    doc["digest"] = approvalDigest(doc);
    return doc;
}

const char *verifyRepairApprovalToken(const Json::Value &tokenDoc,
                                      const std::string &planId, long long coordinatorId,
                                      long long nowMs)
{
    if (!tokenDoc.isObject() || !tokenDoc["kind"].isString() ||
        tokenDoc["kind"].asString() != kRepairApprovalKind ||
        !tokenDoc["schema_version"].isString() ||
        tokenDoc["schema_version"].asString() != kRepairApprovalSchema ||
        !tokenDoc["plan_id"].isString() || !tokenDoc["coordinator_id"].isInt64() ||
        !tokenDoc["issued_at_ms"].isInt64() || !tokenDoc["expires_at_ms"].isInt64() ||
        !tokenDoc["digest"].isString() || !isHex16(tokenDoc["digest"].asString()))
        return approval_check::kMalformed;

    if (tokenDoc["digest"].asString() != approvalDigest(tokenDoc))
        return approval_check::kTampered;

    if (tokenDoc["plan_id"].asString() != planId)
        return approval_check::kWrongPlan;

    if (tokenDoc["coordinator_id"].asInt64() != coordinatorId)
        return approval_check::kWrongCoordinator;

    // No usable clock means expiry cannot be proven false: the token is
    // treated as expired (fail closed), never as "still valid".
    if (nowMs <= 0 || nowMs > tokenDoc["expires_at_ms"].asInt64())
        return approval_check::kExpired;

    return approval_check::kOk;
}

} // namespace sicnu::agent_ops
