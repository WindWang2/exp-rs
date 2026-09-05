# Plugin Manifest v1 Reference

Every plugin is a directory containing `plugin.json` (UTF-8 JSON). Unknown
optional fields are ignored; unknown major versions are rejected with
`E1005 ManifestUnknownVersion`.

## Example

```json
{
  "manifest_version": 1,
  "id": "org.example.plugin",
  "name": "Example Plugin",
  "version": "1.0.0",
  "api_version": "3.0",
  "abi_version": 1,
  "description": "What the plugin provides",
  "vendor": "Example Institute",
  "license": "MIT",
  "platforms": ["linux"],
  "entrypoint": "libplugin.so",
  "entrypoint_kind": "native",
  "capabilities": ["operator", "agent_tool"],
  "permissions": ["filesystem_read"],
  "dependencies": ["org.other.plugin@^1.0"],
  "operators": [ { "...": "see below" } ],
  "agent_tools": [ { "...": "see below" } ]
}
```

## Required fields

| field | type | rules |
|---|---|---|
| `manifest_version` | int | must be `1` |
| `id` | string | reverse-DNS, lowercase, `[a-z0-9-]` labels |
| `name` | string | non-empty display name |
| `version` | string | semver `MAJOR[.MINOR[.PATCH]]` |
| `api_version` | string | `MAJOR.MINOR`, gate vs host (`E2001`) |
| `abi_version` | int | must equal host ABI (`E2002`) |

## Entry points

| `entrypoint_kind` | `entrypoint` | meaning |
|---|---|---|
| `native` (default) | shared library file name | dlopen'd; must export `EXPRS_createPluginV1()` |
| `python` | — | `python.module` is imported inside the isolated Python worker |
| `manifest` | — | no code payload; every operator must carry an `external` section |

Native entrypoints that declare `"ui"` capabilities may additionally export
`EXPRS_createUiContributionV1()` (see `exprs/plugin_ui.h`).

## Contributions (manifest = discovery index)

All contribution arrays are the **discovery-time index**: catalogs render
from the manifest without loading the binary. The conformance kit
(`plugin doctor`) cross-checks binary-provided data against these entries.

### `operators[]`

```json
{
  "id": "vendor:name",
  "display_name": "Human name",
  "group": "catalog group",
  "description": "short description",
  "memory_policy": "full_raster",
  "determinism_grade": "bit_exact",
  "supports_cancel": true,
  "inputs":  [ { "name": "input", "type": "raster", "required": true } ],
  "outputs": [ { "name": "output", "type": "raster", "required": false } ],
  "external": { "argv": ["tool", "${input}"], "timeout_seconds": 600 }
}
```

Port `type` uses the processing DataType strings: `any raster vector table
number integer string boolean enum bbox crs json`.

The optional `external` section turns the entry into a pure-manifest
external-tool operator: `${name}` placeholders substitute parameters,
`${plugin_dir}` the plugin directory, and declared `outputs` are redirected
to temp files and published (renamed) only after a clean exit. See
[external-process.md](external-process.md).

### `data_providers[]`, `model_runtimes[]`, `agent_tools[]`, `ui`, `cartography`

```json
"data_providers": [
  { "id": "vendor:store", "display_name": "Store", "schemes": ["mydb://"] }
],
"model_runtimes": [
  { "framework": "myrt", "display_name": "My Runtime", "gpu": false }
],
"agent_tools": [
  { "id": "vendor:tool", "display_name": "Tool",
    "input_schema": { "type": "object", "properties": {} },
    "description": "Bounded, structured tool" }
],
"ui": { "dock": true, "dock_title": "My Panel", "settings_page": true },
"cartography": { "layout_items": false }
```

## Permissions

See [permissions.md](permissions.md). Declaring a capability whose implied
permission is missing produces a warning; the policy decides whether that
blocks loading (`SICNU_PLUGIN_POLICY=enforce`).

## Diagnostics

Every failure maps to a stable code group:

| group | codes | meaning |
|---|---|---|
| E1xxx | 1001–1007 | manifest structure/fields |
| E2xxx | 2001–2004 | API/ABI/manifest/platform compatibility |
| E3xxx | 3001–3006 | entrypoint, dependencies, resources |
| E4xxx | 4001–4004 | symbol/load/init/registration failures |
| E5xxx | 5001–5004 | permission/trust/policy |

`plugin validate <dir>` prints them (add `--json` for the structured form).
