// Sole installed entry point. No upstream RPC command surface is exposed.
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { fileURLToPath, pathToFileURL } from "node:url";
import { resolve } from "node:path";
import { createControlledSession, PI_VERSION } from "./controlled-session.mjs";
import { JsonLineDecoder, PrivateSession, ProtocolError, LIMITS } from "./protocol.mjs";

const ENTRY = "libexec/sicnu-geo-rs/pi-product/main.mjs";
const NODE_VERSION = "24.22.3";
const TARGET = "x86_64-pc-windows-msvc";
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");

/** Resolve only the relocatable installation containing this entry point. */
export async function loadInstallation(entry = import.meta.url) {
  try {
    const prefix = new URL("../../../", entry);
    const [productBytes, sdkBytes] = await Promise.all([
      readFile(new URL("share/sicnu-geo-rs/pi-product.json", prefix)),
      readFile(new URL("share/sicnu-geo-rs/pi-runtime.json", prefix)),
    ]);
    const product = JSON.parse(productBytes.toString("utf8"));
    const sdk = JSON.parse(sdkBytes.toString("utf8"));
    const required = product.requires_sdk;
    const sdkReceiptSha256 = hash(sdkBytes);
    const recipes = `{${Object.keys(sdk.recipes).sort().map((key) => `${JSON.stringify(key)}:${JSON.stringify(sdk.recipes[key])}`).join(",")}}`;
    if (product.schema !== "sicnu.pi-product/1" || product.protocol_version !== 1 || product.entry !== ENTRY
        || new URL(ENTRY, prefix).href !== new URL(entry).href
        || sdk.schema !== "sicnu.pi-runtime/1" || sdk.target !== TARGET || sdk.pi_version !== PI_VERSION || sdk.node_version !== NODE_VERSION
        || required.target !== TARGET || required.pi_version !== PI_VERSION || required.node_version !== NODE_VERSION
        || required.receipt_sha256 !== sdkReceiptSha256 || required.recipe_digest !== hash(recipes)
        || process.versions.node !== NODE_VERSION) throw new Error();
    const expected = ["main.mjs", "protocol.mjs", "controlled-session.mjs", "checkpoint.mjs"].map((name) => `libexec/sicnu-geo-rs/pi-product/${name}`);
    if (!Array.isArray(product.files) || product.files.length !== expected.length) throw new Error();
    for (const path of expected) {
      const records = product.files.filter((file) => file.path === path);
      if (records.length !== 1) throw new Error();
      const bytes = await readFile(new URL(path, prefix));
      if (records[0].size !== bytes.length || records[0].sha256 !== hash(bytes)) throw new Error();
    }
    return {
      nodeModules: fileURLToPath(new URL("libexec/sicnu-geo-rs/pi-agent/node_modules", prefix)),
      metadata: { nodeVersion: NODE_VERSION, piVersion: PI_VERSION, sdkReceiptSha256 },
    };
  } catch { throw new ProtocolError("INSTALLATION_INVALID"); }
}

async function main() {
  if (process.argv.length !== 2) throw new ProtocolError("INVALID_ARGUMENTS");
  // Console output from dependencies must not contaminate the JSONL channel or
  // reveal provider diagnostics/credentials. All diagnostics here are constants.
  let diagnosticBytes = 0;
  const diagnostic = () => {
    const text = "Pi SDK diagnostic suppressed.\n";
    if (diagnosticBytes + text.length <= 4096) { diagnosticBytes += text.length; process.stderr.write(text); }
  };
  for (const method of ["log", "info", "debug", "warn", "error"]) console[method] = diagnostic;
  process.env.PI_OFFLINE = "1"; process.env.PI_TELEMETRY = "0";
  const installation = await loadInstallation();
  let stopped = false, queuedBytes = 0;
  const decoder = new JsonLineDecoder();
  const protocol = new PrivateSession({
    metadata: installation.metadata,
    createSession: (options) => createControlledSession({ ...options, nodeModules: installation.nodeModules }),
    emit(frame) {
      const line = `${JSON.stringify(frame)}\n`;
      const bytes = Buffer.byteLength(line);
      if (bytes - 1 > LIMITS.frameBytes || queuedBytes + bytes > 2 * LIMITS.frameBytes) {
        // SDK tool execution may catch callback exceptions. Terminate at the
        // transport owner as well, so it cannot swallow a broken output pipe.
        queueMicrotask(() => { void terminate("output_error", 1); });
        throw new ProtocolError("OUTPUT_BACKPRESSURE");
      }
      queuedBytes += bytes;
      process.stdout.write(line, () => { queuedBytes -= bytes; });
    },
  });
  const terminate = async (reason, exitCode = 0) => {
    if (stopped) return;
    stopped = true;
    const deadline = setTimeout(() => process.exit(exitCode), 1000);
    try { await protocol.disconnect(reason); } catch { /* Only fixed diagnostics leave this process. */ }
    await new Promise((done) => process.stdout.write("", done));
    clearTimeout(deadline); process.exit(exitCode);
  };
  process.stdout.on("error", () => { void terminate("disconnect", 1); });
  process.stdin.on("error", () => { void terminate("disconnect", 1); });
  // Initialization may be awaiting an SDK import. Do not defer loss-of-host
  // detection until that awaited command returns to the input iterator.
  process.stdin.on("end", () => { void terminate("eof"); });
  process.stdin.on("close", () => { if (!stopped) void terminate("disconnect"); });
  process.on("SIGTERM", () => { void terminate("terminated", 1); });
  process.on("SIGINT", () => { void terminate("terminated", 1); });
  process.on("uncaughtException", () => { void terminate("runtime_error", 1); });
  process.on("unhandledRejection", () => { void terminate("runtime_error", 1); });
  try {
    for await (const chunk of process.stdin) {
      for (const frame of decoder.push(chunk)) {
        await protocol.accept(frame);
        if (frame.type === "shutdown") { await terminate("shutdown"); return; }
      }
    }
    decoder.end(); await terminate("eof");
  } catch (error) {
    try { protocol.protocolFailure(error instanceof ProtocolError ? error.code : "RUNTIME_ERROR"); } catch { /* Output can be disconnected. */ }
    await terminate("protocol_error", 1);
  }
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  main().catch(() => { process.stderr.write("Pi product runtime startup failed.\n"); process.exit(1); });
}
