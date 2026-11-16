# Pi ModelRuntime AuthContext patch

This patch applies only to `@earendil-works/pi-coding-agent@1.5.2`, from
upstream commit `b4afdddf44e059c67ccf5097e5317d913521b470`. The package tarball
integrity and every original/modified file hash are recorded in `manifest.json`.
The upstream source is MIT licensed; retain its license with the complete
runtime's third-party notices.

## Public behavior

`ModelRuntime.create({ authContext })` now forwards that optional context to
pi-ai's existing public `createModels({ authContext })` interface. Product code
can inject environment/file discovery functions that return `undefined`/`false`,
while explicitly supplied in-memory provider credentials continue to work.
Omitting this option preserves upstream ambient discovery behavior.

An empty credential store alone is insufficient: registering a provider refreshes
availability of the built-in providers. Before this patch, the supplied
`authContext` was ignored, so synthetic `OPENAI_API_KEY` and ADC fixture files
could authorize providers despite the product's denied discovery policy.

This is an interface passthrough, not a provider allowlist, a filesystem sandbox,
or a network policy. Product session configuration must independently control
tools, resources, model catalogs, telemetry, retry and cache warming.

## Source and published distribution

- `0001-model-runtime-auth-context.source.patch` is the reviewable change against
  the fixed upstream TypeScript source, suitable for an upstream proposal.
- `0001-model-runtime-auth-context.dist.patch` makes the corresponding change to
  the official npm `dist/core/model-runtime.js` and `.d.ts`. This avoids building
  the entire unrelated monorepo during runtime preparation.
- `manifest.json` provides exact single-occurrence replacements and SHA-256
  preconditions/postconditions. The preparation script must reject a different
  input, an already-patched input or a missing/ambiguous replacement. Never
  silently patch a newer release.

The manifest replacements preserve original UTF-8/LF bytes independently of Git
configuration. For manual unified-patch verification, use
`git -c core.autocrlf=false apply`; enabling Windows newline conversion changes
the declared output hashes and must not be used to produce runtime artifacts.

The two modified files no longer reference their old source maps. Original map
assets remain unmodified in the dependency inventory, but Node/TypeScript will
not use them for these patched files. This avoids misleading mapped stack traces
without pretending that the unmodified published maps describe changed code.

## Regression seam

`pi/product-runtime/test/auth-context.test.mjs` crosses the real published
`ModelRuntime.create/registerProvider/getAvailable` interface in an owned child
process. Its HOME, ADC, environment keys and credential store are all synthetic.
It requires an explicit absolute `SICNU_TEST_PI_NODE_MODULES` root and the pinned
Node executable. The original package should fail because built-in OpenAI/Vertex
become available and the injected resolver is not consulted; the patched package
must reject those ambient credentials while retaining the host's memory-only
custom model.

Run the same test against an unmodified dependency fixture and then against the
prepared patched stage. Record both actual results; the presence of this patch
or a source-text check is not a substitute for that red/green verification.
No runtime test result is claimed by this README.
