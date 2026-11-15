# Handoff

No cross-track request has been needed yet.

## Standing compatibility contracts

- T02 consumes existing JobEngine→TaskCenter→dispatcher/Result APIs. T01 must preserve #2063 Python approval, staging/path, identity, owner-thread, conflict and duplicate-commit behavior; no second committer is permitted.
- T03 consumes backend snapshots/events/APIs. No T01 change requires an unmerged UI or mapspec/layout implementation.
- Shared schemas, identities, error codes, cancellation and output contracts remain backward compatible; new JSON fields should be optional.

## Current blocker ownership

None. If another owner's file becomes necessary, record exact path, current/expected contract, minimal input, owner, compatibility option and verification here, then continue independent T01 work rather than expanding ownership silently.
