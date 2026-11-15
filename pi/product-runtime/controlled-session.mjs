// Product-owned Pi SDK assembly. This module runs only in the owned Node child.
// It never opens a second exp-rs MCP host or executes a script itself.
import { isAbsolute, join } from "node:path";
import { readFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import { BlockList, isIP } from "node:net";

export const PI_VERSION = "1.5.2";
export const EXCLUDED_TOOLS = Object.freeze([
  "mcp__*", "codemode", "tool_search", "read", "bash", "powershell",
  "edit", "write", "grep", "find", "ls",
]);
const MAX_TOOL_RESULT_BYTES = 512 * 1024;
const PROVIDER_ID = "exp-rs-product";

async function loadSdk(nodeModules) {
  if (!isAbsolute(nodeModules)) throw new Error("Pi node_modules must be an absolute prepared path");
  const load = async (name) => {
    const directory = join(nodeModules, "@earendil-works", name);
    const metadata = JSON.parse(await readFile(join(directory, "package.json"), "utf8"));
    if (metadata.version !== PI_VERSION) throw new Error(`Unsupported ${name} version`);
    return import(pathToFileURL(join(directory, "dist", "index.js")).href);
  };
  const [sdk, ai] = await Promise.all([load("pi-coding-agent"), load("pi-ai")]);
  return { sdk, ai };
}

function emptyResources(createExtensionRuntime, systemPrompt) {
  const extensions = { extensions: [], errors: [], runtime: createExtensionRuntime() };
  return {
    getExtensions: () => extensions,
    getSkills: () => ({ skills: [], diagnostics: [] }),
    getPrompts: () => ({ prompts: [], diagnostics: [] }),
    getThemes: () => ({ themes: [], diagnostics: [] }),
    getAgentsFiles: () => ({ agentsFiles: [] }),
    getSystemPrompt: () => systemPrompt,
    getSystemPromptSource: () => undefined,
    getAppendSystemPrompt: () => [],
    getAppendSystemPromptSources: () => [],
    extendResources: () => {},
    reload: async () => {},
  };
}

function validateProfile(profile) {
  const endpoint = new URL(profile.baseUrl);
  if (!["http:", "https:"].includes(endpoint.protocol) || endpoint.username || endpoint.password || endpoint.search || endpoint.hash)
    throw new Error("Model endpoint must be an HTTP(S) base URL without credentials, query or fragment");
  if (typeof profile.model !== "string" || !profile.model || typeof profile.apiKey !== "string" || !profile.apiKey)
    throw new Error("Explicit model and in-memory credential are required");
  for (const field of ["contextWindow", "maxTokens"])
    if (!Number.isSafeInteger(profile[field]) || profile[field] <= 0) throw new Error(`Invalid model ${field}`);
  // Preserve the native AS-015 policy without resolving hostnames. BlockList
  // recognizes the complete IPv4 loopback range and its IPv6-mapped forms.
  const hostname = endpoint.hostname.replace(/^\[|\]$/g, "");
  const family = isIP(hostname);
  const loopback = new BlockList();
  loopback.addSubnet("127.0.0.0", 8, "ipv4");
  loopback.addAddress("::1", "ipv6");
  if (endpoint.protocol === "http:" && hostname !== "localhost"
      && (!family || !loopback.check(hostname, family === 6 ? "ipv6" : "ipv4")))
    throw new Error("Refusing to send the API key over plaintext HTTP to a non-loopback endpoint");
  return endpoint.href.replace(/\/$/, "");
}

function hostTools(definitions, onToolRequest) {
  if (!Array.isArray(definitions) || typeof onToolRequest !== "function") throw new Error("Explicit host tools and callback are required");
  const names = new Set();
  const ids = new Set();
  return definitions.map((definition) => {
    const { name, canonicalId, description, parameters } = definition;
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(name) || EXCLUDED_TOOLS.includes(name) || name.startsWith("mcp__"))
      throw new Error("Invalid or reserved host tool name");
    if (names.has(name) || typeof canonicalId !== "string" || !canonicalId || ids.has(canonicalId))
      throw new Error("Duplicate host tool name or canonical id");
    if (!parameters || parameters.type !== "object" || typeof description !== "string")
      throw new Error("Host tools require an object JSON Schema and description");
    names.add(name);
    ids.add(canonicalId);
    return {
      name,
      label: name,
      description,
      parameters: structuredClone(parameters),
      executionMode: "sequential",
      async execute(toolCallId, args, signal) {
        signal?.throwIfAborted();
        const result = await onToolRequest({ toolCallId, canonicalId, arguments: args, signal });
        if (!result || typeof result !== "object" || Array.isArray(result)) throw new Error("Host tool result must be a JSON object");
        const text = JSON.stringify(result);
        if (Buffer.byteLength(text, "utf8") > MAX_TOOL_RESULT_BYTES) throw new Error("Host tool result exceeds product output limit");
        return {
          content: [{ type: "text", text }],
          details: result,
          structuredContent: result,
          isError: ["error", "failed", "cancelled"].includes(result.status),
        };
      },
    };
  });
}

