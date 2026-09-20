/***************************************************************************
 * snapshot_diff.h — readable drift reports for byte-gated contract snapshots
 *
 * Platform 12.0 (Track 02). The existing gates (contract_inventory --check,
 * contract_inventory --census-check) are *byte* gates: they answer only
 * "identical / not identical". A maintainer who trips one is told that
 * something changed and nothing about *what* changed. That is the failure
 * mode this header removes: every snapshot drift must render as a readable,
 * attributable diff, and drift that cannot be attributed to a known commit
 * must be visible as such rather than silently blessed.
 *
 * Design constraints, deliberately:
 *   - It is a pure *report* generator. It never decides pass/fail, never
 *     writes, never repairs. The gate stays a byte comparison; this only
 *     explains it. (A diff tool that also decided outcomes would let a
 *     "small enough" drift pass.)
 *   - It consumes already-serialized JSON so it works identically for the
 *     graph snapshot and the determinism census, and can be driven from a
 *     test or from the CLI without re-linking the assembly code.
 *   - Output is deterministic and line-ordered, so the same pair of inputs
 *     always produces byte-identical text. That makes the report itself
 *     snapshot-comparable and diffable in a code review.
 *
 * Element identity (the part that makes a *readable* diff possible):
 *   - graph snapshot  : a node's identity is (kind, id); an edge's identity
 *                       is (kind, from, to). The `origin` attribute is
 *                       *payload*, so moving a node's evidence file shows as
 *                       a single "changed" line rather than an add+remove
 *                       pair.
 *   - census snapshot : an entry's identity is `operatorId`.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::contracts {

/// One human-readable difference line.
struct SnapshotDiffLine
{
    /// "added" | "removed" | "changed"
    std::string change;
    /// Node kind (graph) or "entry" (census): what sort of thing this is.
    std::string kind;
    /// Stable identity of the element, e.g. "operator/gdal:clip".
    std::string id;
    /// For "changed": the attribute that moved ("origin"). Empty otherwise.
    std::string field;
    /// For "changed": the recorded value → the live value.
    std::string before;
    std::string after;
};

struct SnapshotDiffReport
{
    /// Which snapshot this describes ("exp.contract.graph.v1" or
    /// "exp.determinism_census.v1").
    std::string schema;
    /// True when the two documents have a different schema string, or an
    /// unrecognized one. A schema change is never a "small" drift.
    bool schemaMismatch = false;
    std::vector<SnapshotDiffLine> lines;

    /// Total number of difference lines. Zero means the documents carry the
    /// same element set.
    std::size_t size() const { return lines.size(); }
    bool empty() const { return lines.empty(); }
};

/// Diff a *recorded* snapshot document (the committed file) against a *live*
/// document (a fresh generation). Returns false only when the inputs cannot
/// be interpreted at all (missing `schema`, or an unrecognized schema); the
/// error string then names the reason. A schema mismatch is reported through
/// the result, not as a hard failure, so the caller can print both the
/// mismatch and any recoverable element diff.
bool diffContractSnapshot( const Json::Value &recorded,
                           const Json::Value &live,
                           SnapshotDiffReport &out,
                           std::string &error );

/// Render a report as deterministic, human-readable text. One line per
/// difference, prefixed "+" / "-" / "~" so the output is also greppable.
std::string formatSnapshotDiff( const SnapshotDiffReport &report );

} // namespace sicnu::contracts
