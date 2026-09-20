# docs/development — local development build guide

This directory is the developer-facing companion to the build system owned by
the `ds41-build-portability` track. It covers what you need on a fresh machine,
how to configure/build/test without burning the host, and how to read the
diagnostics the build now produces.

* [build-from-scratch.md](build-from-scratch.md) — minimal dependencies per OS
  (Windows/vcpkg, Linux packages, macOS/Homebrew), first-time configure, the
  clean-tree smoke gate, and the common-error → fix table.
* [resource-discipline.md](resource-discipline.md) — the -j1/-j2 policy, the
  `scripts/build.sh` / `scripts\windows\build.cmd` wrappers, and the gates.
* [dependency-doctor.md](dependency-doctor.md) — the configure-time dependency
  doctor (actionable failures, the paste-able dependency summary, the feature
  probes) and how it relates to the runtime env-doctor.

Quick start on any platform:

```sh
scripts/build.sh selftest          # hermetic: no toolchain needed (seconds)
scripts/build.sh dep-fixture-test  # dependency-doctor contract gate
scripts/build.sh preset-check      # CMakePresets.json hygiene assertions
```

Windows uses `scripts\windows\build.cmd` with the same subcommands (it loads
`scripts\windows\_env.cmd` for the toolchain probe and vcvars64 for MSVC).
