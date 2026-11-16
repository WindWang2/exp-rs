import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const fixture = fileURLToPath(new URL("./auth-context-fixture.mjs", import.meta.url));

test("ModelRuntime uses only the injected auth context and host memory credential", { timeout: 90000 }, async () => {
  const nodeModules = process.env.SICNU_TEST_PI_NODE_MODULES;
  assert.ok(nodeModules && path.isAbsolute(nodeModules), "Set SICNU_TEST_PI_NODE_MODULES to the locked node_modules root");
  const temporaryRoot = path.resolve(tmpdir());
  const workspace = await mkdtemp(path.join(temporaryRoot, "sicnu-pi-auth-"));
  try {
    const home = path.join(workspace, "home");
    const temporary = path.join(workspace, "temp");
    await mkdir(path.join(home, ".config", "gcloud"), { recursive: true });
    await mkdir(temporary);
    const adc = path.join(home, ".config", "gcloud", "application_default_credentials.json");
    await writeFile(adc, JSON.stringify({ type: "authorized_user", client_id: "fixture-only" }));
    const emptyAws = path.join(home, "empty-aws-config");
    await writeFile(emptyAws, "");

    // These are synthetic credentials in an owned HOME. The original SDK
    // incorrectly considers them available despite the denied AuthContext.
    const environment = {
      PATH: path.dirname(process.execPath),
      HOME: home,
      USERPROFILE: home,
      APPDATA: path.join(home, "AppData", "Roaming"),
      LOCALAPPDATA: path.join(home, "AppData", "Local"),
      XDG_CONFIG_HOME: path.join(home, ".config"),
      TEMP: temporary,
      TMP: temporary,
      PI_OFFLINE: "1",
      PI_TELEMETRY: "0",
      OPENAI_API_KEY: "fixture-ambient-key-must-not-be-used",
      GOOGLE_APPLICATION_CREDENTIALS: adc,
      GOOGLE_CLOUD_PROJECT: "fixture-project",
      GOOGLE_CLOUD_LOCATION: "us-central1",
      AWS_CONFIG_FILE: emptyAws,
      AWS_SHARED_CREDENTIALS_FILE: emptyAws,
      AWS_EC2_METADATA_DISABLED: "true",
    };
    for (const name of ["SystemRoot", "WINDIR"]) {
      if (process.env[name]) environment[name] = process.env[name];
    }

    const result = await new Promise((resolve, reject) => {
      const child = spawn(process.execPath, [fixture, nodeModules], {
        cwd: workspace,
        env: environment,
        windowsHide: true,
        stdio: ["ignore", "pipe", "pipe"],
      });
      let output = "";
      let errors = "";
      const timeout = setTimeout(() => child.kill(), 75000);
      child.stdout.setEncoding("utf8").on("data", (chunk) => { output += chunk; });
      child.stderr.setEncoding("utf8").on("data", (chunk) => { errors += chunk; });
      child.once("error", (error) => { clearTimeout(timeout); reject(error); });
      child.once("close", (code, signal) => {
        clearTimeout(timeout);
        if (code !== 0) {
          reject(new Error(`Auth fixture exited ${code ?? signal}: ${errors}`));
          return;
        }
        try { resolve(JSON.parse(output)); } catch (error) { reject(error); }
      });
    });

    assert.equal(result.openaiAvailable, 0, "Ambient OpenAI environment must not authorize a built-in provider");
    assert.equal(result.vertexAvailable, 0, "Ambient ADC file must not authorize a built-in provider");
    assert.ok(result.envQueries > 0, "The injected environment resolver must be used");
    assert.ok(result.fileQueries > 0, "The injected file resolver must be used instead of probing disk");
    assert.equal(result.hostModelAvailable, true, "The explicit host memory credential remains usable");
    assert.equal(result.memoryCredentialCount, 1, "Discovery must not persist ambient credentials");
  } finally {
    assert.equal(path.dirname(path.resolve(workspace)), temporaryRoot, "Cleanup must stay under the owned temporary root");
    assert.ok(path.basename(workspace).startsWith("sicnu-pi-auth-"), "Cleanup target must be this fixture's directory");
    await rm(workspace, { recursive: true, force: true });
  }
});
