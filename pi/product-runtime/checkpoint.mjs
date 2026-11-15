// Draft P3 module. Pure data only: no filesystem, transport, credential or model calls.
// This contract deliberately supports the entry/message types emitted by the
// current controlled 1.5.2 session, rather than arbitrary extension state.
import { isAbsolute } from "node:path";

export const CHECKPOINT_SCHEMA = "sicnu.pi-checkpoint/1";
export const CHECKPOINT_PI_VERSION = "1.5.2";
export const MAX_CHECKPOINT_BYTES = 1024 * 1024;
const MAX_ENTRIES = 16384;
const MAX_JSON_DEPTH = 64;
const MAX_JSON_NODES = 200000;
const RESERVED_TOOLS = new Set(["read", "bash", "powershell", "edit", "write", "grep", "find", "ls", "codemode", "tool_search"]);
const ENVELOPE = ["schema", "piVersion", "checkpointId", "conversationId", "projectId", "completedRunId", "toolRevision", "sdkReceiptSha256", "systemPromptSha256", "status", "header", "entries", "leafId"];
const BINDING_KEYS = ["conversationId", "projectId", "toolRevision", "sdkReceiptSha256", "systemPromptSha256"];
const ENTRY_BASE = ["type", "id", "parentId", "timestamp"];

export class CheckpointError extends Error {
  constructor(code = "CHECKPOINT_INVALID") { super(code); this.name = "CheckpointError"; this.code = code; }
}

function fail(code) { throw new CheckpointError(code); }
function record(value) { return value !== null && typeof value === "object" && !Array.isArray(value); }
function shape(value, required, optional = []) {
  if (!record(value) || required.some((key) => !Object.hasOwn(value, key))
      || Object.keys(value).some((key) => !required.includes(key) && !optional.includes(key))) fail();
}
function text(value, max = MAX_CHECKPOINT_BYTES, empty = true) {
  if (typeof value !== "string" || (!empty && !value) || Buffer.byteLength(value, "utf8") > max) fail();
}
function identity(value) {
  text(value, 256, false);
  if (/[\u0000-\u001f\u007f]/.test(value)) fail();
}
function hash(value) { if (typeof value !== "string" || !/^[a-f0-9]{64}$/.test(value)) fail(); }
function unsigned(value) { if (!Number.isSafeInteger(value) || value < 0) fail(); }
function finite(value) { if (typeof value !== "number" || !Number.isFinite(value) || value < 0) fail(); }
function timestamp(value) {
  if (typeof value !== "string" || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$/.test(value)) fail();
  const parsed = Date.parse(value);
  if (!Number.isFinite(parsed) || new Date(parsed).toISOString() !== value) fail();
}

// Compute the encoded size while copying; do not stringify arbitrary objects
// with toJSON/getters, or duplicate an unbounded transcript before rejecting it.
// Undefined optional SDK object fields are omitted exactly as JSON would omit them.
function cloneBoundedJson(input) {
  let bytes = 0, nodes = 0;
  const ancestors = new Set();
  const charge = (count) => { bytes += count; if (bytes > MAX_CHECKPOINT_BYTES) fail("CHECKPOINT_TOO_LARGE"); };
  const visit = (value, depth) => {
    if (++nodes > MAX_JSON_NODES || depth > MAX_JSON_DEPTH) fail("CHECKPOINT_TOO_LARGE");
    if (value === null || typeof value === "boolean") { charge(value === null ? 4 : value ? 4 : 5); return value; }
    if (typeof value === "string") { charge(Buffer.byteLength(JSON.stringify(value), "utf8")); return value; }
    if (typeof value === "number") {
      if (!Number.isFinite(value)) fail();
      charge(JSON.stringify(value).length); return value;
    }
    if (typeof value !== "object" || ancestors.has(value)) fail();
    const prototype = Object.getPrototypeOf(value);
    if (prototype !== Object.prototype && prototype !== null && prototype !== Array.prototype) fail();
    if (Object.getOwnPropertySymbols(value).length) fail();
    ancestors.add(value);
    let copy;
    if (Array.isArray(value)) {
      charge(2 + Math.max(0, value.length - 1));
      copy = [];
      for (let index = 0; index < value.length; index++) {
        const descriptor = Object.getOwnPropertyDescriptor(value, String(index));
        if (!descriptor || !Object.hasOwn(descriptor, "value")) fail();
        copy.push(visit(descriptor.value, depth + 1));
      }
      if (Object.keys(value).some((key) => !/^(0|[1-9]\d*)$/.test(key) || Number(key) >= value.length)) fail();
    } else {
      charge(2);
      const pairs = [];
      for (const [key, descriptor] of Object.entries(Object.getOwnPropertyDescriptors(value))) {
        if (!descriptor.enumerable) continue;
        if (!Object.hasOwn(descriptor, "value")) fail();
        if (descriptor.value === undefined) continue;
        charge(Buffer.byteLength(JSON.stringify(key), "utf8") + 1 + (pairs.length ? 1 : 0));
        pairs.push([key, visit(descriptor.value, depth + 1)]);
      }
      copy = Object.fromEntries(pairs);
    }
    ancestors.delete(value);
    return copy;
  };
  return visit(input, 0);
}

