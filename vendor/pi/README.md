# Vendored Pi SDK closure (P0)

`runtime.lock.json` pins Pi **1.5.2**, upstream commit
`b4afdddf44e059c67ccf5097e5317d913521b470`, the published SDK integrity and
the reviewed AuthContext patch manifest. `package-lock.json` locks the complete
production dependency graph. Node is independently pinned to **24.22.3** for
Windows x64 in `vendor/node/runtime.lock.json`. Other platforms are currently
explicitly unsupported by this preparation recipe.

This follows the managed-Python vendor convention: recipes, provenance, locks,
patches and licenses are reviewed in Git; large fetched artifacts remain in a
build cache. The installed application receives the actual verified Node and
production SDK files. It never searches for a global Node/Pi or installs npm
packages at startup.

## Prepare and verify

Use a build Python interpreter to prepare a fresh install-prefix tree:

```sh
python scripts/prepare_pi_runtime.py --destination build-release/pi-runtime
python scripts/prepare_pi_runtime.py --verify-stage build-release/pi-runtime
python scripts/prepare_pi_runtime.py --smoke build-release/pi-runtime
```

`--node-archive` accepts an already fetched official ZIP and still checks the
pinned digest; it never permits another executable or version.
`--cache-dir` selects the private build cache (default
`build-logs/pi-vendor-cache`). `--offline` refuses both Node downloads and npm
registry access, and therefore requires a previously populated cache. Cached
Node archives are always checked against their official SHA-256; npm `ci`
consumes the committed lock with exact registry artifact integrity. Corrupt
cached archives are refused rather than silently replaced.

Preparation extracts the pinned official Node ZIP into owned scratch space and
uses its npm CLI with `--ignore-scripts --omit=dev --include=optional`, no bin
links, no audit/fund/update checks, and empty explicit user/global npm configs.
It does not run package lifecycle scripts. The complete production dependency
tree is retained, including WASM, native prebuilds, model data and notices; SDK
imports alone are not evidence that a pruned single-file bundle is complete.

The patch is applied as exact before/after SHA-256 transformations from
`patches/manifest.json`, then all payload files, dependency versions, source
recipes and patch identities are recorded. Preparation uses a fresh sibling
scratch directory and publishes by atomic rename only after verification.
An existing destination is verified, never overwritten. Verification rejects
modified, missing or extra component files and a stage made from another
source lock or native target. Inventory traversal refuses symlinks and Windows
reparse points, including prefix/component roots and their ancestors, before
descending; it does not follow a payload junction into another directory.
`--verify-stage` does not execute Node or npm.

Paths in a staged/installed prefix:

- `libexec/sicnu-geo-rs/node/`: `node.exe`, Node license and release notices.
- `libexec/sicnu-geo-rs/pi-agent/node_modules/`: the complete locked production
  SDK closure, with package licenses/resources preserved.
- `libexec/sicnu-geo-rs/pi-agent/package{,-lock}.json`: installed dependency
  identity. npm itself is only a preparation tool and is not shipped here.
- `share/sicnu-geo-rs/pi/`: exact source recipes, patches and per-package
  license/provenance index.
- `share/sicnu-geo-rs/pi-runtime.json`: complete payload receipt.

## What P0 proves

`--smoke` verifies the stage, uses the packaged Node with an isolated temporary
environment, and imports the actual public SDK/AI exports. It does not create a
model session or make a model request. The separate controlled-session harness
must validate resource/auth isolation, provider compatibility, tool selection,
streaming, cancellation and settled-state semantics before product integration.

The SDK still has the current user's OS permissions. Disabled tools and an
AuthContext patch do not establish an operating-system sandbox. Product SDK
sessions must only proxy tools to the existing Qt host; no Pi Shell/file tools,
implicit resource discovery or second `--mcp` execution instance are enabled by
this preparation recipe. This P0 stage deliberately contains no product UI or
runtime wrapper, no CMake install component, and no claimed live-model/NDVI
acceptance. Those are later phases of the approved implementation plan.

## Upgrade

Update the exact Node release/checksum, Pi source SHA and npm dependency lock in
one reviewed change. Regenerate and review patch before/after hashes against
that version; never fuzzy-apply a patch. Rebuild from a fresh cache, run negative
integrity tests and relocated/offline SDK/session probes, then carry the full
payload/notices through application packaging and bilingual NDVI acceptance.
Do not ship `latest` dependencies or update the model catalog/runtime at user
startup.
