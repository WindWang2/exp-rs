// verdict_stamp.h — bind a recorded verdict to the artifact bytes it graded.
//
// The teaching session persists the student's last validation summary
// (LabFeedbackProjection JSON) inside the session file. On restart that
// document is untrusted input: #1519/#1525 closed the *internal
// consistency* forgeries (counts_as_pass flips), but a summary edited
// wholesale — or a summary recorded for artifact A presented beside a
// re-submitted artifact B — had nothing to contradict it.
//
// The stamp is a bounded content fingerprint of the artifact, taken by the
// session save authority when a verdict is recorded, and re-derived on
// load. It is NOT a trust root (same discipline as lab/sha256.h): it
// detects drift and tampering against a file the student controls, where
// the alternative is detecting nothing at all. Two bounds to know:
// (1) an attacker who can EDIT BOTH the session file and the artifact can
//     recompute this public unkeyed stamp consistently — the stamp raises
//     the bar from "edit one field" to "recompute two digests over the
//     right bytes", nothing more; (2) the content digest samples head,
//     interior and tail 64 KiB windows, so a rewrite confined to an
//     unsampled region of a >~512 KiB artifact does not trip drift.
//
// Encoding: "v1:<summary-sha256>:<size>:<content-sha256>"
//   - summary-sha256 binds the stamp to the exact summary document, so a
//     post-stamp edit of the summary voids the verdict;
//   - size + content-sha256 (head/interior/tail windows, bounded read)
//     identify the artifact bytes the verdict graded — same sampled-window
//     shape as the batch checkpoint stamp (teaching_admin #1520/#1525).
// Old encodings simply stop parsing and fail closed to "unverifiable".
#pragma once

#include <string>

namespace sicnu::teaching
{

/// A parsed verdict stamp. `parsed` is false for anything that is not the
/// exact "v1" encoding — readers must treat that as "no usable binding".
struct VerdictStamp
{
  bool parsed = false;
  std::string summaryDigest;
  unsigned long long size = 0;
  std::string contentDigest;
};

/// Parses a stamp string; VerdictStamp{} (parsed=false) when malformed.
VerdictStamp parseVerdictStamp( const std::string &stamp );

/// Fingerprint of the artifact at @p artifactPath: sha256 over head,
/// evenly-spaced interior and tail windows (bounded bytes read regardless
/// of file size). Returns "" when the path is empty, missing or
/// unreadable — callers must treat "" as "cannot bind", never as a match.
std::string verdictArtifactContentDigest( const std::string &artifactPath );

/// Builds the stamp for a freshly recorded verdict. Returns "" when the
/// artifact cannot be read (no binding is better than a wrong one).
std::string makeVerdictStamp( const std::string &summaryDigest,
                              const std::string &artifactPath );

/// sha256 hex of a summary document's compact JSON serialization — the
/// stable identity of the recorded verdict text.
std::string verdictSummaryDigest( const std::string &summaryJson );

} // namespace sicnu::teaching