function toolAllowlist(names) {
  if (!Array.isArray(names) || names.length > 256) fail();
  const allowed = new Set();
  for (const name of names) {
    if (typeof name !== "string" || !/^[A-Za-z0-9_-]{1,64}$/.test(name)
        || RESERVED_TOOLS.has(name) || name.startsWith("mcp__") || allowed.has(name)) fail("CHECKPOINT_TOOL_NOT_ALLOWED");
    allowed.add(name);
  }
  return allowed;
}
function allowedTool(name, allowed) { if (!allowed.has(name)) fail("CHECKPOINT_TOOL_NOT_ALLOWED"); }

function content(value, kinds, allowed) {
  if (typeof value === "string") { if (!kinds.includes("string")) fail(); return; }
  if (!Array.isArray(value)) fail();
  for (const block of value) {
    if (!record(block) || !kinds.includes(block.type)) fail();
    if (block.type === "text") {
      shape(block, ["type", "text"], ["textSignature"]); text(block.text);
      if (Object.hasOwn(block, "textSignature")) text(block.textSignature);
    } else if (block.type === "thinking") {
      shape(block, ["type", "thinking"], ["thinkingSignature", "redacted"]); text(block.thinking);
      if (Object.hasOwn(block, "thinkingSignature")) text(block.thinkingSignature);
      if (Object.hasOwn(block, "redacted") && typeof block.redacted !== "boolean") fail();
    } else if (block.type === "image") {
      shape(block, ["type", "data", "mimeType"]); text(block.data); text(block.mimeType, 256, false);
    } else if (block.type === "toolCall") {
      shape(block, ["type", "id", "name", "arguments"], ["thoughtSignature", "namespace", "argumentsError"]);
      identity(block.id); allowedTool(block.name, allowed);
      if (!record(block.arguments)) fail();
      for (const key of ["thoughtSignature", "namespace", "argumentsError"]) if (Object.hasOwn(block, key)) text(block[key]);
    }
  }
}
function usage(value) {
  shape(value, ["input", "output", "cacheRead", "cacheWrite", "totalTokens", "cost"], ["cacheWrite1h", "reasoning"]);
  for (const key of ["input", "output", "cacheRead", "cacheWrite", "totalTokens", "cacheWrite1h", "reasoning"])
    if (Object.hasOwn(value, key)) unsigned(value[key]);
  shape(value.cost, ["input", "output", "cacheRead", "cacheWrite", "total"], ["totalFromProvider"]);
  for (const key of ["input", "output", "cacheRead", "cacheWrite", "total"]) finite(value.cost[key]);
  if (Object.hasOwn(value.cost, "totalFromProvider") && typeof value.cost.totalFromProvider !== "boolean") fail();
}
function systemMessage(value, allowed) {
  shape(value, ["role", "content", "timestamp"], ["sections", "toolsAdded", "toolsRemoved"]);
  if (value.role !== "system") fail();
  unsigned(value.timestamp); content(value.content, ["string", "text"], allowed);
  if (Object.hasOwn(value, "sections")) {
    if (!record(value.sections)) fail();
    for (const section of Object.values(value.sections)) if (section !== null) text(section);
  }
  if (Object.hasOwn(value, "toolsAdded")) {
    if (!Array.isArray(value.toolsAdded)) fail();
    const seen = new Set();
    for (const tool of value.toolsAdded) {
      shape(tool, ["name", "description", "parameters"], ["constrainedSampling"]);
      allowedTool(tool.name, allowed); text(tool.description);
      if (seen.has(tool.name) || !record(tool.parameters) || tool.parameters.type !== "object") fail();
      if (Object.hasOwn(tool, "constrainedSampling") && tool.constrainedSampling !== false && !record(tool.constrainedSampling)) fail();
      seen.add(tool.name);
    }
  }
  if (Object.hasOwn(value, "toolsRemoved")) {
    if (!Array.isArray(value.toolsRemoved)) fail();
    for (const tool of value.toolsRemoved) { shape(tool, ["name"]); allowedTool(tool.name, allowed); }
  }
}
function nestedCalls(value, allowed) {
  shape(value, ["calls", "complete"]);
  if (value.complete !== true || !Array.isArray(value.calls)) fail("CHECKPOINT_PENDING");
  const seen = new Set();
  for (const call of value.calls) {
    shape(call, ["id", "name", "status"], ["arguments", "argumentsBytes", "durationMs", "error"]);
    identity(call.id); allowedTool(call.name, allowed);
    if (seen.has(call.id) || !["ok", "error"].includes(call.status)) fail("CHECKPOINT_PENDING");
    seen.add(call.id);
    if (Object.hasOwn(call, "arguments") && !record(call.arguments)) fail();
    for (const key of ["argumentsBytes", "durationMs"]) if (Object.hasOwn(call, key)) unsigned(call[key]);
    if (Object.hasOwn(call, "error")) text(call.error);
  }
}
function message(value, allowed) {
  if (!record(value)) fail();
  if (value.role === "system") { systemMessage(value, allowed); return; }
  if (value.role === "user") {
    shape(value, ["role", "content", "timestamp"]); content(value.content, ["string", "text", "image"], allowed);
  } else if (value.role === "assistant") {
    shape(value, ["role", "content", "api", "provider", "model", "usage", "stopReason", "timestamp"],
      ["responseModel", "responseId", "providerThinkingLevel", "thinkingLevel", "diagnostics", "errorMessage", "rawStopReason", "endTurn", "durationMs", "deferred"]);
    if (["pending", "deferred"].includes(value.stopReason) || Object.hasOwn(value, "deferred")) fail("CHECKPOINT_PENDING");
    if (!["stop", "length", "toolUse", "error", "aborted"].includes(value.stopReason)) fail();
    content(value.content, ["text", "thinking", "toolCall"], allowed); usage(value.usage);
    for (const key of ["api", "provider", "model"]) text(value[key], 256, false);
    for (const key of ["responseModel", "responseId", "providerThinkingLevel", "thinkingLevel", "errorMessage", "rawStopReason"])
      if (Object.hasOwn(value, key)) text(value[key]);
    if (Object.hasOwn(value, "diagnostics") && !Array.isArray(value.diagnostics)) fail();
    if (Object.hasOwn(value, "endTurn") && typeof value.endTurn !== "boolean") fail();
  } else if (value.role === "toolResult") {
    shape(value, ["role", "toolCallId", "toolName", "content", "isError", "timestamp"], ["details", "usage", "nestedCalls", "durationMs"]);
    identity(value.toolCallId); allowedTool(value.toolName, allowed);
    content(value.content, ["text", "image"], allowed);
    if (typeof value.isError !== "boolean") fail();
    if (Object.hasOwn(value, "usage")) usage(value.usage);
    if (Object.hasOwn(value, "nestedCalls")) nestedCalls(value.nestedCalls, allowed);
  } else fail();
  unsigned(value.timestamp);
  if (Object.hasOwn(value, "durationMs")) unsigned(value.durationMs);
}

