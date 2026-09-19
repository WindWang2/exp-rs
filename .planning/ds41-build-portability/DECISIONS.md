# DECISIONS — ds41-build-portability (Track D2)

## D1 — Dependency Doctor split: configure-time vs runtime

**Decision.** The doctor added by this track is *configure-time* (CMake):
actionable `find_package` failure guidance plus an end-of-configure dependency
summary. It does not probe a running process, GDAL drivers, or proj.db — that is
Deployment 11.0's runtime env-doctor (`src/geospatial/doctor/env_doctor.*`).

**Why.** Overlap would create two authorities for "is my environment healthy".
The failure modes are disjoint: configure-time = toolchain/SDK discovery
(missing package, wrong version, wrong mode); runtime = deployment bundle.

**Alternatives rejected.** (a) Extending the runtime doctor — wrong phase, needs
a built binary; (b) replacing the CMake errors with a Python preflight — adds a
second configuration entry point and can drift from CMake's actual view.

## D2 — Wrapper shape: one entry point, subcommands, hermetic selftest

**Decision.** A single `scripts/build.sh` (POSIX) and `scripts/windows/build.cmd`
(Windows) with subcommands `doctor|configure|build|test|smoke|selftest|preset-check`,
and a `selftest` that needs no compiler/cmake so portability and cap oracles are
cheap and hermetic. Windows delegates toolchain probing to the landed
`scripts/windows/_env.cmd` and mirrors `setup.cmd`'s configure flags.

**Why.** One documented entry point is what "固化 -j1/-j2 纪律为开发工具" means;
a hermetic selftest makes the path/space/Unicode oracle runnable on any host,
including this one, in seconds.

**Alternatives rejected.** (a) Only presets (`--preset`) — presets cannot express
log collection, failure summaries, or the refusal policy; (b) wrapping only
Ninja — the repo also configures with other generators on macOS/Linux.

## D3 — Cap policy: refuse above cap, don't silently clamp

**Decision.** Effective jobs = explicit `--jobs/-SICNU_BUILD_JOBS` if in 1..2,
else default 1. A request above the cap exits 2 with the cap printed.
Inherited `CMAKE_BUILD_PARALLEL_LEVEL`/`CTEST_PARALLEL_LEVEL` above the cap are
clamped (env may be stale) — clamped values are logged.

**Why.** Exceeding the cap must be impossible; silently accepting `--jobs 8`
would violate the resource discipline, while silently clamping *explicit* intent
hides mistakes. Clamping inherited env matches "protect the host" intent.

**Alternative rejected.** Hard-clamp everything (masks user error).

## D4 — Feature probes: compile checks, never version guesses

**Decision.** New `cmake/SicnuFeatureProbes.cmake` uses
`check_cxx_source_compiles` against the *actually found* headers/libs; results
are `SICNU_HAVE_*` cache vars + a generated `sicnu_feature_probes.h`. Missing
headers ⇒ probe skipped, macro 0, status message — never a configure error.

**Why.** #1108's lesson: version-number ifdefs guessed wrong (GDAL 3.8.4 vs 3.9
API). Compile checks are the only portable truth; they also give the "test fails
before, passes after" property the track requires.

**Alternative rejected.** Version-range tables (`GDAL_VERSION VERSION_LESS …`):
reproduce the #1108 class of bug whenever a distro patches an API.

## D5 — Do not touch `src/`; #1108 pattern stays authoritative

**Decision.** No business-source edits. The GDAL quiet-error idiom landed in
#1108 is portable by construction; WP3 generalises *probing*, not that code.

**Why.** Track boundary; avoids conflicting with the CI-portability fix.

## D6 — Presets: additive, cap-pinned, offscreen test env

**Decision.** Keep all five existing presets byte-compatible. Add: an
`offline-lab` configure preset (`SICNU_LAB_PROFILE=ON` via the landed
`cmake/SicnuLabProfile.cmake`), `jobs: 1` on build presets (2 where the Windows
lane already pins 2 — actually: cap presets at the global cap 2, dev default 1),
and `environment: QT_QPA_PLATFORM=offscreen` + `jobs: 1` on test presets.
`binaryDir` stays in-source-relative (`build-*` are gitignored) to avoid churn
in setup.cmd which expects `build-dev`.

**Why.** Preset hygiene without breaking the landed Windows scripts' paths and
without a "must delete build-dev" migration.

## D7 — Smoke test reuses the installed vcpkg tree instead of the network

**Decision.** The clean-tree smoke gate configures with
`-DVCPKG_MANIFEST_MODE=OFF -DVCPKG_INSTALLED_DIR=<installed tree>` on this host
(and documents the manifest-mode path for CI where the bootstrap is expected).
No gate ever downloads dependencies (non-goal: "不在脚本里自动下载未锁版本的大依赖").

**Why.** Hermetic, offline, repeatable on this host; still a genuine
from-empty-dir configure.
