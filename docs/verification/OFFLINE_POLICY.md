# Offline Policy — local verification without a network

* Status: in force (D10 verification-baseline-green)
* Contract: [ADR 0146](../adr/0146-offline-degradation-contract.md)
* Applies to: every transport-dependent test binary, the verification ladder,
  the READINESS report, and the teaching-lab build profile.

## Principle

`not-built`, `skipped`, `timeout`, `failed` and `passed` are distinct verdicts
and stay distinct. An environment that cannot run a suite must produce an
**explained skip** — never a timeout, never a silent pass. `timeout` is never
an acceptable steady state: if a suite needs a transport and the transport is
missing, the suite must *detect* that with bounded probes and report
`skipped` with a machine-readable reason.

## What every transport-dependent test binary must do

Embed the guard from `tests/support/offline_probe.h`:

```cpp
#include "support/offline_probe.h"
SICNU_OFFLINE_GUARD()
```

The guard runs once when the test run starts (not during `--list-tests`), and:

1. **`SICNU_FORCE_OFFLINE=1`** → skip with reason `forced-offline`. This is how
   the skip path itself is exercised on healthy machines.
2. **Proxy hygiene** → `127.0.0.1,localhost` are appended to `no_proxy`/`NO_PROXY`.
   Loopback fixtures never need a proxy; this *fixes* the machine-room proxy
   failure class instead of skipping it.
3. **Bounded loopback TCP probe** → bind → listen → connect → accept on
   `127.0.0.1`, every stage under a `poll()` deadline. Failure ⇒ skip with
   reason `loopback-unavailable` (detail names the failing stage).

On skip the binary prints `sicnu-skip: <reason-code>` on stdout and exits **77**.

```console
$ SICNU_FORCE_OFFLINE=1 ./test_io_remote_range
sicnu-skip: forced-offline
$ echo $?
77
```

(The guard fires before Catch2 flushes its banner, so stdout shows only the
sentinel line; the human-readable detail goes to stderr as
`sicnu-skip-detail: SICNU_FORCE_OFFLINE is set`.)

## The three remote I/O suites are loopback suites

`test_io_range_cache`, `test_io_remote_range` and `test_io_remote_validator`
exercise the production remote-transport paths (probe, range cache, conditional
validators) against `tests/support/http_range_server.cpp` — a range-capable
HTTP fixture bound to `127.0.0.1`. They require **no internet access**; their
historical `timeout` verdicts came from hostile proxy environments (see ADR 0146),
not from the code under test. They are, therefore, the local substitute
evidence for remote I/O: no path goes permanently unverified when a network is
absent — the suites either run (loopback is enough) or skip with a reason.

## Verification ladder / READINESS integration

* `scripts/verification_ladder.py` maps the exit-77 contract to the `skipped`
  verdict (reason in `detail`) for directly-run test binaries.
* `scripts/collect_readiness.py` prints `skipped (<reason>)` per capability;
  the reason text is produced (and guaranteed) by the ladder.
* READINESS regeneration on this host targets `failed=0`, `timeout=0`; any
  `skipped` entry carries its reason code.

## Benchmarks

* `--bench-quick` — smoke tier: fixed, small, non-overridable iteration counts;
  declared budget: well under 120 s single-threaded; artifact labelled
  `"tier": "quick"`. This is the routine tier.
* Full tier (no flag) — the documented scales; opt-in evidence for meaningful
  hosts. Benchmarks remain evidence snapshots, never gates.

## Teaching-lab builds

`cmake/SicnuLabProfile.cmake`, enabled with `-DSICNU_LAB_PROFILE=ON` (default
OFF), configures a teaching machine build: no vendored OTB/ITK, ONNX Runtime
provider stubbed, no vendored GDAL, no embedded Python, no pybind11 bindings
(their FetchContent is a network dependency), no verification suite. The lab
operator set and the desktop application are unchanged. Measured deltas are in
the D10 PR description.
