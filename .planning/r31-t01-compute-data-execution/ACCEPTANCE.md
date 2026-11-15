# Acceptance

Base: `85ce917bea0ece4df6548a677b97a72ecde8659b`. Current HEAD initially equals base. All entries are `NOT_EXECUTED` until tied to a command, exit code, real test-case count, and evidence on this worktree's current HEAD.

| Gate | Status | Required observable evidence |
|---|---|---|
| Configure/build | NOT_EXECUTED | Fresh `dev-default` configure and affected real targets under double guard. |
| E2E-1 reports vs artifacts | NOT_EXECUTED | Registry→JobEngine→TaskCenter→dispatcher; inspect/doctor/subdatasets/verify cause zero commits/assets/revisions/input changes; real producer still publishes correct bytes/identity/provenance; verified=false is not green integrity. |
| E2E-2 source→VRT→task/delivery | NOT_EXECUTED | GDAL pixel/mask/CRS/grid oracle, source NoData vs output fill, old/new readers, failed publish dependency safety, generation cleanup. |
| E2E-3 ROI→samples→classification/postprocess/evaluation | NOT_EXECUTED | X/y/groups/seed/order/NoData independent oracle; pre-allocation budget; cancellation has no partial publication; any chunking is pixel-equivalent. |
| E2E-4 model identity→forward→cancel/unload/reload | NOT_EXECUTED | Real available CPU provider; config isolation, cancellation isolation, live holders and ledger; classes/working set agree. Optional ORT/GPU absence is explicitly NOT_EXECUTED. |
| E2E-5 workflow→checkpoint→resume→cache delivery | NOT_EXECUTED | Relative checkpoint survives CWD change; lock/attempt/provenance correct; stable AssetId recovery; unchanged hit does not bump revision. |
| Affected legacy regressions | NOT_EXECUTED | Actual CTest discovery first; no 0-match, NOT_BUILT, DISCOVERY_FAILED or environment skip counted as pass. |
| Negative paths | NOT_EXECUTED | Relevant cancel/failure/restart/reopen cases, controlled synchronization for high-risk races. |
| Independent R1 | NOT_EXECUTED | Clean-context reviewer reads complete base..HEAD diff, callers, tests and evidence. |
| Fix + independent R2 | NOT_EXECUTED | All reproducible P0/P1 and introduced blocking P2 resolved or evidence-backed rebuttal; final HEAD reviewed. |
| Final upstream/rebase/diff check | NOT_EXECUTED | Refresh master/PRs/file ownership, affected tests after base change, `git diff --check`, clean staging. |
| PR delivery | NOT_EXECUTED | Push only this branch and create PR with truthful commands/counts/limits/review SHA; no merge or CI wait. |

Online CI is intentionally not awaited and will not be described as passed.