/**
 * Assemble a single in-memory Pi conversation against the locked, patched SDK.
 * The owner must launch its process with PI_OFFLINE=1 and PI_TELEMETRY=0.
 * Node retains the current user's permissions; this is not an OS sandbox.
 */
export async function createControlledSession({ nodeModules, cwd, profile, tools, onToolRequest, systemPrompt }) {
  if (process.env.PI_OFFLINE !== "1" || process.env.PI_TELEMETRY !== "0")
    throw new Error("Owned Pi process requires PI_OFFLINE=1 and PI_TELEMETRY=0");
  if (!isAbsolute(cwd) || typeof systemPrompt !== "string" || !systemPrompt)
    throw new Error("Explicit owned cwd and system prompt are required");
  const baseUrl = validateProfile(profile);
  const customTools = hostTools(tools, onToolRequest);
  const { sdk, ai } = await loadSdk(nodeModules);
  const authRequests = { environment: 0, file: 0 };
  const credentials = new ai.InMemoryCredentialStore();
  await credentials.modify(PROVIDER_ID, async () => ({ type: "api_key", key: profile.apiKey }));
  const modelRuntime = await sdk.ModelRuntime.create({
    credentials,
    modelsPath: null,
    modelsStore: new ai.InMemoryModelsStore(),
    allowModelNetwork: false,
    refreshOnCreate: false,
    authContext: {
      env: async () => { authRequests.environment++; return undefined; },
      fileExists: async () => { authRequests.file++; return false; },
    },
  });
  modelRuntime.registerProvider(PROVIDER_ID, {
    baseUrl,
    api: "openai-completions",
    authHeader: true,
    models: [{
      id: profile.model,
      name: profile.model,
      reasoning: false,
      input: ["text"],
      contextWindow: profile.contextWindow,
      maxTokens: profile.maxTokens,
      cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 },
      compat: { supportsStore: false, supportsDeveloperRole: false, supportsReasoningEffort: false, maxTokensField: "max_tokens" },
    }],
  });
  await modelRuntime.refresh({ allowNetwork: false });
  if (!authRequests.environment || !authRequests.file)
    throw new Error("Pi SDK does not honor the required product AuthContext patch");
  const model = modelRuntime.getModel(PROVIDER_ID, profile.model);
  if (!model) throw new Error("Configured model is unavailable");
  const settingsManager = sdk.SettingsManager.inMemory({
    enableInstallTelemetry: false,
    enableAnalytics: false,
    enableSkillCommands: false,
    cacheWarming: "off",
    retry: { enabled: false, provider: { maxRetries: 0 } },
    compaction: { enabled: true, reserveTokens: 1024, keepRecentTokens: 1024 },
    transport: "sse",
  });
  const { session } = await sdk.createAgentSession({
    cwd,
    agentDir: join(cwd, ".product-agent-unused"),
    modelRuntime,
    model,
    thinkingLevel: "off",
    settingsManager,
    sessionManager: sdk.SessionManager.inMemory(cwd),
    resourceLoader: emptyResources(sdk.createExtensionRuntime, systemPrompt),
    noTools: "all",
    tools: customTools.map((tool) => tool.name),
    excludeTools: [...EXCLUDED_TOOLS],
    customTools,
  });
  const expected = customTools.map((tool) => tool.name).sort();
  const exposed = session.getActiveToolNames().sort();
  const callable = session.getCallableToolNames().sort();
  if (JSON.stringify(exposed) !== JSON.stringify(expected) || JSON.stringify(callable) !== JSON.stringify(expected)) {
    session.dispose();
    throw new Error("Pi exposed an unexpected tool loadout");
  }
  return {
    session,
    prompt: (text) => session.prompt(text, { expandPromptTemplates: false }),
    diagnostics: () => ({ version: PI_VERSION, authRequests: { ...authRequests }, tools: [...exposed], callableTools: [...callable] }),
  };
}
