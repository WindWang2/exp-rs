// #2080: redaction semantics for every configured credential. Pure data
// tests over redactCheckpoint — no prepared SDK or node_modules required.
import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { redactCheckpoint } from "../protocol.mjs";
import { CheckpointError } from "../checkpoint.mjs";

const sha256 = (text) => createHash("sha256").update(text).digest("hex");
const bindings = {
  conversationId: "conversation-1", projectId: "project-1", toolRevision: "catalog-1",
  sdkReceiptSha256: sha256("sdk"), systemPromptSha256: sha256("system-prompt"),
  allowedToolNames: ["exprs_tool"],
};
const usage = { input: 1, output: 1, cacheRead: 0, cacheWrite: 0, totalTokens: 2,
  cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } };

// Minimal valid completed checkpoint: user turn with digits in the text, an
// assistant toolCall round, the host toolResult, and a final assistant turn.
function baseCheckpoint() {
  return {
    schema: "sicnu.pi-checkpoint/1", piVersion: "1.5.2",
    checkpointId: "cp-1", conversationId: bindings.conversationId, projectId: bindings.projectId,
    completedRunId: "run-1", toolRevision: bindings.toolRevision,
    sdkReceiptSha256: bindings.sdkReceiptSha256, systemPromptSha256: bindings.systemPromptSha256,
    status: "completed",
    header: { type: "session", version: 3, id: "header-1", timestamp: "2026-11-17T00:00:00.000Z", cwd: "/owned-1" },
    entries: [
      { type: "message", id: "e-1", parentId: null, timestamp: "2026-11-17T00:00:00.001Z",
        message: { role: "user", content: "run step 1 on band 1", timestamp: 1 } },
      { type: "message", id: "e-2", parentId: "e-1", timestamp: "2026-11-17T00:00:00.002Z",
        message: { role: "assistant", content: [{ type: "toolCall", id: "call-1", name: "exprs_tool", arguments: { arg1: "value-1" } }],
          api: "sse", provider: "fixture", model: "fixture-1", usage, stopReason: "toolUse", timestamp: 2 } },
      { type: "message", id: "e-3", parentId: "e-2", timestamp: "2026-11-17T00:00:00.003Z",
        message: { role: "toolResult", toolCallId: "call-1", toolName: "exprs_tool", content: [{ type: "text", text: "result 100 ok" }], isError: false, timestamp: 3 } },
      { type: "message", id: "e-4", parentId: "e-3", timestamp: "2026-11-17T00:00:00.004Z",
        message: { role: "assistant", content: [{ type: "text", text: "done in 1 step" }], api: "sse", provider: "fixture", model: "fixture-1", usage, stopReason: "stop", timestamp: 4 } },
    ],
    leafId: "e-4",
  };
}

test("non-colliding credentials save without corrupting structure or text", () => {
  for (const dummy of ["none", "dummy12"]) {
    const saved = redactCheckpoint(baseCheckpoint(), dummy, bindings);
    assert.equal(saved.schema, "sicnu.pi-checkpoint/1");
    assert.equal(saved.conversationId, bindings.conversationId);
    assert.equal(saved.entries.at(-1).message.content[0].text, "done in 1 step");
    assert.equal(saved.entries[0].message.content, "run step 1 on band 1");
    assert.equal(JSON.stringify(saved).includes("[REDACTED]"), false);
  }
});

test("short provider credentials are scrubbed from user, tool and assistant text", () => {
  const secret = "abc1234";
  const checkpoint = baseCheckpoint();
  checkpoint.entries[0].message.content = `user echoes ${secret}`;
  checkpoint.entries[1].message.content[0].arguments.arg1 = `argument ${secret}`;
  checkpoint.entries[2].message.content[0].text = `result ${secret}`;
  checkpoint.entries[3].message.content[0].text = `answer ${secret}`;
  const saved = redactCheckpoint(checkpoint, secret, bindings);
  assert.equal(saved.entries[0].message.content, "user echoes [REDACTED]");
  assert.equal(saved.entries[1].message.content[0].arguments.arg1, "argument [REDACTED]");
  assert.equal(saved.entries[2].message.content[0].text, "result [REDACTED]");
  assert.equal(saved.entries[3].message.content[0].text, "answer [REDACTED]");
  assert.equal(JSON.stringify(saved).includes(secret), false);
});