function entryShape(value, fields, optional = []) { shape(value, [...ENTRY_BASE, ...fields], optional); }
function validateToolPairs(messages, requireCompleted) {
  const pending = new Map();
  let lastConversation, hasUser = false;
  for (const current of messages) {
    if (current.role === "system" || current.role === "compactionSummary") continue;
    if (current.role === "user" || current.role === "assistant") {
      if (pending.size) fail("CHECKPOINT_TOOL_PAIRS");
      if (current.role === "user") hasUser = true;
      else for (const call of current.content.filter((block) => block.type === "toolCall")) {
        // Product call IDs are scoped to a run. A provider may reuse an ID in a
        // later completed round, but never for two calls pending together.
        if (pending.has(call.id)) fail("CHECKPOINT_TOOL_PAIRS");
        pending.set(call.id, call.name);
      }
    } else if (current.role === "toolResult") {
      if (pending.get(current.toolCallId) !== current.toolName) fail("CHECKPOINT_TOOL_PAIRS");
      pending.delete(current.toolCallId);
    } else fail();
    lastConversation = current;
  }
  if (pending.size) fail("CHECKPOINT_TOOL_PAIRS");
  if (requireCompleted && (!hasUser || lastConversation?.role !== "assistant" || !["stop", "length"].includes(lastConversation.stopReason))) fail("CHECKPOINT_NOT_COMPLETED");
}

