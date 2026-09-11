# Plugin Packages (local distribution)

A package is a plugin directory: `plugin.json` + payload (native library,
python package, `resources/`, licenses). v1 packages are plain directories;
an archive format would be a manifest-versioned extension, not a format
break.

## Lifecycle

```bash
sicnu_geo_rs_cli plugin install ./my-plugin        # validates, copies into user root
sicnu_geo_rs_cli plugin list                       # state: validated | disabled | ...
sicnu_geo_rs_cli plugin disable org.example.demo   # persisted in plugins.index.json
sicnu_geo_rs_cli plugin enable org.example.demo
sicnu_geo_rs_cli plugin uninstall org.example.demo
```

Install root: `~/.local/share/sicnu_geo_rs/plugins/<plugin-id>`.

## Install rules

1. The manifest is validated (full validator) **before** anything is copied.
2. Id takeover protection: the target directory is refused when it hosts a
   manifest declaring a **different** id.
3. Path-escape protection: only regular files and real subdirectories are
   copied; symlinks/devices are refused; every target path is verified to
   stay inside source and destination (zip-slip style checks).
4. Re-installing the same id replaces the previous payload (read-modify of
   other plugins is never required).
5. **Entrypoint containment is re-checked at load time** (`E3007`): a
   manifest whose `entrypoint` is absolute, contains `..`, or resolves
   through a symlink outside the plugin root is refused at validation AND
   immediately before the library is mapped — install-time validation alone
   is never trusted (issue #756; see ADR 0130).

## Dependency constraints (plugin-platform 9.0)

`plugin install` probes each declared `"dependencies": ["<id>@<range>"]`
constraint against the installed set and records a diagnostic per
constraint: INFO when satisfied, a typed E3003 WARNING when not.
Install PROCEEDS either way — install order is the user's business — while
the load-time gate remains the enforcement point.

Supported ranges (semver-ish, npm caret semantics): `^X.Y.Z` (same major;
`0.x` bounds pin the minor, `0.0.x` bounds pin the patch), `~X.Y.Z` (same
minor), `>=X.Y.Z`, `=X.Y.Z`, `X.Y.Z` (exact) and a bare plugin id (any
version). An unparsable range is satisfied by nothing (fail closed).

**Honest scope**: the probe is ADVISORY. Nothing enforces dependencies at
load time — the loader validates the dependency spec SYNTAX only. A plugin
with unsatisfied dependencies installs, loads and fails at its own
integration seam; the diagnostic exists so tooling and users see the gap
early.

## Interrupted installs

Staging leftovers from crashed installs are swept automatically (older than
24 h, or the current process's own directory at install start). A failed
upgrade never touches the previous-good install (staged install + atomic
swap + rollback).

## Enable/disable

`plugins.index.json` next to the user plugin root records disabled ids.
Disabled is a registry state (`E5003` in diagnostics), distinct from
broken (`E1xxx`) or incompatible (`E2xxx`) — the Plugin Manager and
`plugin list` show all three. Disable unloads a loaded plugin (refused with
`E4005 PluginInUse` while it is still executing); enable re-loads and
re-attaches its contributions — no restart (ADR 0130).

## Conformance

```bash
sicnu_geo_rs_cli plugin test ./my-plugin    # PT_* checks, JSON with --json
```

Exercises manifest schema (PT_MANIFEST), API/ABI/platform compatibility
(PT_COMPAT), entrypoint containment (PT_CONTAINMENT), load (PT_LOAD),
operator registration (PT_REGISTER), unload revocation (PT_REVOKE) and the
enable round-trip (PT_ROUNDTRIP); non-zero exit when any check fails.

## Scaffolding

```bash
python3 scripts/exprs_new_plugin.py --kind external_tool \
    --id org.acme.ndvi-tool --name "NDVI Tool" --out ~/plugins
```

Generates a minimal conformance-shaped plugin for one of: `cpp_operator`,
`python_operator`, `external_tool`, `model_runtime`, `data_provider`,
`agent_tool`, `ui_contribution`.
