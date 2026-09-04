# Permissions & Trust Model

Declarations make high-risk capability explicit before a plugin runs. This
is a declaration + policy + diagnostics model, not an OS sandbox.

## Permission vocabulary

`filesystem_read`, `filesystem_write`, `network`, `external_process`,
`python`, `gpu`, `project_mutation`

## Capabilities imply permissions

| capability | implies |
|---|---|
| `data_provider` | `filesystem_read` |
| `model_runtime` | `gpu`, `filesystem_read` |
| `agent_tool` | `filesystem_read` |
| `external_tools` | `external_process`, `filesystem_read` |
| `python_processing` | `python` |

A capability whose implied permission is missing from `permissions` produces
a **warning** diagnostic at validation time.

## Policy

| env var | effect |
|---|---|
| `SICNU_PLUGIN_POLICY` | `audit` (default): record; `enforce`: refuse plugins exercising undeclared permissions |
| `SICNU_PLUGIN_BLOCK` | comma-separated plugin ids that never load (`E5004`) |
| `SICNU_PLUGIN_ALLOW` | when set, ONLY these ids load (`E5004`) |
| `SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY` | refuse third-party native binaries (`E5002`); Python/manifest plugins still load |

## Trust classification

| origin | trust | runtime |
|---|---|---|
| `builtin` (application payload) | trusted | in-process allowed |
| `system` / `user` | third-party | in-process, gated by policy |
| external dirs (`SICNU_PLUGIN_PATH`) | untrusted | validate/doctor only |

## Credentials

The SDK never logs environment values, never stores credentials in project
files, and data-provider `open()` results are plain path references —
authentication hooks are the provider's responsibility using the existing
auth/settings mechanisms. The default child-process environment baseline
exists precisely so secrets in the parent environment do not leak into
external tools.