// Validate the data projection defined by the locked public session format:
// newest compaction + its retained range + newer entries, then latest retained
// context edit per target. Restoration itself still belongs to SessionManager.
function validateSelectedContext(active) {
  const compactIndex = active.findLastIndex((entry) => entry.type === "compaction");
  let selected = active;
  if (compactIndex >= 0) {
    const compaction = active[compactIndex];
    const firstKeptIndex = active.findIndex((entry) => entry.id === compaction.firstKeptEntryId);
    selected = [compaction,
      ...active.slice(firstKeptIndex, compactIndex).filter((entry) => entry.type !== "message" || entry.message.role !== "system"),
      ...active.slice(compactIndex + 1)];
  }
  const edits = new Map(selected.filter((entry) => entry.type === "context_edit").map((entry) => [entry.targetId, entry.replacement]));
  const messages = [];
  for (const entry of selected) {
    if (entry.type !== "message") continue;
    if (!edits.has(entry.id)) { messages.push(entry.message); continue; }
    const replacement = edits.get(entry.id);
    if (replacement === null) continue;
    const role = entry.message.role;
    const projectedContent = (role === "assistant" || role === "toolResult") && typeof replacement.content === "string"
      ? [{ type: "text", text: replacement.content }] : replacement.content;
    messages.push({ ...entry.message, content: projectedContent });
  }
  // A valid retain-none compaction may leave only its summary, so completion is
  // established by raw history; this pass checks projected tool-call ordering.
  validateToolPairs(messages, false);
}

function validateManagerContext(manager, allowedNames) {
  // Live SDK messages keep own optional properties with value undefined (for
  // example a host result's usage). Validate their JSON form, just as capture
  // validates serialized entries, without weakening the persisted schema.
  const context = cloneBoundedJson(manager.buildSessionContext());
  if (!record(context) || !Array.isArray(context.messages)) fail();
  const allowed = toolAllowlist(allowedNames);
  for (const value of context.messages) {
    if (value.role === "compactionSummary") continue;
    message(value, allowed);
  }
  validateToolPairs(context.messages, false);
}

