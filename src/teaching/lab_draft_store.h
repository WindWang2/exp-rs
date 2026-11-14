// lab_draft_store.h — durable local manual drafts (R30-T07 WP10).
//
// The guided lab workspace kept drafts in an in-memory QHash that was
// CLEARED on a lab switch and vanished on restart; the only persisted
// evidence was `human:<stepId>:<length>`, which records a LENGTH and can
// never restore the text. This leaf stores the actual draft bytes with a
// bounded read + atomic publish through the platform sidecar authority.
//
// Contract:
//   * keyed by course / lab / STABLE step id (never a row index);
//   * an autosaved draft is NOT a completion and NOT a pass;
//   * old `human:<stepId>:<length>` refs remain READABLE but are explicitly
//     marked as text-unrecoverable;
//   * rule/artifact changes make a restored draft STALE with a reason.
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sicnu::teaching
{

inline constexpr const char *kLabDraftStoreSchema = "sicnu.teaching.lab_drafts/1";
inline constexpr std::size_t kLabDraftStoreMaxBytes = 4u * 1024u * 1024u;

struct LabDraftRecord
{
    std::string courseId;
    std::string labId;
    std::string stepId;
    std::string text;
    bool submitted = false;          ///< explicit evidence submission, not completion
    std::string rulesVersion;        ///< context the draft was written under
    std::string artifactPath;        ///< context the draft was written under
    std::string updatedAtIso;        ///< caller-injected; the leaf reads no clock
};

struct LabDraftStoreState
{
    bool ok = false;
    std::vector<LabDraftRecord> drafts;
    std::vector<std::string> issues;
};

/// Stable composite key — lab switches and step reorders cannot alias.
std::string labDraftKey( const std::string &courseId, const std::string &labId,
                         const std::string &stepId );

/// Bounded, fail-closed read. A missing store is an honest EMPTY state
/// (ok=true, no drafts), not a parse error.
LabDraftStoreState loadLabDrafts( const std::string &path,
                                  std::size_t maxBytes = kLabDraftStoreMaxBytes );

/// Atomic publish (prepare → fsync → rename → read-back verify). This is a
/// SINGLE document (no last-good rotation): a failed write leaves the
/// previous document intact; an unreadable document must be left alone
/// rather than overwritten (the workspace caller enforces that).
bool saveLabDrafts( const std::string &path, const std::vector<LabDraftRecord> &drafts,
                    std::string *error, std::size_t maxBytes = kLabDraftStoreMaxBytes );

/// Legacy evidence ref `human:<stepId>:<length>`: readable metadata only.
struct LegacyHumanRef
{
    bool parsed = false;
    std::string stepId;
    std::size_t length = 0;
    /// ALWAYS false: a length cannot reconstruct the submitted text.
    bool textRecoverable = false;
    std::string reason;
};
LegacyHumanRef parseLegacyHumanRef( const std::string &ref );

/// Validity of a restored draft under the CURRENT grading context.
struct LabDraftValidity
{
    bool valid = false;
    std::vector<std::string> reasons;
};
LabDraftValidity checkLabDraftValidity( const LabDraftRecord &record,
                                        const std::string &currentRulesVersion,
                                        const std::string &currentArtifactPath );

} // namespace sicnu::teaching