test("short credentials echoed with JSON escaping are scrubbed as text", () => {
  const secret = 'q"w\\e';
  const escaped = JSON.stringify(secret).slice(1, -1);
  const checkpoint = baseCheckpoint();
  checkpoint.entries[0].message.content = `escaped ${escaped}`;
  const saved = redactCheckpoint(checkpoint, secret, bindings);
  assert.equal(saved.entries[0].message.content, "escaped [REDACTED]");
});

test("short credentials colliding with identities, hashes or keys refuse without mutation", () => {
  const secret = "abc1234";
  const identityCollision = baseCheckpoint();
  identityCollision.conversationId = `conversation-${secret}`;
  const identityBefore = structuredClone(identityCollision);
  assert.throws(() => redactCheckpoint(identityCollision, secret,
    { ...bindings, conversationId: identityCollision.conversationId }),
  (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");
  assert.deepEqual(identityCollision, identityBefore);

  const hashSecret = bindings.sdkReceiptSha256.slice(0, 7);
  assert.throws(() => redactCheckpoint(baseCheckpoint(), hashSecret, bindings),
    (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");

  const keyCollision = baseCheckpoint();
  keyCollision.entries[1].message.content[0].arguments = { [secret]: "must not persist" };
  assert.throws(() => redactCheckpoint(keyCollision, secret, bindings),
    (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");
});

test("a one-character credential collision refuses instead of changing checkpoint identities", () => {
  const checkpoint = baseCheckpoint();
  const before = structuredClone(checkpoint);
  assert.throws(() => redactCheckpoint(checkpoint, "1", bindings),
    (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");
  assert.deepEqual(checkpoint, before);
});

test("real secrets are still scrubbed from free-form text", () => {
  const secret = "fixture-credential-quote\"slash\\-not-user";
  const checkpoint = baseCheckpoint();
  checkpoint.entries.at(-1).message.content[0].text = `partial ${secret} echo`;
  checkpoint.entries[0].message.content = `user echoes ${JSON.stringify(secret).slice(1, -1)} escaped`;
  const saved = redactCheckpoint(checkpoint, secret, bindings);
  const serialized = JSON.stringify(saved);
  assert.equal(serialized.includes(secret), false);
  assert.equal(serialized.includes(JSON.stringify(secret).slice(1, -1)), false);
  assert.ok(serialized.includes("[REDACTED]"));
});

test("real secrets colliding with identities or object keys still refuse the save", () => {
  const secret = "real-provider-secret-0123456789";
  const identityCollision = baseCheckpoint();
  identityCollision.conversationId = `conversation-${secret}`;
  assert.throws(() => redactCheckpoint(identityCollision, secret, { ...bindings, conversationId: identityCollision.conversationId }),
    (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");

  const keyCollision = baseCheckpoint();
  keyCollision.entries[1].message.content[0].arguments = { [secret]: "must not persist" };
  assert.throws(() => redactCheckpoint(keyCollision, secret, bindings),
    (error) => error instanceof CheckpointError && error.code === "CHECKPOINT_REDACTION_FAILED");
});

test("an eight-character boundary secret keeps full strictness", () => {
  const secret = "12345678";
  const checkpoint = baseCheckpoint();
  checkpoint.entries.at(-1).message.content[0].text = `leak ${secret} here`;
  const saved = redactCheckpoint(checkpoint, secret, bindings);
  assert.equal(JSON.stringify(saved).includes(secret), false);
});
