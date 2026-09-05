# API / ABI Versioning Policy

Three independent axes (declared in `exprs/version.h`):

| Axis | Where declared | Gate |
|---|---|---|
| Plugin **API** version | `api_version: "3.0"` in the manifest | same MAJOR, plugin MINOR ≤ host MINOR |
| Plugin **ABI** version | `abi_version: 1` in the manifest | exact equality, checked **before dlopen** |
| **Manifest** version | `manifest_version: 1` | major must be 1; unknown optional fields ignored |

## What "API minor bump" means

Strictly additive: new optional virtual methods at the END of a versioned
interface (with default implementations), new manifest fields, new capability
kinds, new diagnostic codes (appended). A plugin compiled against 3.0 loads
into a 3.1 host. The reverse is refused with `E2001 ApiVersionMismatch`.

## What "ABI bump" means

The C++ binary contract of the interface headers changed without an
API-visible change: vtable layout, base-class layout, or a pinned third-party
type changed meaning. C++ ABIs are NOT assumed stable — every ABI bump
requires a plugin rebuild against the new SDK. The loader reads the manifest
first; a mismatch yields `E2002 AbiVersionMismatch`, never a dlopen of an
incompatible binary.

## Pinned cross-ABI types

The plugin interfaces use exactly these types across the boundary:

- `Json::Value` (jsoncpp) — the operator result/parameter contract
- `std::string`, `std::vector`, `std::function`, `std::shared_ptr/unique_ptr`

`std::string`/STL types imply same-toolchain plugins (same GCC major /
libstdc++). This is the documented support model for in-process native
plugins on the supported Linux platform; stricter isolation (out-of-process
plugin host) is described in [../plugins/isolation.md](../plugins/isolation.md).

Qt types are confined to `exprs/plugin_ui.h`, which is build-locked to
the host application (same Qt minor + same app build) and explicitly NOT a
long-term ABI surface.

## Interface evolution rules (V1)

For every `*V1` interface (`PluginV1`, `ContributionContextV1`,
`HostServicesV1`, `IPluginDataProviderV1`, `IPluginAgentToolV1`,
`IPluginModelRuntimeV1`, `UiContributionV1`):

1. Never reorder, remove, or change the signature of existing virtuals.
2. Additions go at the END and must carry a default implementation.
3. The entry point symbols are version-suffixed (`EXPRS_createPluginV1`).
   An incompatible V2 is a NEW symbol, not a change.
4. Enum values (`PluginDiagnosticCode`, permissions, states) are append-only.

## Compatibility gate mechanics

The gate runs on the manifest (a JSON file), so incompatible plugins are
identified **without loading any plugin binary** (the `plugin doctor`
symbol probe dlopens the library and therefore runs its ELF initializers —
that probe is a diagnostic, not the gate):

```
plugin record: Validated → (api/abi/platform checks) → loadable | Incompatible (E2001/E2002/E2004)
```

`exprs::isPluginApiCompatible(host, declared)` in `exprs/version.h`
is the single decision function — the CLI `plugin doctor` command and the
conformance kit call the same code the loader calls.