function validateEntries(checkpoint, allowed) {
  const { entries, leafId } = checkpoint;
  if (!Array.isArray(entries)) fail();
  if (entries.length > MAX_ENTRIES) fail("CHECKPOINT_TOO_LARGE");
  const byId = new Map();
  for (const entry of entries) {
    if (!record(entry)) fail();
    identity(entry.id); if (entry.parentId !== null) identity(entry.parentId); timestamp(entry.timestamp);
    if (byId.has(entry.id) || entry.id === checkpoint.header.id) fail("CHECKPOINT_INVALID_TREE");
    byId.set(entry.id, entry);
    switch (entry.type) {
      case "message": entryShape(entry, ["message"]); message(entry.message, allowed); break;
      case "thinking_level_change":
        entryShape(entry, ["thinkingLevel"]);
        if (!["off", "minimal", "low", "medium", "high", "xhigh"].includes(entry.thinkingLevel)) fail();
        break;
      case "model_change": entryShape(entry, ["provider", "modelId"]); text(entry.provider, 256, false); text(entry.modelId, 256, false); break;
      case "usage":
        entryShape(entry, ["kind", "provider", "model", "usage"], ["note"]);
        for (const key of ["kind", "provider", "model"]) text(entry[key], 256, false);
        usage(entry.usage); if (Object.hasOwn(entry, "note")) text(entry.note); break;
      case "compaction":
        entryShape(entry, ["summary", "firstKeptEntryId", "tokensBefore"], ["details", "fromHook", "usage", "systemMessage"]);
        text(entry.summary); identity(entry.firstKeptEntryId); unsigned(entry.tokensBefore);
        if (Object.hasOwn(entry, "fromHook") && typeof entry.fromHook !== "boolean") fail();
        if (Object.hasOwn(entry, "usage")) usage(entry.usage);
        if (Object.hasOwn(entry, "systemMessage")) systemMessage(entry.systemMessage, allowed);
        break;
      case "context_edit":
        entryShape(entry, ["targetId", "replacement"]); identity(entry.targetId);
        if (entry.replacement !== null) shape(entry.replacement, ["content"]);
        break;
      case "label": entryShape(entry, ["targetId"], ["label"]); identity(entry.targetId); if (Object.hasOwn(entry, "label")) text(entry.label); break;
      case "session_info": entryShape(entry, [], ["name"]); if (Object.hasOwn(entry, "name")) text(entry.name); break;
      default: fail(); // No extension/custom/branch-summary/Bash state in the controlled product surface.
    }
  }
  if (leafId !== null) identity(leafId);
  if ((leafId !== null && !byId.has(leafId)) || (entries.length && leafId === null)) fail("CHECKPOINT_INVALID_TREE");
  let roots = 0;
  for (const entry of entries) {
    if (entry.parentId === null) roots++;
    else if (!byId.has(entry.parentId)) fail("CHECKPOINT_INVALID_TREE");
  }
  if (entries.length && roots !== 1) fail("CHECKPOINT_INVALID_TREE");
  // Iterative coloring bounds traversal and rejects cycles in abandoned branches too.
  const done = new Set();
  for (const entry of entries) {
    const path = new Set();
    let current = entry;
    while (current && !done.has(current.id)) {
      if (path.has(current.id)) fail("CHECKPOINT_INVALID_TREE");
      path.add(current.id); current = current.parentId === null ? undefined : byId.get(current.parentId);
    }
    for (const id of path) done.add(id);
  }
  const isAncestor = (target, entry) => {
    for (let id = entry.parentId; id !== null; id = byId.get(id).parentId) if (id === target) return true;
    return false;
  };
  for (const entry of entries) {
    if (entry.type === "compaction" && entry.firstKeptEntryId !== entry.id && !isAncestor(entry.firstKeptEntryId, entry)) fail("CHECKPOINT_INVALID_TREE");
    if (entry.type === "label" && !byId.has(entry.targetId)) fail("CHECKPOINT_INVALID_TREE");
    if (entry.type === "context_edit") {
      const target = byId.get(entry.targetId);
      if (!target || target.type !== "message" || target.message.role === "system" || !isAncestor(entry.targetId, entry)) fail("CHECKPOINT_INVALID_TREE");
      if (entry.replacement !== null)
        content(entry.replacement.content, target.message.role === "user" ? ["string", "text", "image"]
          : target.message.role === "assistant" ? ["string", "text", "thinking", "toolCall"] : ["string", "text", "image"], allowed);
    }
  }
  const active = [];
  for (let id = leafId; id !== null; id = byId.get(id).parentId) active.push(byId.get(id));
  active.reverse();
  // Raw history is retained across compaction. Pair raw active-branch calls,
  // rather than treating a compaction summary as evidence of native execution.
  validateToolPairs(active.filter((entry) => entry.type === "message").map((entry) => entry.message), true);
  validateSelectedContext(active);
}

