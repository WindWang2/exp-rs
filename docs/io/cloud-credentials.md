# Cloud Object Access & Credential Safety Boundary (8.0)

Track: Cloud-Native Geospatial Data Fabric 8.0 (package G).
Authority: ADR 0139 (GDAL VSI is the only network stack; CPL owns credentials).
This page documents the provider-neutral boundary this layer commits to — it
introduces no new credential store and no provider SDK.

## Where credentials live — and nowhere else

1. **GDAL/CPL configuration** — the standard mechanisms (`AWS_ACCESS_KEY_ID` /
   `AWS_SECRET_ACCESS_KEY` / `AWS_REGION` environment variables, `GDAL_HTTP_*`
   options, `~/.aws/credentials` via CPL's AWS config loader, `CPL_CREDENTIAL_*
   ` config options). The foundation layer configures bounded *network
   behavior* (timeouts, retries, byte budgets) through the existing helpers;
   it never reads, writes, stores, or relays credential values.
2. **Signed URLs** — credentials carried in the URL query (S3/Google/Azure
   presigned forms) are treated as follows:
   - **Requests** use the raw URL (CPL/CURL).
   - **Identity** (`remoteIdentityToken`) strips credential-shaped query keys
     from the token basis — a re-signed URL of the same object yields the
     same identity token.
   - **Display/log/report surfaces** use `ResourceUri::display()`: userinfo
     masked, credential-shaped query values masked (ADR 0135 vocabulary:
     token/signature/key/password/… patterns, case-insensitive).

## Where credentials must never appear

| Surface | Guarantee | Enforced by |
|---|---|---|
| Dataset/project stores | no credential fields | data-layer schema (no free-text source URLs persisted with credentials) |
| Execution fingerprints / cache keys | digests only | `remoteIdentityToken` is a SHA-256 over a credential-stripped basis |
| Telemetry & doctor reports | redacted display URL only | `RemoteSourceIdentity::toJson` (display form) |
| Help / diagnostics | never echoes argv of identity URLs | `data identity` reports display forms |
| Range-cache keys | canonical URL with credentials removed | `remoteIdentityToken::identityUrl`; cache keys key on `ResourceUri::canonical()` per ADR 0135 (identity form, never displayed) |
| Test fixtures | loopback only, synthetic payloads | `tests/support/http_range_server.h` (no public-network dependency) |

## Provider-neutrality statement

The layer's remote contracts (`RemoteSourceValidator`,
`remoteIdentityToken`, `/vsirangecache/`) operate on **http(s) URLs and VSI
spellings** — `https://host/object`, `/vsicurl/…`, `/vsis3/…`, `/vsigs/…`,
`/vsiaz/…`. Provider semantics (signing, region resolution, retries) stay
inside GDAL/CPL. A provider whose GDAL driver is absent is
**capability-gated honestly**: capability queries answer *unavailable*, and
nothing claims support that the running stack cannot execute.

## Honest limits

- Identity tokens are provable **only** for origins serving strong ETags.
  Weak validators and Last-Modified-only origins are reported as weak —
  cache reuse stays disabled for them (fail-closed).
- A token proves *content identity*, not *authorization*: an expired signed
  URL fails at read time even though its token was derivable while the probe
  succeeded.
- The layer never caches credentials, never logs them, and provides no CLI
  surface that accepts them.
