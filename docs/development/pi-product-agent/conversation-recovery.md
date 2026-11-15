# Product conversation recovery

Pi history belongs to one conversation in one saved project. The project binding
uses the canonical project filename; an unsaved project has only a dock-lifetime
identity. Moving the project creates a different binding. The native backend
retains its existing behavior during migration.

After a successful round, both the SDK and the current native tool host must be
idle before a checkpoint can be requested. The Node wrapper validates the locked
SDK format, branches, compaction and complete tool call/result pairs. Qt stores
the bounded checkpoint and its final JSON SHA-256 through the existing atomic
sidecar writer in `QStandardPaths::AppDataLocation/pi-conversations`. Settings
contain only the conversation ID. Model profiles and credentials keep using the
existing configuration and secret store. The current API credential is scrubbed
before capture is delivered; this does not promise to redact arbitrary secrets
that a user deliberately places in conversation text.

The product host selects tools with language-stable contracts. QGIS
`ProviderAlgorithmAdapter` tools use translated descriptions and enum values,
so they are excluded from this bounded product-agent surface; they remain
available in the Processing toolbox and standalone MCP catalog. Python, core
native operators, spatial inspection, discovery and view tools retain their
complete schemas and strict revision checks. Switching UI language therefore
does not require weakening enum or checkpoint validation. Checkpoints created
with an earlier, different tool selection fail the existing binding check;
start a new conversation rather than rewriting their saved revision.

The checkpoint limit is 1 MiB. Saving has a ten-second protocol deadline and
blocks the next prompt until its result is known. Overflow, unsupported state or
a binding mismatch fails explicitly rather than truncating history. An earlier
checkpoint is kept on failures before publication. If publication succeeded but
verification failed, the UI reports that uncertainty without promising an old
version remains available.

Choose **Restore conversation** to initialize history. Restoration never sends
a model prompt, restores an approval or replays a task. A new explicit prompt
continues the same SDK history. UI language is supplied per turn, so English
history can be restored from the Chinese interface. Model settings changed
during an existing session take effect when **Clear conversation** or an explicit
**Restore conversation** initializes a new SDK session. Restore keeps completed
history and uses the current model settings. It requires an idle native host;
an ongoing write is never switched to another model. The profile controls
explain which settings the current session uses.

An incomplete-run marker is saved before admitting a prompt. Safe native
execution references are observed and merged into that same atomic file. On
restore, the dock queries the current Task Center; process-boot and job IDs
prevent a reused numeric task ID being mistaken for an older execution. Known
active tasks and pipeline workers prevent acknowledgement. Missing, foreign or
not-yet-recorded references remain explicitly **unknown**, including after an
application restart. They are never interpreted as zero executions or success.
**Acknowledge and restore** acknowledges those observations and loads earlier
completed history; it does not resume or retry the interrupted operation.

Deterministic verification uses the locked SDK against a loopback model fixture,
the Qt protocol/store regressions and real Python NDVI delivery. Actual cloud
model compatibility and relocated Windows application evidence are recorded
separately in the implementation ledger.