/** Validate and deep-copy an opaque product snapshot. Never resumes execution. */
export function parseCheckpoint(input, bindings) {
  if (typeof input === "string") {
    if (Buffer.byteLength(input, "utf8") > MAX_CHECKPOINT_BYTES) fail("CHECKPOINT_TOO_LARGE");
    try { input = JSON.parse(input); } catch { fail("CHECKPOINT_INVALID_JSON"); }
  }
  const checkpoint = cloneBoundedJson(input);
  shape(checkpoint, ENVELOPE); // Rejects credentials/profile/baseUrl and all other unknown envelope fields.
  if (checkpoint.schema !== CHECKPOINT_SCHEMA || checkpoint.piVersion !== CHECKPOINT_PI_VERSION) fail("CHECKPOINT_VERSION_UNSUPPORTED");
  if (checkpoint.status !== "completed") fail("CHECKPOINT_NOT_COMPLETED");
  for (const key of ["checkpointId", "conversationId", "projectId", "completedRunId", "toolRevision"]) identity(checkpoint[key]);
  for (const key of ["sdkReceiptSha256", "systemPromptSha256"]) hash(checkpoint[key]);
  if (!record(bindings)) fail();
  for (const key of BINDING_KEYS) {
    if (checkpoint[key] !== bindings[key]) fail("CHECKPOINT_BINDING_MISMATCH");
  }
  const allowed = toolAllowlist(bindings.allowedToolNames);
  shape(checkpoint.header, ["type", "version", "id", "timestamp", "cwd"], ["parentSession"]);
  if (checkpoint.header.type !== "session") fail();
  if (checkpoint.header.version !== 3) fail("CHECKPOINT_VERSION_UNSUPPORTED");
  identity(checkpoint.header.id);
  if (!/^[A-Za-z0-9](?:[A-Za-z0-9._-]*[A-Za-z0-9])?$/.test(checkpoint.header.id)) fail();
  timestamp(checkpoint.header.timestamp); text(checkpoint.header.cwd, 32768, false);
  if (Object.hasOwn(checkpoint.header, "parentSession")) text(checkpoint.header.parentSession, 32768, false);
  validateEntries(checkpoint, allowed);
  return checkpoint;
}

/** Host must attest both terminal boundaries. The SDK owns finalized history. */
export function captureCheckpoint(session, options) {
  if (!record(options) || options.status !== "completed") fail("CHECKPOINT_NOT_COMPLETED");
  if (options.hostSettled !== true || options.hostIdle !== true || session?.isIdle !== true) fail("CHECKPOINT_NOT_SETTLED");
  const manager = session.sessionManager;
  if (!manager || !["getHeader", "getEntries", "getLeafId", "buildSessionContext"].every((name) => typeof manager[name] === "function")) fail();
  const checkpoint = {
    schema: CHECKPOINT_SCHEMA, piVersion: CHECKPOINT_PI_VERSION,
    checkpointId: options.checkpointId, conversationId: options.conversationId, projectId: options.projectId,
    completedRunId: options.completedRunId, toolRevision: options.toolRevision,
    sdkReceiptSha256: options.sdkReceiptSha256, systemPromptSha256: options.systemPromptSha256,
    status: "completed", header: manager.getHeader(), entries: manager.getEntries(), leafId: manager.getLeafId(),
  };
  const captured = parseCheckpoint(checkpoint, options);
  validateManagerContext(manager, options.allowedToolNames);
  return captured;
}

/** Restore only an in-memory manager. Caller separately creates its controlled SDK session. */
export function restoreCheckpoint(SessionManager, input, options) {
  const checkpoint = parseCheckpoint(input, options);
  if (typeof options.currentOwnedCwd !== "string" || !isAbsolute(options.currentOwnedCwd)
      || /[\u0000\r\n]/.test(options.currentOwnedCwd) || Buffer.byteLength(options.currentOwnedCwd, "utf8") > 32768
      || typeof SessionManager?.inMemory !== "function") fail();
  const header = { ...checkpoint.header, cwd: options.currentOwnedCwd };
  try {
    const manager = SessionManager.inMemory(options.currentOwnedCwd, undefined, [header, ...checkpoint.entries]);
    if (checkpoint.leafId === null) manager.resetLeaf();
    else manager.branch(checkpoint.leafId);
    validateManagerContext(manager, options.allowedToolNames);
    return manager;
  } catch (error) {
    if (error instanceof CheckpointError) throw error;
    fail("CHECKPOINT_RESTORE_FAILED");
  }
}
